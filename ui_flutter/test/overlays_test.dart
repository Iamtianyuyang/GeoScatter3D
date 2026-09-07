import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/ffi/geoscatter3d_service.dart';
import 'package:ui_flutter/src/panels/export_dialog.dart';
import 'package:ui_flutter/src/panels/performance_panel.dart';
import 'package:ui_flutter/src/panels/tile_inspector_dialog.dart';
import 'package:ui_flutter/src/widgets/command_palette.dart';
import 'package:ui_flutter/src/widgets/shortcut_overlay.dart';

void main() {
  group('GeoScatter3D Overlays and Modals Widget Tests', () {
    late GeoScatter3dService service;

    setUp(() {
      service = GeoScatter3dService();
    });

    testWidgets('CommandPalette renders, filters actions, and executes selected action', (WidgetTester tester) async {
      tester.view.physicalSize = const Size(1920, 1080);
      tester.view.devicePixelRatio = 1.0;
      addTearDown(() {
        tester.view.resetPhysicalSize();
        tester.view.resetDevicePixelRatio();
      });

      service.toggleCommandPalette(true);
      expect(service.isCommandPaletteOpen, isTrue);

      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: Stack(
              children: [
                Container(color: Colors.blueGrey),
                if (service.isCommandPaletteOpen)
                  CommandPalette(service: service),
              ],
            ),
          ),
        ),
      );
      await tester.pumpAndSettle();

      expect(find.byKey(const ValueKey('command_palette_modal')), findsOneWidget);
      expect(find.byKey(const ValueKey('command_palette_input')), findsOneWidget);

      // 输入搜索过滤词
      await tester.enterText(find.byKey(const ValueKey('command_palette_input')), '复位');
      await tester.pumpAndSettle();

      expect(find.textContaining('复位'), findsWidgets);

      // 点击执行第一个匹配项
      final resetItem = find.byKey(const ValueKey('command_item_workbench.camera.reset'));
      expect(resetItem, findsOneWidget);

      await tester.tap(resetItem);
      await tester.pumpAndSettle();

      // 执行后面板自动关闭
      expect(service.isCommandPaletteOpen, isFalse);
    });

    testWidgets('ShortcutOverlay renders cheat sheet and dismisses on close button', (WidgetTester tester) async {
      tester.view.physicalSize = const Size(1920, 1080);
      tester.view.devicePixelRatio = 1.0;
      addTearDown(() {
        tester.view.resetPhysicalSize();
        tester.view.resetDevicePixelRatio();
      });

      service.toggleShortcutOverlay(true);
      expect(service.isShortcutOverlayOpen, isTrue);

      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: Stack(
              children: [
                Container(color: Colors.black12),
                if (service.isShortcutOverlayOpen)
                  ShortcutOverlay(service: service),
              ],
            ),
          ),
        ),
      );
      await tester.pumpAndSettle();

      expect(find.byKey(const ValueKey('shortcut_overlay_modal')), findsOneWidget);
      expect(find.text('键盘与鼠标快捷操作指南'), findsOneWidget);
      expect(find.textContaining('视口漫游与交互观察'), findsOneWidget);
      expect(find.textContaining('几何测量与选区分析'), findsOneWidget);

      // 点击完成/我知道了按钮
      await tester.tap(find.text('我知道了'));
      await tester.pumpAndSettle();

      expect(service.isShortcutOverlayOpen, isFalse);
    });

    testWidgets('PerformancePanel renders live metrics and clears cache on button press', (WidgetTester tester) async {
      tester.view.physicalSize = const Size(1920, 1080);
      tester.view.devicePixelRatio = 1.0;
      addTearDown(() {
        tester.view.resetPhysicalSize();
        tester.view.resetDevicePixelRatio();
      });

      service.togglePerformancePanel(true);
      expect(service.isPerformancePanelOpen, isTrue);

      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: Stack(
              children: [
                Container(color: Colors.black26),
                if (service.isPerformancePanelOpen)
                  PerformancePanel(service: service),
              ],
            ),
          ),
        ),
      );
      await tester.pumpAndSettle();

      expect(find.byKey(const ValueKey('performance_panel_modal')), findsOneWidget);
      expect(find.text('性能指标与显存实时诊断'), findsOneWidget);
      expect(find.byKey(const ValueKey('btn_clear_cache')), findsOneWidget);

      // 点击清空显存缓存
      await tester.tap(find.byKey(const ValueKey('btn_clear_cache')));
      await tester.pumpAndSettle();

      expect(service.loadedTiles, equals(0));

      // 点击完成按钮关闭
      await tester.tap(find.text('完成'));
      await tester.pumpAndSettle();
      expect(service.isPerformancePanelOpen, isFalse);
    });

    testWidgets('TileInspectorDialog renders octree info and triggers clear cache', (WidgetTester tester) async {
      tester.view.physicalSize = const Size(1920, 1080);
      tester.view.devicePixelRatio = 1.0;
      addTearDown(() {
        tester.view.resetPhysicalSize();
        tester.view.resetDevicePixelRatio();
      });

      service.toggleTileInspector(true);
      expect(service.isTileInspectorOpen, isTrue);

      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: Stack(
              children: [
                Container(color: Colors.black26),
                if (service.isTileInspectorOpen)
                  TileInspectorDialog(service: service),
              ],
            ),
          ),
        ),
      );
      await tester.pumpAndSettle();

      expect(find.byKey(const ValueKey('tile_inspector_dialog')), findsOneWidget);
      expect(find.text('八叉树瓦片与 LOD 流式视察器'), findsOneWidget);
      expect(find.byKey(const ValueKey('btn_tile_inspector_clear')), findsOneWidget);

      // 点击重置缓存
      await tester.tap(find.byKey(const ValueKey('btn_tile_inspector_clear')));
      await tester.pumpAndSettle();
      expect(service.loadedTiles, equals(0));

      // 点击完成关闭
      await tester.tap(find.text('完成'));
      await tester.pumpAndSettle();
      expect(service.isTileInspectorOpen, isFalse);
    });

    testWidgets('ExportPointCloudDialog renders and executes export action', (WidgetTester tester) async {
      tester.view.physicalSize = const Size(1920, 1080);
      tester.view.devicePixelRatio = 1.0;
      addTearDown(() {
        tester.view.resetPhysicalSize();
        tester.view.resetDevicePixelRatio();
      });

      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: Builder(
              builder: (ctx) => ElevatedButton(
                key: const ValueKey('open_export_dialog'),
                onPressed: () {
                  showDialog(
                    context: ctx,
                    builder: (_) => ExportPointCloudDialog(service: service),
                  );
                },
                child: const Text('Open'),
              ),
            ),
          ),
        ),
      );
      await tester.pumpAndSettle();

      await tester.tap(find.byKey(const ValueKey('open_export_dialog')));
      await tester.pumpAndSettle();

      expect(find.byKey(const ValueKey('export_point_cloud_dialog')), findsOneWidget);
      expect(find.text('导出点云数据'), findsOneWidget);
      expect(find.byKey(const ValueKey('btn_confirm_export')), findsOneWidget);

      // 点击开始导出
      await tester.tap(find.byKey(const ValueKey('btn_confirm_export')));
      await tester.pumpAndSettle();

      // 对话框已关闭
      expect(find.byKey(const ValueKey('export_point_cloud_dialog')), findsNothing);
    });

    test('Overlay toggles and export action execution via service executeAction', () {
      // 1. Command Palette toggle action
      final cpRes = service.executeAction('workbench.overlay.command_palette', {'open': true});
      expect(cpRes['success'], isTrue);
      expect(service.isCommandPaletteOpen, isTrue);

      service.executeAction('workbench.overlay.command_palette', {'open': false});
      expect(service.isCommandPaletteOpen, isFalse);

      // 2. Shortcut Help toggle action
      final scRes = service.executeAction('workbench.overlay.shortcut_help', {'open': true});
      expect(scRes['success'], isTrue);
      expect(service.isShortcutOverlayOpen, isTrue);

      service.executeAction('workbench.overlay.shortcut_help', {'open': false});
      expect(service.isShortcutOverlayOpen, isFalse);

      // 3. Performance Panel toggle action
      final perfRes = service.executeAction('workbench.overlay.performance', {'open': true});
      expect(perfRes['success'], isTrue);
      expect(service.isPerformancePanelOpen, isTrue);

      service.executeAction('workbench.overlay.performance', {'open': false});
      expect(service.isPerformancePanelOpen, isFalse);

      // 4. Tile Inspector toggle action
      final tileRes = service.executeAction('workbench.overlay.tile_inspector', {'open': true});
      expect(tileRes['success'], isTrue);
      expect(service.isTileInspectorOpen, isTrue);

      service.executeAction('workbench.overlay.tile_inspector', {'open': false});
      expect(service.isTileInspectorOpen, isFalse);

      // 5. Cache Clear action
      final cacheRes = service.executeAction('workbench.cache.clear');
      expect(cacheRes['success'], isTrue);
      expect(service.loadedTiles, equals(0));

      // 6. Point cloud export action
      final expRes = service.executeAction('workbench.export', {
        'path': 'tmp_test_export.ply',
        'format': 'ply',
      });
      expect(expRes['success'], isTrue);
    });
  });
}
