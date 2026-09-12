import 'dart:async';
import 'dart:ui' as ui;
import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/ffi/geoscatter3d_service.dart';
import 'package:ui_flutter/src/ffi/gs3d_engine.dart';
import 'support/fake_engine.dart';

void main() {
  test('factory failure is explicit, never ready or a fake frame', () async {
    final service = GeoScatter3dService(
      createEngine: () async => throw StateError('no Vulkan'),
    );
    addTearDown(service.dispose);
    await service.initialize();
    expect(service.ready, isFalse);
    expect(service.error, contains('no Vulkan'));
    expect(service.frame, isNull);
  });
  test(
    'initialization is idempotent and borrowed engine is not destroyed',
    () async {
      final engine = FakeEngine();
      final service = GeoScatter3dService(engine: engine);
      await service.initialize();
      await service.initialize();
      expect(engine.loadCount, 1);
      expect(service.ready, isTrue);
      expect(service.pointCount, 10000);
      service.dispose();
      expect(engine.disposeCount, 0);
    },
  );
  test(
    'shutdown before asynchronous creation releases the late owned engine',
    () async {
      final created = Completer<Gs3dEngine>();
      final engine = FakeEngine();
      final service = GeoScatter3dService(createEngine: () => created.future);
      final initializing = service.initialize();
      service.dispose();
      created.complete(engine);
      await initializing;
      expect(engine.disposeCount, 1);
      expect(engine.loadCount, 0);
    },
  );
  test('sample failure is not ready and shutdown is idempotent', () async {
    final engine = FakeEngine()..sampleResult = -3;
    final service = GeoScatter3dService(createEngine: () async => engine);
    await service.initialize();
    expect(service.error, contains('fixture native failure'));
    expect(service.ready, isFalse);
    service.shutdown();
    service.shutdown();
    service.dispose();
    expect(engine.disposeCount, 1);
  });
  test(
    'resize is deferred and coalesced; null frame becomes terminal error',
    () async {
      final engine = FakeEngine();
      final service = GeoScatter3dService(engine: engine);
      addTearDown(service.dispose);
      await service.initialize();
      service.requestSize(const ui.Size(100, 100));
      service.requestSize(const ui.Size(320, 240));
      service.requestSize(const ui.Size(double.infinity, 240));
      expect(engine.sizes, isEmpty);
      await service.renderNextFrame();
      expect(engine.sizes, [(320, 240)]);
      expect(service.error, contains('没有返回帧'));
      await service.renderNextFrame();
      expect(engine.renderCount, 1);
    },
  );
  test('pause and in-flight guard prevent extra render calls', () async {
    final completing = Completer<ui.Image?>();
    final engine = FakeEngine()..render = () => completing.future;
    final service = GeoScatter3dService(engine: engine);
    await service.initialize();
    service.setPaused(true);
    await service.renderNextFrame();
    expect(engine.renderCount, 0);
    service.setPaused(false);
    final rendering = service.renderNextFrame();
    await service.renderNextFrame();
    expect(engine.renderCount, 1);
    service.dispose();
    completing.complete(null);
    await rendering;
  });
  test(
    'actions forward inputs and reject invalid or unknown requests',
    () async {
      final engine = FakeEngine();
      final service = GeoScatter3dService(engine: engine);
      addTearDown(service.dispose);
      expect(() => service.executeAction('camera.reset'), throwsStateError);
      await service.initialize();
      service.executeAction('camera.input', {'rotDx': 25, 'scrollY': 2});
      expect(engine.input!['rotDx'], 25);
      expect(engine.input!['scrollY'], 2);
      service.executeAction('camera.reset');
      expect(engine.resetCount, 1);
      expect(() => service.executeAction('missing'), throwsUnsupportedError);
      expect(
        () => service.executeAction('camera.input', {'rotDx': double.nan}),
        throwsArgumentError,
      );
      expect(service.ready, isTrue);
    },
  );
}
