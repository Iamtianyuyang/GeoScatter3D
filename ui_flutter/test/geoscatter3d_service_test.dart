import 'dart:async';
import 'dart:ui' as ui;
import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/ffi/geoscatter3d_service.dart';
import 'package:ui_flutter/src/ffi/gs3d_engine.dart';
import 'support/fake_engine.dart';
import 'support/fake_present_surface.dart';

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

  group('零拷贝呈现通路', () {
    test('拿到视口尺寸后接管纹理，present 模式不再回读像素', () async {
      final engine = FakeEngine();
      final binder = FakePresentSurfaceBinder();
      final service = GeoScatter3dService(
        engine: engine,
        presentBinder: binder,
      );
      addTearDown(service.dispose);
      await service.initialize();
      expect(service.zeroCopyPresent, isFalse);

      service.requestSize(const ui.Size(320, 240));
      await service.renderNextFrame();
      expect(service.zeroCopyPresent, isTrue);
      expect(service.textureId, 42);
      expect(binder.attachCount, 1);
      expect(binder.attachSizes.single, (320, 240));
      // 纹理接管前仍需同步引擎宽高比（相机投影依赖它）。
      expect(engine.sizes, [(320, 240)]);

      final reads = engine.readCount;
      final requests = engine.requestCount;
      for (var i = 0; i < 5; i++) {
        await service.renderNextFrame();
      }
      // present 模式：每个 tick 推帧，但永不回读（frameSequence 不再变化）。
      expect(engine.readCount, reads);
      expect(engine.requestCount, requests + 5);
      expect(service.frame, isNull);
      expect(service.error, isNull);
    });

    test('present 模式周期性从诊断刷新遥测，不靠帧序号', () async {
      final engine = FakeEngine();
      engine.setLodScenario(source: 1000000, drawn: 79400, level: 2,
          levelCount: 3);
      final binder = FakePresentSurfaceBinder();
      final service = GeoScatter3dService(
        engine: engine,
        presentBinder: binder,
      );
      addTearDown(service.dispose);
      await service.initialize();
      service.requestSize(const ui.Size(320, 240));
      await service.renderNextFrame();
      expect(service.zeroCopyPresent, isTrue);

      final frameNumber = service.frameNumber;
      for (var i = 0; i < kPresentTelemetryTicks; i++) {
        await service.renderNextFrame();
      }
      // 遥测（fps / 绘制点数 / LOD 摘要 / 帧计数）必须真的在动。
      expect(service.frameNumber, frameNumber + 1);
      expect(service.fps, 58.5);
      expect(service.frameTimeMs, closeTo(1000 / 58.5, 0.01));
      expect(service.pointCount, 79400);
      expect(service.sourcePointCount, 1000000);
      expect(service.lodSummary, contains('LOD 3/3'));
    });

    test('纹理 id 0 是合法 id，必须接管而不是当成不可用', () async {
      // 回归：Flutter 纹理注册表的首个纹理 id 就是 0；若用 0 当「不可用」哨兵，
      // 就会出现「Kotlin 已把引擎挂到 Surface、Dart 却走回读」的死屏状态。
      final engine = FakeEngine();
      final binder = FakePresentSurfaceBinder(textureId: 0);
      final service = GeoScatter3dService(
        engine: engine,
        presentBinder: binder,
      );
      addTearDown(service.dispose);
      await service.initialize();
      service.requestSize(const ui.Size(320, 240));
      await service.renderNextFrame();
      expect(service.zeroCopyPresent, isTrue);
      expect(service.textureId, 0);
      final reads = engine.readCount;
      await service.renderNextFrame();
      expect(engine.readCount, reads);
    });

    test('平台没有纹理通路时（attach 返回 null）保持回读', () async {
      final engine = FakeEngine();
      final binder = FakePresentSurfaceBinder()..unsupported = true;
      final service = GeoScatter3dService(
        engine: engine,
        presentBinder: binder,
      );
      addTearDown(service.dispose);
      await service.initialize();
      service.requestSize(const ui.Size(320, 240));
      await service.renderNextFrame();
      expect(service.zeroCopyPresent, isFalse);
      expect(service.textureId, isNull);
      final reads = engine.readCount;
      await service.renderNextFrame();
      expect(engine.readCount, greaterThan(reads));
    });

    test('纹理不可用时保持回读路径，并只记录一次', () async {
      final engine = FakeEngine();
      final recorder = ui.PictureRecorder();
      ui.Canvas(recorder);
      final picture = recorder.endRecording();
      final source = picture.toImageSync(2, 2);
      picture.dispose();
      // 每次读回一个新 clone（与真引擎一致）；clone 所有权归服务。
      engine.render = () async => source.clone();
      addTearDown(source.dispose);
      final failed = FakePresentSurfaceBinder()..failAttach = true;
      final service = GeoScatter3dService(
        engine: engine,
        presentBinder: failed,
      );
      addTearDown(service.dispose);
      await service.initialize();
      service.requestSize(const ui.Size(320, 240));
      await service.renderNextFrame();
      expect(service.zeroCopyPresent, isFalse);
      expect(failed.attachCount, 1);

      // 回读路径必须仍然工作（不得因纹理失败而主屏黑屏）。
      final reads = engine.readCount;
      await service.renderNextFrame();
      expect(engine.readCount, greaterThan(reads));
      expect(service.frame, isNotNull);
      expect(service.error, isNull);

      // 同一尺寸只尝试一次：后续 tick 不再反复调用平台层。
      await service.renderNextFrame();
      expect(failed.attachCount, 1);

      // 尺寸变化（旋转/分屏）后再给一次机会——此前只试一次会让一次失败永久失去纹理。
      service.requestSize(const ui.Size(640, 480));
      await service.renderNextFrame();
      expect(failed.attachCount, 2);
      expect(service.zeroCopyPresent, isFalse);
    });

    test('resize 在 present 模式下交给平台层，并同步引擎宽高比', () async {
      final engine = FakeEngine();
      final binder = FakePresentSurfaceBinder();
      final service = GeoScatter3dService(
        engine: engine,
        presentBinder: binder,
      );
      addTearDown(service.dispose);
      await service.initialize();
      service.requestSize(const ui.Size(320, 240));
      await service.renderNextFrame();
      expect(service.zeroCopyPresent, isTrue);

      service.requestSize(const ui.Size(640, 480));
      await service.renderNextFrame();
      expect(binder.resizeCount, 1);
      expect(binder.resizeSizes.single, (640, 480));
      expect(engine.sizes, [(320, 240), (640, 480)]);

      // 尺寸未变时不重复下发（避免每帧重建交换链）。
      service.requestSize(const ui.Size(640, 480));
      await service.renderNextFrame();
      expect(binder.resizeCount, 1);
    });

    test('dispose 先解除纹理通路再释放引擎', () async {
      final engine = FakeEngine();
      final binder = FakePresentSurfaceBinder();
      final service = GeoScatter3dService(
        engine: engine,
        presentBinder: binder,
      );
      await service.initialize();
      service.requestSize(const ui.Size(320, 240));
      await service.renderNextFrame();
      expect(service.zeroCopyPresent, isTrue);

      service.dispose();
      // detach 是异步的：等一拍确保已下发。
      await Future<void>.delayed(Duration.zero);
      expect(binder.detachCount, 1);
      expect(binder.detachedTextureId, 42);
      expect(service.textureId, isNull);
    });
  });
}
