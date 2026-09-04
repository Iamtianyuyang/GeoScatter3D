import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/ffi/geoscatter3d_service.dart';
import 'package:ui_flutter/src/welcome/gpu_selection_dialog.dart';
import 'package:ui_flutter/src/welcome/new_project_dialog.dart';
import 'package:ui_flutter/src/welcome/open_project_dialog.dart';
import 'package:ui_flutter/src/welcome/recent_projects_card.dart';
import 'package:ui_flutter/src/welcome/welcome_action_cards.dart';
import 'package:ui_flutter/src/welcome/welcome_footer.dart';
import 'package:ui_flutter/src/welcome/welcome_hero.dart';
import 'package:ui_flutter/src/welcome/welcome_page.dart';
import 'package:ui_flutter/src/welcome/welcome_ui_keys.dart';

void main() {
  group('GeoScatter3dService AI / MCP Action Engine Tests', () {
    late GeoScatter3dService service;

    setUp(() {
      service = GeoScatter3dService();
      service.initialize();
    });

    test('getAvailableActions returns all 10 registered welcome actions', () {
      final actions = service.getAvailableActions();
      expect(actions.length, equals(10));
      final ids = actions.map((a) => a.id).toSet();
      expect(ids, containsAll([
        'welcome.quick_demo',
        'welcome.open_project',
        'welcome.new_project',
        'welcome.recent.open',
        'welcome.recent.clear',
        'welcome.gpu.set_preferred',
        'welcome.gpu.get_list',
        'welcome.get_state',
        'welcome.about.get_info',
        'welcome.docs.get_info',
      ]));
    });

    test('executeAction welcome.get_state returns full system snapshot', () {
      final res = service.executeAction('welcome.get_state');
      expect(res['success'], isTrue);
      final data = res['data'] as Map<String, dynamic>;
      expect(data['is_initialized'], isTrue);
      expect(data['is_workbench_active'], isFalse);
      expect(data['recent_project_count'], isNonNegative);
    });

    test('executeAction welcome.about.get_info and welcome.docs.get_info return specifications', () {
      final about = service.executeAction('welcome.about.get_info');
      expect(about['success'], isTrue);
      expect((about['data'] as Map)['app_name'], equals('GeoScatter3D'));

      final docs = service.executeAction('welcome.docs.get_info');
      expect(docs['success'], isTrue);
      expect((docs['data'] as Map)['supported_formats'], contains('.gs3d.bundle'));
    });

    test('executeAction welcome.gpu.get_list and welcome.gpu.set_preferred', () {
      final listRes = service.executeAction('welcome.gpu.get_list');
      expect(listRes['success'], isTrue);
      final gpus = (listRes['data'] as Map)['gpus'] as List;
      expect(gpus.isNotEmpty, isTrue);

      final setRes = service.executeAction('welcome.gpu.set_preferred', {'index': 0});
      expect(setRes['success'], isTrue);
      expect(service.activeGpuIndex, equals(0));

      final invalidRes = service.executeAction('welcome.gpu.set_preferred', {'index': 999});
      expect(invalidRes['success'], isFalse);
    });

    test('executeAction welcome.quick_demo loads dataset and activates workbench', () {
      final res = service.executeAction('welcome.quick_demo');
      expect(res['success'], isTrue);
      expect(service.isWorkbenchActive, isTrue);
      expect(service.summary.isLoaded, isTrue);
      expect(service.summary.pointCount, equals(25));
    });

    test('executeAction welcome.open_project handles valid and invalid paths', () {
      final invalidRes = service.executeAction('welcome.open_project', {'path': 'non_existent_path.gs3d'});
      expect(invalidRes['success'], isFalse);

      final validRes = service.executeAction('welcome.open_project', {'path': 'data/sample-points.gs3d.bundle'});
      expect(validRes['success'], isTrue);
      expect(service.isWorkbenchActive, isTrue);
    });

    test('executeAction welcome.new_project handles paths', () {
      final invalidRes = service.executeAction('welcome.new_project', {'path': ''});
      expect(invalidRes['success'], isFalse);

      final validRes = service.executeAction('welcome.new_project', {
        'path': 'data/sample-points.gs3d.bundle',
        'name': 'test_proj',
        'threads': 8,
      });
      expect(validRes['success'], isTrue);
    });

    test('executeAction welcome.recent.clear and welcome.recent.open', () {
      final clearRes = service.executeAction('welcome.recent.clear');
      expect(clearRes['success'], isTrue);
      expect(service.recentProjects.isEmpty, isTrue);

      final openInvalidRes = service.executeAction('welcome.recent.open', {'index': 0});
      expect(openInvalidRes['success'], isFalse);
    });

    test('executeAction handles unknown action gracefully', () {
      final res = service.executeAction('unknown.action.id');
      expect(res['success'], isFalse);
      expect(res['message'], contains('未知的动作 ID'));
    });

    test('executeJsonRpc dispatches list_ui_actions and ui_action properly', () {
      final listRpc = jsonEncode({
        'jsonrpc': '2.0',
        'id': 1,
        'method': 'list_ui_actions',
      });
      final listRes = jsonDecode(service.executeJsonRpc(listRpc));
      expect(listRes['result'], isList);
      expect((listRes['result'] as List).length, equals(10));

      final actionRpc = jsonEncode({
        'jsonrpc': '2.0',
        'id': 2,
        'method': 'ui_action',
        'params': {
          'action': 'welcome.get_state',
        },
      });
      final actionRes = jsonDecode(service.executeJsonRpc(actionRpc));
      expect(actionRes['result']['success'], isTrue);
    });
  });

  group('Welcome Page UI Keys and Interactive Component Tests', () {
    late GeoScatter3dService service;

    setUp(() {
      service = GeoScatter3dService();
      service.initialize();
    });

    testWidgets('WelcomeHero contains heroLogo key and brand texts', (WidgetTester tester) async {
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

      expect(find.byKey(WelcomeUiKeys.heroLogo), findsOneWidget);
      expect(find.text('G'), findsOneWidget);

      await tester.pumpWidget(const SizedBox());
      await tester.pump();
    });

    testWidgets('WelcomeActionCards contains action keys and triggers quick demo', (WidgetTester tester) async {
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(body: WelcomeActionCards(service: service)),
        ),
      );
      await tester.pump();

      expect(find.byKey(WelcomeUiKeys.newProjectAction), findsOneWidget);
      expect(find.byKey(WelcomeUiKeys.openProjectAction), findsOneWidget);
      expect(find.byKey(WelcomeUiKeys.quickDemoAction), findsOneWidget);

      await tester.tap(find.byKey(WelcomeUiKeys.quickDemoAction));
      await tester.pump(const Duration(milliseconds: 300));

      expect(service.isWorkbenchActive, isTrue);
      expect(service.summary.isLoaded, isTrue);
    });

    testWidgets('OpenProjectDialog binds all keys and sample chip fills path', (WidgetTester tester) async {
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(body: OpenProjectDialog(service: service)),
        ),
      );
      await tester.pump();

      expect(find.byKey(WelcomeUiKeys.openProjectDialogCloseButton), findsOneWidget);
      expect(find.byKey(WelcomeUiKeys.openProjectDialogPathInput), findsOneWidget);
      expect(find.byKey(WelcomeUiKeys.openProjectDialogSampleChip), findsOneWidget);
      expect(find.byKey(WelcomeUiKeys.openProjectDialogCancelButton), findsOneWidget);
      expect(find.byKey(WelcomeUiKeys.openProjectDialogSubmitButton), findsOneWidget);

      // 点击快捷填充示例 Chip
      await tester.tap(find.byKey(WelcomeUiKeys.openProjectDialogSampleChip));
      await tester.pump();

      final textField = tester.widget<TextField>(find.byKey(WelcomeUiKeys.openProjectDialogPathInput));
      expect(textField.controller?.text, equals('data/sample-points.gs3d.bundle'));

      // 提交并载入
      await tester.tap(find.byKey(WelcomeUiKeys.openProjectDialogSubmitButton));
      await tester.pump(const Duration(milliseconds: 200));

      expect(service.isWorkbenchActive, isTrue);
    });

    testWidgets('NewProjectDialog binds all keys and sliders', (WidgetTester tester) async {
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(body: NewProjectDialog(service: service)),
        ),
      );
      await tester.pump();

      expect(find.byKey(WelcomeUiKeys.newProjectDialogCloseButton), findsOneWidget);
      expect(find.byKey(WelcomeUiKeys.newProjectDialogPathInput), findsOneWidget);
      expect(find.byKey(WelcomeUiKeys.newProjectDialogBrowseButton), findsOneWidget);
      expect(find.byKey(WelcomeUiKeys.newProjectDialogNameInput), findsOneWidget);
      expect(find.byKey(WelcomeUiKeys.newProjectDialogThreadsSlider), findsOneWidget);
      expect(find.byKey(WelcomeUiKeys.newProjectDialogCancelButton), findsOneWidget);
      expect(find.byKey(WelcomeUiKeys.newProjectDialogSubmitButton), findsOneWidget);

      // 点击提交创建工程
      await tester.tap(find.byKey(WelcomeUiKeys.newProjectDialogSubmitButton));
      await tester.pump(const Duration(milliseconds: 200));

      expect(service.isWorkbenchActive, isTrue);
    });

    testWidgets('RecentProjectsCard displays items, keys and empty-state demo button', (WidgetTester tester) async {
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(body: RecentProjectsCard(service: service)),
        ),
      );
      await tester.pump();

      // 清空记录前有历史项
      expect(find.byKey(WelcomeUiKeys.recentClearButton), findsOneWidget);
      expect(find.byKey(WelcomeUiKeys.recentItem(0)), findsOneWidget);

      // 点击清空
      await tester.tap(find.byKey(WelcomeUiKeys.recentClearButton));
      await tester.pump(const Duration(milliseconds: 200));

      expect(find.byKey(WelcomeUiKeys.recentCancelClearButton), findsOneWidget);
      expect(find.byKey(WelcomeUiKeys.recentConfirmClearButton), findsOneWidget);

      // 确认清空
      await tester.tap(find.byKey(WelcomeUiKeys.recentConfirmClearButton));
      await tester.pump(const Duration(milliseconds: 200));

      // 验证空态及其一键体验按钮
      expect(find.byKey(WelcomeUiKeys.recentEmptyDemoButton), findsOneWidget);

      // 点击空态体验按钮
      await tester.tap(find.byKey(WelcomeUiKeys.recentEmptyDemoButton));
      await tester.pump(const Duration(milliseconds: 200));

      expect(service.isWorkbenchActive, isTrue);
    });

    testWidgets('GpuSelectionDialog binds keys and switches preferred GPU', (WidgetTester tester) async {
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(body: GpuSelectionDialog(service: service)),
        ),
      );
      await tester.pump();

      expect(find.byKey(WelcomeUiKeys.gpuDialogCloseButton), findsOneWidget);
      expect(find.byKey(WelcomeUiKeys.gpuItem(0)), findsOneWidget);
      expect(find.byKey(WelcomeUiKeys.gpuDialogCancelButton), findsOneWidget);
      expect(find.byKey(WelcomeUiKeys.gpuDialogApplyButton), findsOneWidget);

      // 切换 GPU
      if (service.gpus.length > 1) {
        await tester.tap(find.byKey(WelcomeUiKeys.gpuItem(1)));
        await tester.pump();
        await tester.tap(find.byKey(WelcomeUiKeys.gpuDialogApplyButton));
        await tester.pump(const Duration(milliseconds: 200));

        expect(service.activeGpuIndex, equals(1));
      }
    });

    testWidgets('WelcomeFooter displays GPU, Docs, and About dialogs with keys', (WidgetTester tester) async {
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

      expect(find.byKey(WelcomeUiKeys.footerGpuButton), findsOneWidget);
      expect(find.byKey(WelcomeUiKeys.footerDocsButton), findsOneWidget);
      expect(find.byKey(WelcomeUiKeys.footerAboutButton), findsOneWidget);

      // 测试技术指南弹窗
      await tester.tap(find.byKey(WelcomeUiKeys.footerDocsButton));
      await tester.pump(const Duration(milliseconds: 200));
      expect(find.text('操作指南与技术标准'), findsOneWidget);
      expect(find.byKey(WelcomeUiKeys.docsDialogCloseButton), findsOneWidget);
      await tester.tap(find.byKey(WelcomeUiKeys.docsDialogCloseButton));
      await tester.pump(const Duration(milliseconds: 200));
      expect(find.text('操作指南与技术标准'), findsNothing);

      // 测试关于系统弹窗
      await tester.tap(find.byKey(WelcomeUiKeys.footerAboutButton));
      await tester.pump(const Duration(milliseconds: 200));
      expect(find.text('关于 GeoScatter3D'), findsOneWidget);
      expect(find.byKey(WelcomeUiKeys.aboutDialogCloseButton), findsOneWidget);
      await tester.tap(find.byKey(WelcomeUiKeys.aboutDialogCloseButton));
      await tester.pump(const Duration(milliseconds: 200));
      expect(find.text('关于 GeoScatter3D'), findsNothing);
    });

    testWidgets('WelcomePage integration renders all main sections', (WidgetTester tester) async {
      tester.view.physicalSize = const Size(1280, 720);
      tester.view.devicePixelRatio = 1.0;
      addTearDown(() {
        tester.view.resetPhysicalSize();
        tester.view.resetDevicePixelRatio();
      });

      await tester.pumpWidget(
        MaterialApp(
          home: WelcomePage(service: service),
        ),
      );
      await tester.pump(const Duration(milliseconds: 100));

      expect(find.byKey(WelcomeUiKeys.heroLogo), findsOneWidget);
      expect(find.byKey(WelcomeUiKeys.newProjectAction), findsOneWidget);
      expect(find.byKey(WelcomeUiKeys.openProjectAction), findsOneWidget);
      expect(find.byKey(WelcomeUiKeys.quickDemoAction), findsOneWidget);
      expect(find.byKey(WelcomeUiKeys.footerGpuButton), findsOneWidget);
      expect(find.byKey(WelcomeUiKeys.footerDocsButton), findsOneWidget);
      expect(find.byKey(WelcomeUiKeys.footerAboutButton), findsOneWidget);

      await tester.pumpWidget(const SizedBox());
      await tester.pump();
    });
  });
}
