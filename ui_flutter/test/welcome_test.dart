import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/ffi/geoscatter3d_service.dart';
import 'package:ui_flutter/src/welcome/gpu_selection_dialog.dart';
import 'package:ui_flutter/src/welcome/recent_projects_card.dart';
import 'package:ui_flutter/src/welcome/welcome_action_cards.dart';
import 'package:ui_flutter/src/welcome/welcome_footer.dart';
import 'package:ui_flutter/src/welcome/welcome_hero.dart';

void main() {
  setUp(() {
    final service = GeoScatter3dService();
    service.initialize();
  });

  testWidgets('WelcomeHero widget renders correctly', (WidgetTester tester) async {
    tester.view.physicalSize = const Size(1280, 720);
    tester.view.devicePixelRatio = 1.0;
    addTearDown(() {
      tester.view.resetPhysicalSize();
      tester.view.resetDevicePixelRatio();
    });

    await tester.pumpWidget(
      const MaterialApp(
        home: Scaffold(body: WelcomeHero()),
      ),
    );
    await tester.pump(const Duration(milliseconds: 100));

    expect(find.text('G'), findsOneWidget);
    expect(find.textContaining('GeoScatter', findRichText: true), findsOneWidget);
    expect(find.textContaining('3D', findRichText: true), findsOneWidget);
    expect(find.textContaining('大规模地理空间与地质勘探可视化引擎'), findsOneWidget);

    // 释放 widget 销毁持续旋转的动画控制器
    await tester.pumpWidget(const SizedBox());
    await tester.pump();
  });

  testWidgets('WelcomeActionCards renders and triggers Quick Demo', (WidgetTester tester) async {
    final service = GeoScatter3dService();

    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(body: WelcomeActionCards(service: service)),
      ),
    );
    await tester.pump();

    expect(find.text('新建工程'), findsOneWidget);
    expect(find.text('打开工程'), findsOneWidget);
    expect(find.text('快速体验示例'), findsOneWidget);

    // 点击快速体验示例
    await tester.tap(find.text('快速体验示例'));
    await tester.pump(const Duration(milliseconds: 300));

    // 验证数据集已载入且工作台被激活
    expect(service.isWorkbenchActive, isTrue);
    expect(service.summary.isLoaded, isTrue);
    expect(service.summary.pointCount, equals(25));
  });

  testWidgets('RecentProjectsCard displays and can clear', (WidgetTester tester) async {
    final service = GeoScatter3dService();

    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(body: RecentProjectsCard(service: service)),
      ),
    );
    await tester.pump();

    expect(find.text('最近打开的项目'), findsOneWidget);

    // 如果有清空记录按钮，测试弹窗
    final clearBtn = find.text('清空记录');
    if (clearBtn.evaluate().isNotEmpty) {
      await tester.tap(clearBtn);
      await tester.pump(const Duration(milliseconds: 200));
      expect(find.text('清空最近记录'), findsOneWidget);
      await tester.tap(find.text('确认清空'));
      await tester.pump(const Duration(milliseconds: 200));
      expect(find.text('暂无最近打开的项目记录'), findsOneWidget);
    }
  });

  testWidgets('GpuSelectionDialog allows selecting active GPU', (WidgetTester tester) async {
    final service = GeoScatter3dService();

    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(body: GpuSelectionDialog(service: service)),
      ),
    );
    await tester.pump();

    expect(find.text('选择图形渲染硬件 (Vulkan GPU)'), findsOneWidget);
    expect(find.textContaining('GeForce RTX'), findsOneWidget);

    // 切换到 Intel GPU
    if (find.textContaining('Intel').evaluate().isNotEmpty) {
      await tester.tap(find.textContaining('Intel').first);
      await tester.pump();
      await tester.tap(find.text('应用并保存设置'));
      await tester.pump(const Duration(milliseconds: 200));
      expect(service.activeGpuIndex, equals(1));
    }
  });

  testWidgets('WelcomeFooter shows GPU chip and About dialog', (WidgetTester tester) async {
    final service = GeoScatter3dService();

    tester.view.physicalSize = const Size(1280, 720);
    tester.view.devicePixelRatio = 1.0;
    addTearDown(() {
      tester.view.resetPhysicalSize();
      tester.view.resetDevicePixelRatio();
    });

    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          bottomNavigationBar: WelcomeFooter(service: service),
        ),
      ),
    );
    await tester.pump();

    expect(find.textContaining('设备:'), findsOneWidget);
    expect(find.text('关于系统'), findsOneWidget);

    await tester.tap(find.text('关于系统'));
    await tester.pump(const Duration(milliseconds: 200));
    expect(find.text('关于 GeoScatter3D'), findsOneWidget);
    expect(find.text('我知道了'), findsOneWidget);

    await tester.tap(find.text('我知道了'));
    await tester.pump(const Duration(milliseconds: 200));
    expect(find.text('关于 GeoScatter3D'), findsNothing);
  });
}
