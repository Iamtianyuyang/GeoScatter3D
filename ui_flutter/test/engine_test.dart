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
      } finally {
        engine.dispose();
      }
    });
  }, skip: Platform.environment['GS3D_ENGINE_E2E'] != '1');
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
