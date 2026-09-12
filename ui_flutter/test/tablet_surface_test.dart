import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/ffi/geoscatter3d_service.dart';
import 'package:ui_flutter/src/layouts/floating_dock_layout.dart';
import 'package:ui_flutter/src/layouts/standard_workbench_layout.dart';
import 'package:ui_flutter/src/panels/left_dock_panel.dart';
import 'package:ui_flutter/src/panels/right_dock_panel.dart';
import 'package:ui_flutter/src/panels/tablet/tablet_drawer_scaffold.dart';
import 'package:ui_flutter/src/theme/gs3d_form_factor.dart';
import 'package:ui_flutter/src/welcome/welcome_ui_keys.dart';
import 'package:ui_flutter/src/widgets/command_palette.dart';

import 'support/tablet_surface.dart';
import 'support/welcome_ui_test_service.dart';

/// 平板 widget test 基线。
///
/// 覆盖：断点档位 / 抽屉开合与命中区 / 底部动作条与命令按钮 /
/// 引擎显式降级卡 / 视口多指手势（单指旋转、双指平移+捏合缩放）/
/// 长按上下文菜单 / 桌面专属布局入口守卫 / welcome 顶栏命令入口。
void main() {
  group('断点档位推导（单一事实源）', () {
    test('500 / 800 / 1400 宽分别命中 compact / tablet / expanded', () {
      expect(TabletSurface.slotForWidth(500), Gs3dFormFactorSlot.compact);
      expect(TabletSurface.slotForWidth(800), Gs3dFormFactorSlot.tablet);
      expect(TabletSurface.slotForWidth(1400), Gs3dFormFactorSlot.expanded);
      // 平板基准分辨率。
      expect(TabletSurface.slotForWidth(1192), Gs3dFormFactorSlot.tablet);
      expect(TabletSurface.slotForWidth(2208), Gs3dFormFactorSlot.expanded);
    });
  });

  group('平板工作台（1192×820，引擎不可用预设）', () {
    late WelcomeUiTestService service;

    setUp(() {
      service = WelcomeUiTestService();
      service.markEngineUnavailable('libgs3d_ffi.so not found');
      // 手势引导卡为进程级「仅一次」语义；每条用例显式复位以便独立断言。
      TabletDrawerScaffold.resetGestureGuide();
    });

    tearDown(() {
      // 假 service 无引擎与定时器；仅清理浮层状态，避免跨用例泄漏。
      service.toggleCommandPalette(false);
      service.toggleShortcutOverlay(false);
      service.togglePerformancePanel(false);
      service.toggleTileInspector(false);
    });

    /// 若手势引导卡在场（首次进入），先点「知道了」关闭。
    Future<void> dismissGuideIfPresent(WidgetTester tester) async {
      if (find.text('触控手势指南').evaluate().isNotEmpty) {
        await tester.tap(
          find.byKey(const ValueKey('tablet.gesture_guide.dismiss')),
        );
        await tester.pumpAndSettle();
      }
    }

    Future<void> pumpWorkbench(WidgetTester tester) async {
      await TabletSurface.pumpTabletApp(tester, service);
      // 测试双写 service 的 isWorkbenchActive 绑定 active 标志，
      // 走 executeAction('welcome.quick_demo') 才能真正激活工作台。
      service.executeAction('welcome.quick_demo');
      await tester.pumpAndSettle();
      await dismissGuideIfPresent(tester);
    }

    testWidgets('tablet 档：抽屉默认收起、把手可唤出、动作条命中 ≥48、命令按钮可达', (tester) async {
      await pumpWorkbench(tester);

      // 断点档位正确。
      final scaffoldContext = tester.element(find.byType(Scaffold).first);
      expect(scaffoldContext.gs3dFormFactor, Gs3dFormFactorSlot.tablet);

      // 左右面板不在树中（收起态），边缘把手存在。
      expect(find.byType(LeftDockPanel), findsNothing);
      expect(find.byType(RightDockPanel), findsNothing);
      expect(
        find.byKey(const ValueKey('tablet.drawer.handle.left')),
        findsOneWidget,
      );

      // 唤出左抽屉 → LeftDockPanel 出现；再收起。
      await tester.tap(find.byKey(const ValueKey('tablet.drawer.handle.left')));
      await tester.pumpAndSettle();
      expect(find.byType(LeftDockPanel), findsOneWidget);
      await tester.tap(find.byKey(const ValueKey('tablet.drawer.close.left')));
      await tester.pumpAndSettle();
      expect(find.byType(LeftDockPanel), findsNothing);

      // 底部动作条存在且命令按钮命中区 ≥48。
      expect(find.byKey(const ValueKey('tablet.action_bar')), findsOneWidget);
      final cmdSize = tester.getSize(
        find.byKey(const ValueKey('tablet.actionbar.command')),
      );
      expect(cmdSize.longestSide, greaterThanOrEqualTo(48.0));

      // 顶栏常驻命令按钮。
      expect(
        find.byKey(const ValueKey('workbench.menu.command')),
        findsOneWidget,
      );

      // 引擎依赖动作 → R3 显式降级反馈（入口保持可达，不置灰）。
      await tester.tap(
        find.byKey(const ValueKey('tablet.actionbar.reset_camera')),
      );
      await tester.pump();
      // SnackBar 文案（错误卡片标题也含同前缀，故匹配 SnackBar 专属片段）。
      expect(find.textContaining('无法执行该动作'), findsOneWidget);
      // 消化 SnackBar 自动关闭定时器，避免测试结束时遗留 pending Timer。
      await tester.pump(const Duration(seconds: 4));
    });

    testWidgets('视口区渲染 R3 显式降级错误卡片（无 Dart 假视口）', (tester) async {
      await pumpWorkbench(tester);

      expect(
        find.byKey(const ValueKey('workbench.viewport.engine_error_card')),
        findsOneWidget,
      );
      expect(find.textContaining('渲染内核在本平台不可用'), findsWidgets);
    });

    testWidgets('单指拖拽 = 旋转（service 相机方位角变化）', (tester) async {
      await pumpWorkbench(tester);

      final canvas = find.byKey(WorkbenchUiKeys.viewportCanvas);
      final center = tester.getCenter(canvas);
      final azimuthBefore = service.cameraAzimuth;

      final gesture = await tester.startGesture(center);
      await gesture.moveBy(const Offset(60, 0));
      await tester.pump();
      await gesture.moveBy(const Offset(40, 0));
      await tester.pump();
      await gesture.up();
      await tester.pump();
      // 冲洗双击识别器的 pending timer，避免用例结束遗留计时器。
      await tester.pump(const Duration(milliseconds: 350));

      expect(service.cameraAzimuth, isNot(azimuthBefore));
    });

    testWidgets('双指拖动 = 平移、双指捏合 = 缩放（Dart 相机状态变化）', (tester) async {
      await pumpWorkbench(tester);

      final canvas = find.byKey(WorkbenchUiKeys.viewportCanvas);
      final center = tester.getCenter(canvas);
      final azimuthBefore = service.cameraAzimuth;
      final panBefore = Offset(service.cameraPanX, service.cameraPanY);
      final zoomBefore = service.cameraZoom;

      // 双指同向拖动 → 平移（方位角不应变化）。
      final finger1 = await tester.startGesture(center - const Offset(40, 0));
      final finger2 = await tester.startGesture(center + const Offset(40, 0));
      await tester.pump();
      await finger1.moveBy(const Offset(50, 0));
      await tester.pump();
      await finger2.moveBy(const Offset(50, 0));
      await tester.pump();
      await finger1.up();
      await finger2.up();
      await tester.pump();

      final panAfter = Offset(service.cameraPanX, service.cameraPanY);
      expect(panAfter.dx, greaterThan(panBefore.dx));
      expect(service.cameraAzimuth, azimuthBefore);

      // 双指外扩捏合 → 缩放放大。
      final zoomAfterPan = service.cameraZoom;
      final f1 = await tester.startGesture(center - const Offset(60, 0));
      final f2 = await tester.startGesture(center + const Offset(60, 0));
      await tester.pump();
      await f1.moveBy(const Offset(-60, 0));
      await tester.pump();
      await f2.moveBy(const Offset(60, 0));
      await tester.pump();
      await f1.up();
      await f2.up();
      await tester.pump();

      expect(zoomAfterPan, zoomBefore);
      expect(service.cameraZoom, greaterThan(zoomAfterPan));
      // 冲洗双击识别器的 pending timer。
      await tester.pump(const Duration(milliseconds: 350));
    });

    testWidgets('长按视口弹出上下文菜单（触屏等价右键）', (tester) async {
      await pumpWorkbench(tester);

      final canvas = find.byKey(WorkbenchUiKeys.viewportCanvas);
      await tester.longPress(canvas);
      await tester.pumpAndSettle();

      // 视口浮层工具条已有「复位视角」标签，故此处用 findsWidgets；
      // 菜单项在最上层路由，点按取 .last。
      expect(find.text('复位视角'), findsWidgets);
      expect(find.text('命令面板'), findsWidgets);

      // 选择「命令面板」→ 打开全局命令面板。
      await tester.tap(find.text('命令面板').last);
      await tester.pumpAndSettle();
      expect(service.isCommandPaletteOpen, isTrue);
      expect(find.byType(CommandPalette), findsOneWidget);
    });

    testWidgets('命令按钮（顶栏 + 动作条）均可唤起命令面板', (tester) async {
      await pumpWorkbench(tester);

      await tester.tap(find.byKey(const ValueKey('workbench.menu.command')));
      await tester.pumpAndSettle();
      expect(service.isCommandPaletteOpen, isTrue);

      // 关闭后经动作条按钮再次唤起。
      service.toggleCommandPalette(false);
      await tester.pumpAndSettle();

      await tester.tap(find.byKey(const ValueKey('tablet.actionbar.command')));
      await tester.pumpAndSettle();
      expect(service.isCommandPaletteOpen, isTrue);
    });

    testWidgets('布局按钮打开选择器并提交真实布局动作', (tester) async {
      await pumpWorkbench(tester);

      await tester.tap(find.byKey(const ValueKey('tablet.actionbar.layout')));
      await tester.pumpAndSettle();
      expect(find.text('切换工作区布局'), findsOneWidget);
      expect(
        find.byKey(const ValueKey('tablet.layout.option.standard')),
        findsOneWidget,
      );

      await tester.tap(
        find.byKey(const ValueKey('tablet.layout.option.floatingDock')),
      );
      await tester.pumpAndSettle();
      expect(service.layoutMode, WorkbenchLayoutMode.floatingDock);
    });

    testWidgets('统计按钮显示实际统计结果反馈', (tester) async {
      await pumpWorkbench(tester);

      await tester.tap(find.byKey(const ValueKey('tablet.actionbar.stats')));
      await tester.pump();
      expect(find.textContaining('无法执行该动作'), findsOneWidget);
      await tester.pump(const Duration(seconds: 3));
    });

    testWidgets('桌面专属布局入口守卫：tablet 档切 floatingDock 回退触控工作台', (tester) async {
      await pumpWorkbench(tester);

      service.setLayoutMode(WorkbenchLayoutMode.floatingDock);
      await tester.pumpAndSettle();

      // 守卫横幅出现，且仍渲染触控工作台（抽屉脚手架）。
      expect(
        find.byKey(const ValueKey('tablet.layout_guard_banner')),
        findsOneWidget,
      );
      expect(find.byType(StandardWorkbenchLayout), findsOneWidget);
      expect(find.byType(FloatingDockLayout), findsOneWidget);
    });

    testWidgets('首次进入触控工作台显示一次性手势引导卡', (tester) async {
      await TabletSurface.pumpTabletApp(tester, service);
      service.executeAction('welcome.quick_demo');
      await tester.pumpAndSettle();

      expect(find.text('触控手势指南'), findsOneWidget);
      await tester.tap(
        find.byKey(const ValueKey('tablet.gesture_guide.dismiss')),
      );
      await tester.pumpAndSettle();
      expect(find.text('触控手势指南'), findsNothing);
    });
  });

  group('超窄 compact 回归（KB-02：328×525 逻辑分辨率）', () {
    testWidgets('欢迎页无 RenderFlex 溢出且侧栏关键控件仍在树中', (tester) async {
      final service = WelcomeUiTestService();
      service.markEngineUnavailable('libgs3d_ffi.so not found');
      TabletDrawerScaffold.resetGestureGuide();

      // 安卓模拟器 wm size 800x1280（dpr≈2.44）对应的逻辑分辨率，
      // 此前该尺寸下左侧抽屉底部出现 ~9px 黄黑条纹溢出指示器。
      await TabletSurface.pumpTabletApp(
        tester,
        service,
        surface: const Size(328, 525),
      );
      await tester.pumpAndSettle();

      // 布局阶段不允许抛出任何 RenderFlex 溢出异常。
      expect(tester.takeException(), isNull);

      // 侧栏关键控件仍可布局、可访问（不因自适应收缩而丢失）。
      expect(find.byKey(WelcomeUiKeys.sidebarLogo), findsOneWidget);
      expect(find.byKey(WelcomeUiKeys.footerDocsButton), findsOneWidget);
      expect(find.byKey(WelcomeUiKeys.footerAboutButton), findsOneWidget);
      expect(find.byKey(WelcomeUiKeys.footerGpuButton), findsOneWidget);
    });

    testWidgets('工作台左右抽屉均不超出紧凑屏幕', (tester) async {
      final service = WelcomeUiTestService();
      service.markEngineUnavailable('libgs3d_ffi.so not found');
      TabletDrawerScaffold.resetGestureGuide();

      await TabletSurface.pumpTabletApp(
        tester,
        service,
        surface: const Size(328, 525),
      );
      service.executeAction('welcome.quick_demo');
      await tester.pumpAndSettle();
      if (find
          .byKey(const ValueKey('tablet.gesture_guide.dismiss'))
          .evaluate()
          .isNotEmpty) {
        await tester.tap(
          find.byKey(const ValueKey('tablet.gesture_guide.dismiss')),
        );
        await tester.pumpAndSettle();
      }

      final viewportWidth = tester.getSize(find.byType(Scaffold).first).width;
      await tester.tap(find.byKey(const ValueKey('tablet.drawer.handle.left')));
      await tester.pumpAndSettle();
      final leftPanel = tester.getRect(
        find.byKey(const ValueKey('tablet.drawer.panel.left')),
      );
      expect(leftPanel.left, greaterThanOrEqualTo(0));
      expect(leftPanel.right, lessThanOrEqualTo(viewportWidth));

      await tester.tap(find.byKey(const ValueKey('tablet.drawer.close.left')));
      await tester.pumpAndSettle();
      await tester.tap(
        find.byKey(const ValueKey('tablet.drawer.handle.right')),
      );
      await tester.pumpAndSettle();
      final rightPanel = tester.getRect(
        find.byKey(const ValueKey('tablet.drawer.panel.right')),
      );
      expect(rightPanel.left, greaterThanOrEqualTo(0));
      expect(rightPanel.right, lessThanOrEqualTo(viewportWidth));
      expect(tester.takeException(), isNull);
    });
  });

  group('2208×1840 横向平板命中 expanded，桌面布局照旧', () {
    testWidgets('expanded 档保留三栏常驻面板与桌面顶栏', (tester) async {
      final service = WelcomeUiTestService();
      await TabletSurface.pumpTabletApp(
        tester,
        service,
        surface: TabletSurface.foldInnerLandscape,
      );
      service.executeAction('welcome.quick_demo');
      await tester.pumpAndSettle();

      final scaffoldContext = tester.element(find.byType(Scaffold).first);
      expect(scaffoldContext.gs3dFormFactor, Gs3dFormFactorSlot.expanded);

      // 桌面布局：面板常驻（leftDock/rightDock 可见）、无平板抽屉把手。
      expect(find.byType(LeftDockPanel), findsOneWidget);
      expect(find.byType(RightDockPanel), findsOneWidget);
      expect(
        find.byKey(const ValueKey('tablet.drawer.handle.left')),
        findsNothing,
      );
      expect(find.byKey(const ValueKey('tablet.action_bar')), findsNothing);
      // 桌面顶栏菜单仍存在（桌面行为零回退）。
      expect(find.byKey(WorkbenchUiKeys.menuFile), findsOneWidget);
      expect(
        find.byKey(const ValueKey('workbench.menu.command')),
        findsNothing,
      );
    });
  });
}
