import 'dart:ui' as ui;
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/ffi/geoscatter3d_service.dart';
import 'package:ui_flutter/src/widgets/viewport_canvas.dart';
import 'support/fake_engine.dart';
import 'support/fake_present_surface.dart';

void main() {
  testWidgets(
    'displayed frame survives service replacement until widget rebuild',
    (tester) async {
      final recorder = ui.PictureRecorder();
      ui.Canvas(recorder);
      final picture = recorder.endRecording();
      final source = picture.toImageSync(2, 2);
      picture.dispose();
      final engine = FakeEngine()..render = () async => source.clone();
      final service = GeoScatter3dService(engine: engine);
      addTearDown(() {
        service.dispose();
        source.dispose();
      });
      await service.initialize();
      await service.renderNextFrame();
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(body: ViewportCanvas(service: service)),
        ),
      );
      final displayed = tester
          .widget<RawImage>(find.byKey(const ValueKey('viewport_frame')))
          .image!;
      await service.renderNextFrame();
      // 服务旧帧已经释放，显示端独立引用仍必须可用。
      final retained = displayed.clone();
      retained.dispose();
      await tester.pump();
      expect(tester.takeException(), isNull);
      await tester.pumpWidget(const SizedBox.shrink());
    },
  );
  testWidgets('纹理尺寸按设备 dpr 折算成物理像素（与屏幕 1:1）', (tester) async {
    // 默认测试视口是 2400×1800 物理 / dpr 3（逻辑 800×600）。
    tester.view.devicePixelRatio = 2.0;
    addTearDown(tester.view.reset);
    final engine = FakeEngine();
    final binder = FakePresentSurfaceBinder();
    final service = GeoScatter3dService(engine: engine, presentBinder: binder);
    addTearDown(service.dispose);
    await service.initialize();
    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(body: ViewportCanvas(service: service)),
      ),
    );
    for (var i = 0; i < 4 && !service.zeroCopyPresent; i++) {
      await tester.pump();
    }
    expect(service.zeroCopyPresent, isTrue);
    // 逻辑 1200×900 × dpr 2 = 物理 2400×1800：与屏幕像素一一对应。
    expect(binder.attachSizes.single, (2400, 1800));
    expect(tester.takeException(), isNull);
    await tester.pumpWidget(const SizedBox.shrink());
  });

  testWidgets(
    'native startup failure displays its cause, without a fake viewport',
    (tester) async {
      final service = GeoScatter3dService(
        createEngine: () async =>
            throw StateError('Vulkan unavailable fixture'),
      );
      addTearDown(service.dispose);
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(body: ViewportCanvas(service: service)),
        ),
      );
      await tester.pump();
      expect(find.byKey(const ValueKey('vulkan_engine_error')), findsOneWidget);
      expect(find.textContaining('Vulkan unavailable fixture'), findsOneWidget);
      expect(find.byKey(const ValueKey('viewport_frame')), findsNothing);
      expect(find.byType(CircularProgressIndicator), findsNothing);
      await tester.pumpWidget(const SizedBox.shrink());
    },
  );

  testWidgets(
    'texture path renders a Texture layer instead of a RawImage frame',
    (tester) async {
      final engine = FakeEngine();
      final binder = FakePresentSurfaceBinder();
      final service = GeoScatter3dService(
        engine: engine,
        presentBinder: binder,
      );
      addTearDown(service.dispose);
      await service.initialize();
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(body: ViewportCanvas(service: service)),
        ),
      );
      // 首帧 build 提交视口尺寸 → 下一 tick 接管纹理（两条 pump 覆盖 ticker）。
      for (var i = 0; i < 4 && !service.zeroCopyPresent; i++) {
        await tester.pump();
      }
      expect(service.zeroCopyPresent, isTrue);
      await tester.pump();
      expect(find.byKey(const ValueKey('viewport_texture')), findsOneWidget);
      expect(find.byKey(const ValueKey('viewport_frame')), findsNothing);
      expect(tester.takeException(), isNull);
      await tester.pumpWidget(const SizedBox.shrink());
    },
  );

  testWidgets('viewport alone owns gestures and no floating chrome', (
    tester,
  ) async {
    final service = GeoScatter3dService(engine: FakeEngine());
    addTearDown(service.dispose);
    await service.initialize();

    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(body: ViewportCanvas(service: service)),
      ),
    );

    expect(find.byKey(const ValueKey('viewport_gestures')), findsOneWidget);
    expect(find.text('GeoScatter 3D'), findsNothing);
    expect(tester.takeException(), isNull);
    await tester.pumpWidget(const SizedBox.shrink());
  });
}
