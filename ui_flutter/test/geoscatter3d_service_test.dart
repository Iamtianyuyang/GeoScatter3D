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
    'resize is deferred and coalesced; missing frame is not an error',
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
      // 引擎未回帧时不得当成致命错误，也不得伪造帧。
      expect(service.frame, isNull);
      expect(service.error, isNull);
      expect(engine.requestCount, greaterThanOrEqualTo(1));
    },
  );
  test('no new sequence means no read and no error, only a new request', () async {
    final engine = FakeEngine()..stallFrames = true;
    final service = GeoScatter3dService(engine: engine);
    addTearDown(service.dispose);
    await service.initialize();
    final requests = engine.requestCount;
    await service.renderNextFrame();
    await service.renderNextFrame();
    expect(engine.readCount, 0);
    expect(service.error, isNull);
    expect(service.frame, isNull);
    // 每个 tick 都向渲染线程预约下一帧（内核侧会合并重复请求）。
    expect(engine.requestCount, requests + 2);
  });
  test('published frame is decoded once and updates telemetry', () async {
    final engine = FakeEngine();
    final completer = Completer<ui.Image?>();
    engine.render = () => completer.future;
    final service = GeoScatter3dService(engine: engine);
    addTearDown(service.dispose);
    await service.initialize();
    expect(service.frameNumber, 0);
    final rendering = service.renderNextFrame();
    final recorder = ui.PictureRecorder();
    ui.Canvas(recorder);
    final picture = recorder.endRecording();
    final image = picture.toImageSync(4, 4);
    picture.dispose();
    completer.complete(image);
    await rendering;
    expect(engine.readCount, 1);
    expect(service.frame, isNotNull);
    expect(service.frameNumber, 1);
    // 消耗一帧后立即预约下一帧（initialize + 帧后各一次）。
    expect(engine.requestCount, greaterThanOrEqualTo(2));
    // 帧所有权归服务：由 service.dispose() 释放，测试不重复 dispose。
  });
  test('pause and in-flight guard prevent extra frame reads', () async {
    final completing = Completer<ui.Image?>();
    final engine = FakeEngine()..render = () => completing.future;
    final service = GeoScatter3dService(engine: engine);
    await service.initialize();
    service.setPaused(true);
    await service.renderNextFrame();
    expect(engine.readCount, 0);
    service.setPaused(false);
    final rendering = service.renderNextFrame();
    await service.renderNextFrame();
    expect(engine.readCount, 1);
    service.dispose();
    completing.complete(null);
    await rendering;
  });
  test('engine diagnostics expose the real GPU name', () async {
    final engine = FakeEngine();
    final service = GeoScatter3dService(engine: engine);
    addTearDown(service.dispose);
    await service.initialize();
    expect(service.gpuName, 'fixture GPU');
  });
  test('lod summary appears only when the source is scaled down', () async {
    final engine = FakeEngine();
    final service = GeoScatter3dService(engine: engine);
    addTearDown(service.dispose);
    await service.initialize();
    // 10k 样例：绘制量等于源点，不显示 LOD 行。
    expect(service.lodSummary, isEmpty);
    expect(service.pointCount, 10000);
    expect(service.sourcePointCount, 10000);
  });
  test('lod summary reports drawn vs source and level', () async {
    final engine = FakeEngine()
      ..setLodScenario(source: 10000000, drawn: 393216, level: 5, levelCount: 6);
    final service = GeoScatter3dService(engine: engine);
    addTearDown(service.dispose);
    await service.initialize();
    expect(service.pointCount, 393216);
    expect(service.sourcePointCount, 10000000);
    expect(service.lodSummary, '39.3 万 / 1000 万 点 · LOD 6/6');
  });
  test('point count formatting uses chinese scales', () {
    expect(GeoScatter3dService.formatPointCount(9999), '9999');
    expect(GeoScatter3dService.formatPointCount(10000), '1.0 万');
    expect(GeoScatter3dService.formatPointCount(393216), '39.3 万');
    expect(GeoScatter3dService.formatPointCount(10000000), '1000 万');
    expect(GeoScatter3dService.formatPointCount(123456789), '1.2 亿');
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
