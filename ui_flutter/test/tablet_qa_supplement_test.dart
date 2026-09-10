import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/ffi/geoscatter3d_service.dart';
import 'package:ui_flutter/src/panels/tablet/tablet_drawer_scaffold.dart';
import 'package:ui_flutter/src/theme/gs3d_form_factor.dart';
import 'package:ui_flutter/src/widgets/fluid.dart';
import 'package:ui_flutter/src/welcome/welcome_ui_keys.dart';

import 'support/tablet_surface.dart';
import 'support/welcome_ui_test_service.dart';

/// QA 补强专项（平板适配迭代）。
///
/// 覆盖缺口（相对 tablet_surface_test.dart / action_coverage_test.dart）：
///   A. 断点三档边界值（599/600/1279/1280/1281）；
///   B. 触控目标 ≥48 抽查（动作条全部按钮 / 抽屉关闭钮 / 顶栏命令钮 /
///      视口浮层工具条——后者为 KB-01 已知问题记录，见用例注释）；
///   C. 真实 service 的 DynamicLibrary.open 失败路径（非假 service 预设）；
///   D. 多指手势状态机边界（第三指按下 / 双指抬一指接续旋转 / 状态复位）；
///   E. Gs3dResponsiveDialog 档位路由（tablet → 底部 sheet / expanded → 居中模态）。
void main() {
  group('A. 断点三档边界值（单一事实源）', () {
    test('599 → compact；600 → tablet；1279 → tablet；1280/1281 → expanded',
        () {
      expect(TabletSurface.slotForWidth(599), Gs3dFormFactorSlot.compact,
          reason: '600 为 compact 上界（不含）');
      expect(TabletSurface.slotForWidth(600), Gs3dFormFactorSlot.tablet,
          reason: '600 恰好命中 tablet 下界（含）');
      expect(TabletSurface.slotForWidth(1279), Gs3dFormFactorSlot.tablet,
          reason: '1280 为 tablet 上界（不含）');
      expect(TabletSurface.slotForWidth(1280), Gs3dFormFactorSlot.expanded,
          reason: '1280 恰好命中 expanded 下界（含）');
      expect(TabletSurface.slotForWidth(1281), Gs3dFormFactorSlot.expanded);
    });
  });

  group('B. 触控目标 ≥48 抽查（tablet 档 1192×820）', () {
    late WelcomeUiTestService service;

    setUp(() {
      service = WelcomeUiTestService();
      service.markEngineUnavailable('libgs3d_ffi.so not found');
      TabletDrawerScaffold.resetGestureGuide();
    });

    Future<void> pumpWorkbench(WidgetTester tester) async {
      await TabletSurface.pumpTabletApp(tester, service);
      service.executeAction('welcome.quick_demo');
      await tester.pumpAndSettle();
      if (find.text('触控手势指南').evaluate().isNotEmpty) {
        await tester.tap(
          find.byKey(const ValueKey('tablet.gesture_guide.dismiss')),
        );
        await tester.pumpAndSettle();
      }
    }

    /// 断言命中区宽高均 ≥48（Material 下限 ∩ Apple HIG 44pt）。
    void expectHitTarget(WidgetTester tester, Key key, String label) {
      final size = tester.getSize(find.byKey(key));
      expect(size.width, greaterThanOrEqualTo(48.0),
          reason: '$label 命中区宽度 ${size.width} < 48');
      expect(size.height, greaterThanOrEqualTo(48.0),
          reason: '$label 命中区高度 ${size.height} < 48');
    }

    testWidgets('底部动作条全部按钮命中区 ≥48', (tester) async {
      await pumpWorkbench(tester);

      const barKeys = [
        'tablet.actionbar.reset_camera',
        'tablet.actionbar.zoom_in',
        'tablet.actionbar.zoom_out',
        'tablet.actionbar.pan_toggle',
        'tablet.actionbar.measure_mode',
        'tablet.actionbar.stats',
        'tablet.actionbar.theme_cycle',
        'tablet.actionbar.screenshot',
        'tablet.actionbar.layout',
        'tablet.actionbar.command',
        'tablet.actionbar.shortcuts',
      ];
      for (final k in barKeys) {
        expectHitTarget(
          tester,
          ValueKey<String>(k),
          '动作条按钮 $k',
        );
      }
    });

    testWidgets('抽屉关闭按钮命中区 ≥48（唤出左抽屉后量测）', (tester) async {
      await pumpWorkbench(tester);

      await tester.tap(
        find.byKey(const ValueKey('tablet.drawer.handle.left')),
      );
      await tester.pumpAndSettle();
      expectHitTarget(
        tester,
        const ValueKey('tablet.drawer.close.left'),
        '左抽屉关闭按钮',
      );
    });

    testWidgets('顶栏常驻命令按钮命中区 ≥48', (tester) async {
      await pumpWorkbench(tester);
      expectHitTarget(
        tester,
        const ValueKey('workbench.menu.command'),
        '顶栏命令按钮',
      );
    });

    // ── KB-01 修复验证（原「已知问题记录」断言已按总监裁决口径翻转） ────────
    // 视口浮层工具条（center_viewport.dart 的 _buildPillButton / _buildIconPill /
    // _buildPillToggleButton / 预设视角下拉）与右下角 Gizmo 预设标签
    // （_buildGizmoPresetTag）在触控档经 Gs3dTouchTarget 撑出 ≥48 命中区，
    // 视觉尺寸不变；expanded 档保持现状 28（桌面键鼠零变化）。
    testWidgets('KB-01 修复验证：视口浮层工具条按钮命中区 ≥48', (tester) async {
      await pumpWorkbench(tester);

      final resetSize = tester.getSize(
        find.byKey(WorkbenchUiKeys.viewportResetButton),
      );
      expect(resetSize.width, greaterThanOrEqualTo(48.0),
          reason: '工具条按钮命中区宽度 ${resetSize.width} < 48');
      expect(resetSize.height, greaterThanOrEqualTo(48.0),
          reason: '工具条按钮命中区高度 ${resetSize.height} < 48');

      final zoomInSize = tester.getSize(
        find.byKey(WorkbenchUiKeys.viewportZoomInButton),
      );
      expect(zoomInSize.width, greaterThanOrEqualTo(48.0));
      expect(zoomInSize.height, greaterThanOrEqualTo(48.0));
    });

    testWidgets('KB-01 修复验证：Gizmo 预设标签命中区 ≥48', (tester) async {
      await pumpWorkbench(tester);

      // 预设标签文案为单字（顶/前/侧/等），全树唯一；量测其外包的
      // Gs3dTouchTarget（ConstrainedBox ≥48，视觉容器尺寸不变）。
      final tagTouchTarget = find.ancestor(
        of: find.text('顶'),
        matching: find.byType(Gs3dTouchTarget),
      );
      expect(tagTouchTarget, findsOneWidget);
      final tagSize = tester.getSize(tagTouchTarget);
      expect(tagSize.width, greaterThanOrEqualTo(48.0),
          reason: 'Gizmo 预设标签命中区宽度 ${tagSize.width} < 48');
      expect(tagSize.height, greaterThanOrEqualTo(48.0),
          reason: 'Gizmo 预设标签命中区高度 ${tagSize.height} < 48');
    });
  });

  group('C. 引擎加载失败显式降级（真实 service 路径）', () {
    test('DynamicLibrary.open 失败 → engineAvailability=unavailable + R3 guard',
        () {
      final service = GeoScatter3dService.detached();
      // 传入存在但非法的文件路径，强制 DynamicLibrary.open 抛异常，
      // 等价 Android 上 libgs3d_ffi.so 缺失的失败路径（R3 验证不依赖真机）。
      final ok = service.initialize('pubspec.yaml');

      expect(ok, isFalse);
      expect(service.engineAvailability, EngineAvailability.unavailable);
      expect(service.initializationError, isNotNull);
      expect(service.backendStatus, contains('渲染内核在本平台不可用'));
      // R3：绝无 Dart 假视口——无引擎时不产生任何原生帧数据。
      expect(service.nativeFrameImage, isNull);
      expect(service.isNativeRendererActive, isFalse);

      // 引擎依赖动作：前置 guard 显式报错（不静默、不假成功）。
      final camera = service.executeAction('workbench.camera.reset');
      expect(camera['success'], isFalse);
      expect(camera['message'], contains('渲染内核在本平台不可用'));

      // 全部引擎依赖前缀均被 guard 拦截（逐一探测）。
      for (final prefix
          in GeoScatter3dService.engineDependentActionPrefixes) {
        final res = service.executeAction('$prefix.qa_probe');
        expect(res['success'], isFalse,
            reason: '引擎依赖前缀 $prefix 未被 R3 guard 拦截');
        expect(res['message'] as String, contains('渲染内核在本平台不可用'),
            reason: '引擎依赖前缀 $prefix 缺少 R3 显式降级文案');
      }

      // 非引擎依赖动作不受 guard 影响（命令面板照常可达）。
      final palette = service.executeAction(
        'workbench.overlay.command_palette',
        {'open': true},
      );
      expect(palette['success'], isTrue);

      service.shutdown();
    });
  });

  group('D. 视口多指手势状态机边界', () {
    late WelcomeUiTestService service;

    setUp(() {
      service = WelcomeUiTestService();
      service.markEngineUnavailable('libgs3d_ffi.so not found');
      TabletDrawerScaffold.resetGestureGuide();
    });

    Future<void> pumpWorkbench(WidgetTester tester) async {
      await TabletSurface.pumpTabletApp(tester, service);
      service.executeAction('welcome.quick_demo');
      await tester.pumpAndSettle();
      if (find.text('触控手势指南').evaluate().isNotEmpty) {
        await tester.tap(
          find.byKey(const ValueKey('tablet.gesture_guide.dismiss')),
        );
        await tester.pumpAndSettle();
      }
    }

    testWidgets('第三指按下不崩溃，手势继续可用且状态复位', (tester) async {
      await pumpWorkbench(tester);

      final canvas = find.byKey(WorkbenchUiKeys.viewportCanvas);
      final center = tester.getCenter(canvas);

      // 双指落地后按下第三指：不应抛异常、不应崩溃。
      final f1 = await tester.startGesture(center - const Offset(40, 0));
      final f2 = await tester.startGesture(center + const Offset(40, 0));
      await tester.pump();
      final f3 = await tester.startGesture(center);
      await tester.pump();
      expect(tester.takeException(), isNull, reason: '第三指按下不应崩溃');

      // 三指在场时移动仍安全（不产生旋转/平移脏增量导致的崩溃）。
      await f1.moveBy(const Offset(20, 0));
      await tester.pump();
      expect(tester.takeException(), isNull);

      // 全部抬指 → 状态复位；随后单指拖拽应恢复旋转（_dragPointer 重新绑定）。
      await f1.up();
      await f2.up();
      await f3.up();
      await tester.pump();

      final azimuthBefore = service.cameraAzimuth;
      final g = await tester.startGesture(center);
      await g.moveBy(const Offset(60, 0));
      await tester.pump();
      await g.up();
      await tester.pump(const Duration(milliseconds: 350));
      expect(service.cameraAzimuth, isNot(azimuthBefore),
          reason: '多指结束后单指拖拽应恢复为旋转（状态正确复位）');
    });

    testWidgets('双指抬一指 → 剩余一指接续为单指旋转（手势不断流）', (tester) async {
      await pumpWorkbench(tester);

      final canvas = find.byKey(WorkbenchUiKeys.viewportCanvas);
      final center = tester.getCenter(canvas);

      final f1 = await tester.startGesture(center - const Offset(40, 0));
      final f2 = await tester.startGesture(center + const Offset(40, 0));
      await tester.pump();

      // 先做一段双指平移。
      await f1.moveBy(const Offset(30, 0));
      await tester.pump();
      await f2.moveBy(const Offset(30, 0));
      await tester.pump();

      // 抬起第一指，剩余 f2 应接续为旋转。
      await f1.up();
      await tester.pump();
      expect(tester.takeException(), isNull);

      final elevationBefore = service.cameraElevation;
      await f2.moveBy(const Offset(0, 50));
      await tester.pump();
      await f2.up();
      await tester.pump(const Duration(milliseconds: 350));

      expect(tester.takeException(), isNull);
      expect(service.cameraElevation, isNot(elevationBefore),
          reason: '抬一指后剩余手指应接续单指旋转（仰角变化）');
    });
  });

  group('E. 响应式对话框档位路由', () {
    testWidgets('tablet 档（1192×820）对话框落底部 sheet', (tester) async {
      final service = WelcomeUiTestService();
      service.markEngineUnavailable('libgs3d_ffi.so not found');
      await TabletSurface.pumpTabletApp(tester, service);
      service.executeAction('welcome.quick_demo');
      await tester.pumpAndSettle();

      final context = tester.element(find.byType(Scaffold).first);
      Gs3dResponsiveDialog.show<void>(
        context,
        title: 'QA 对话框',
        builder: (_) => const SizedBox(
          height: 160,
          child: Center(child: Text('QA-SHEET-CONTENT')),
        ),
      );
      await tester.pumpAndSettle();

      expect(find.byType(BottomSheet), findsOneWidget,
          reason: 'tablet 档对话框应为底部 sheet（拇指可达）');
      expect(find.text('QA-SHEET-CONTENT'), findsOneWidget);
      expect(find.byType(Dialog), findsNothing, reason: '不应落居中模态');
    });

    testWidgets('expanded 档（2208×1840）对话框保持居中模态', (tester) async {
      final service = WelcomeUiTestService();
      await TabletSurface.pumpTabletApp(
        tester,
        service,
        surface: TabletSurface.foldInnerLandscape,
      );
      service.executeAction('welcome.quick_demo');
      await tester.pumpAndSettle();

      final context = tester.element(find.byType(Scaffold).first);
      Gs3dResponsiveDialog.show<void>(
        context,
        title: 'QA 对话框',
        builder: (_) => const SizedBox(
          height: 160,
          child: Center(child: Text('QA-DIALOG-CONTENT')),
        ),
      );
      await tester.pumpAndSettle();

      // 该 Flutter 版本的 showDialog 路由不经过 Dialog 类型 widget，
      // 以「内容出现 + 无底部 sheet」断言居中模态（探针已验证）。
      expect(find.text('QA-DIALOG-CONTENT'), findsOneWidget,
          reason: 'expanded 档对话框内容弹出');
      expect(find.byType(BottomSheet), findsNothing,
          reason: 'expanded 档不应落底部 sheet（桌面交互零变化）');
    });
  });
}
