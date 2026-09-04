import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/main.dart';
import 'package:ui_flutter/src/ffi/geoscatter3d_service.dart';

void main() {
  testWidgets('GeoScatter3dApp WelcomePage and Workbench routing test', (WidgetTester tester) async {
    final service = GeoScatter3dService();

    // 桌面标准测试分辨率 1920x1080
    tester.view.physicalSize = const Size(1920, 1080);
    tester.view.devicePixelRatio = 1.0;
    addTearDown(() {
      tester.view.resetPhysicalSize();
      tester.view.resetDevicePixelRatio();
    });

    await tester.pumpWidget(GeoScatter3dApp(service: service));
    await tester.pump(const Duration(milliseconds: 300));

    // 1. 验证欢迎页主视图正确呈现
    expect(find.text('新建工程'), findsOneWidget);
    expect(find.text('打开工程'), findsOneWidget);
    expect(find.text('快速体验示例'), findsOneWidget);
    expect(find.text('最近打开的项目'), findsOneWidget);
    expect(find.textContaining('Vulkan 1.3 / GS3D v2 核心引擎就绪'), findsOneWidget);

    // 2. 交互测试：点击“新建工程”卡片弹出向导弹窗
    await tester.tap(find.text('新建工程'));
    await tester.pump(const Duration(milliseconds: 300));

    expect(find.text('新建三维散点工程'), findsOneWidget);
    expect(find.text('构建并载入工作台'), findsOneWidget);

    // 关闭向导弹窗
    await tester.tap(find.byIcon(Icons.close));
    await tester.pump(const Duration(milliseconds: 300));
    expect(find.text('新建三维散点工程'), findsNothing);

    // 3. 交互测试：点击“打开工程”卡片弹出快速载入弹窗
    await tester.tap(find.text('打开工程'));
    await tester.pump(const Duration(milliseconds: 300));

    expect(find.text('打开工程数据包'), findsOneWidget);
    expect(find.text('立即载入'), findsOneWidget);

    // 关闭弹窗
    await tester.tap(find.text('取消'));
    await tester.pump(const Duration(milliseconds: 300));
    expect(find.text('打开工程数据包'), findsNothing);

    // 4. 路由切换：进入工作台并验证面板
    service.openWorkbench();
    await tester.pump(const Duration(milliseconds: 350));

    expect(find.text('项目'), findsOneWidget);
    expect(find.text('测量'), findsOneWidget);
    expect(find.text('区域统计'), findsOneWidget);
    expect(find.text('场景'), findsOneWidget);
    expect(find.text('细节层级'), findsOneWidget);
    expect(find.text('导航图'), findsOneWidget);
    expect(find.text('欢迎页'), findsOneWidget);

    // 5. 交互测试：在工作台顶部菜单点击“欢迎页”按钮返回欢迎页
    await tester.tap(find.text('欢迎页'));
    await tester.pump(const Duration(milliseconds: 350));

    expect(find.text('快速体验示例'), findsOneWidget);
    expect(find.text('新建工程'), findsOneWidget);
  });

  testWidgets('Workbench Layout switcher test (Standard, FloatingDock, AnalysisRail)', (WidgetTester tester) async {
    final service = GeoScatter3dService();
    tester.view.physicalSize = const Size(1920, 1080);
    tester.view.devicePixelRatio = 1.0;
    addTearDown(() {
      tester.view.resetPhysicalSize();
      tester.view.resetDevicePixelRatio();
    });

    service.openWorkbench();
    await tester.pumpWidget(GeoScatter3dApp(service: service));
    await tester.pump(const Duration(milliseconds: 300));

    // 1. 验证默认是标准工作台
    expect(service.layoutMode, WorkbenchLayoutMode.standard);
    expect(find.byKey(const ValueKey('layout_standard')), findsOneWidget);

    // 2. 切换到悬浮胶囊 Dock 布局
    service.setLayoutMode(WorkbenchLayoutMode.floatingDock);
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('layout_floating_dock')), findsOneWidget);

    // 点击悬浮胶囊 Dock 的“工区数据集”图标呼出浮动卡片
    await tester.tap(find.byTooltip('工区数据集'));
    await tester.pumpAndSettle();
    expect(find.text('工区名称'), findsOneWidget);

    // 3. 切换到暗色分析舱布局
    service.setLayoutMode(WorkbenchLayoutMode.analysisRail);
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('layout_analysis_rail')), findsOneWidget);

    // 4. 切回标准工作台
    service.setLayoutMode(WorkbenchLayoutMode.standard);
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('layout_standard')), findsOneWidget);
  });
}

