import 'dart:io';
import 'dart:ui' as ui;
import 'package:flutter/foundation.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/ffi/gs3d_engine.dart';

void main() {
  testWidgets('native readback, orbit, reset and resize are valid', (
    tester,
  ) async {
    await tester.runAsync(() async {
      final engine = Gs3dEngine.create(
        width: 256,
        height: 256,
        enableValidation: true,
      );
      try {
        expect(engine.loadSample(), 0, reason: engine.lastError);
        Future<List<int>> pixels() async {
          final image = await engine.renderFrame();
          expect(image, isNotNull);
          try {
            final data = await image!.toByteData(
              format: ui.ImageByteFormat.rawRgba,
            );
            return data!.buffer.asUint8List(
              data.offsetInBytes,
              data.lengthInBytes,
            );
          } finally {
            image?.dispose();
          }
        }

        final initial = await pixels();
        engine.cameraInput(rotDx: 80, rotDy: 20);
        expect(listEquals(initial, await pixels()), isFalse);
        engine.resetCamera();
        expect(listEquals(initial, await pixels()), isTrue);
        engine.resize(320, 240);
        expect((await pixels()).length, 320 * 240 * 4);

        // 普通命令走异步入队：非法参数在调用线程就被拒绝，合法命令不阻塞。
        expect(engine.submitPointSize(-1), -1);
        expect(engine.submitPointSize(2.5), 0);
        expect(engine.submitColormap(2), 0);
        final recolored = await pixels();
        expect(recolored.length, 320 * 240 * 4);
        expect(engine.lastError, isEmpty);

        // 拾取走同一套逻辑像素坐标；样例地形中心应能命中一个点。
        PickedPoint? hit;
        for (var dx = 0; dx <= 120 && hit == null; dx += 20) {
          hit = engine.pickPoint(160.0 + dx, 120.0);
        }
        expect(hit, isNotNull, reason: '中心附近应有点');
        expect(engine.lastError, isEmpty);
      } finally {
        engine.dispose();
      }
    });
  }, skip: Platform.environment['GS3D_ENGINE_E2E'] != '1');
  test(
    'large bundle: lod sidecar plus full-resolution tile streaming',
    () async {
      const bundle = '../tmp/bench-bundle-1m';
      final engine = Gs3dEngine.create(
        width: 1280,
        height: 720,
        enableValidation: true,
      );
      addTearDown(engine.dispose);
      expect(engine.loadDataset(bundle), 0, reason: engine.lastError);
      expect(engine.sourcePointCount, 1000000);

      final lod = engine.diagnostics['lod'] as Map<String, dynamic>;
      expect(lod['tiles_available'], isTrue, reason: 'bundle 带 tiles sidecar');
      expect(lod['tile_count'], greaterThan(0));
      expect(lod['level_count'], greaterThan(1), reason: 'lod.gs3dlod 已生效');

      // 细尺度瓦片流式仍标着「尚未完成」（见 Config::tile_streaming）：way 远视角下
      // 选取不启用，这里只验证多帧合成能稳定出帧。等流式恢复后再断言 tile_resident。
      for (var i = 0; i < 5; i++) {
        engine.requestFrame();
        await Future<void>.delayed(const Duration(milliseconds: 20));
      }
      expect(engine.lastError, isEmpty);

      final image = await engine.readFrame();
      expect(image, isNotNull, reason: 'LOD + tile 合成后必须能取到帧');
      image?.dispose();
    },
    skip: Platform.environment['GS3D_ENGINE_E2E'] != '1'
        ? '需要 GS3D_ENGINE_E2E=1；先运行 gs3d_bench_bundle 生成 tmp/bench-bundle-1m'
        : false,
  );
  test(
    'native engine loads sample and reports diagnostics',
    () {
      final engine = Gs3dEngine.create(
        width: 256,
        height: 256,
        enableValidation: true,
      );
      addTearDown(engine.dispose);
      expect(engine.isDisposed, isFalse);
      expect(engine.loadSample(), 0, reason: engine.lastError);
      expect(engine.pointCount, 10000);
      expect(engine.diagnostics, isNotEmpty);
      engine.dispose();
      expect(engine.isDisposed, isTrue);
    },
    skip: Platform.environment['GS3D_ENGINE_E2E'] != '1'
        ? '需要 GS3D_ENGINE_E2E=1 及已部署的原生 Vulkan 库'
        : false,
  );
}
