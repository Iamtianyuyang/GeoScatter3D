import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/ffi/geoscatter3d_service.dart';
import 'package:ui_flutter/src/layouts/standard_workbench_layout.dart';
import 'package:ui_flutter/src/panels/bottom_status_bar.dart';
import 'package:ui_flutter/src/panels/center_viewport.dart';
import 'package:ui_flutter/src/panels/left_dock_panel.dart';
import 'package:ui_flutter/src/panels/right_dock_panel.dart';
import 'package:ui_flutter/src/panels/top_menu_bar.dart';
import 'package:ui_flutter/src/welcome/welcome_ui_keys.dart';

void main() {
  group('GeoScatter3D Workbench AI / MCP Action Engine Tests', () {
    late GeoScatter3dService service;

    setUp(() {
      service = GeoScatter3dService();
      service.initialize();
      service.openWorkbench();
    });

    test('getAvailableActions returns all 17 workbench actions', () {
      final actions = service.getAvailableActions();
      final ids = actions.map((a) => a.id).toSet();
      expect(ids, containsAll([
        'workbench.camera.reset',
        'workbench.camera.set_view',
        'workbench.camera.get',
        'workbench.render.set_point_size',
        'workbench.render.set_colormap',
        'workbench.render.set_color_attribute',
        'workbench.render.set_shape',
        'workbench.render.set_height_scale',
        'workbench.render.set_scalar_range',
        'workbench.render.reset_scalar_range',
        'workbench.render.set_background',
        'workbench.layout.set',
        'workbench.dock.toggle',
        'workbench.viewport.set_overlays',
        'workbench.measure.calculate',
        'workbench.stats.get',
        'workbench.screenshot',
      ]));
    });

    test('workbench.camera.reset and set_view actions', () {
      // 1. Preset top view
      final topRes = service.executeAction('workbench.camera.set_view', {'preset': 'top'});
      expect(topRes['success'], isTrue);
      expect(service.cameraAzimuth, equals(0.0));
      expect(service.cameraElevation, equals(90.0));

      // 2. Preset front view
      final frontRes = service.executeAction('workbench.camera.set_view', {'preset': 'front'});
      expect(frontRes['success'], isTrue);
      expect(service.cameraAzimuth, equals(0.0));
      expect(service.cameraElevation, equals(0.0));

      // 3. Preset side view
      final sideRes = service.executeAction('workbench.camera.set_view', {'preset': 'side'});
      expect(sideRes['success'], isTrue);
      expect(service.cameraAzimuth, equals(90.0));
      expect(service.cameraElevation, equals(0.0));

      // 4. Custom angles and zoom
      final customRes = service.executeAction('workbench.camera.set_view', {
        'azimuth': 120.0,
        'elevation': 45.0,
        'zoom': 2.5,
        'pan_x': 10.0,
        'pan_y': -15.0,
      });
      expect(customRes['success'], isTrue);
      expect(service.cameraAzimuth, equals(120.0));
      expect(service.cameraElevation, equals(45.0));
      expect(service.cameraZoom, equals(2.5));
      expect(service.cameraPanX, equals(10.0));
      expect(service.cameraPanY, equals(-15.0));

      // 5. Reset camera
      final resetRes = service.executeAction('workbench.camera.reset');
      expect(resetRes['success'], isTrue);
      expect(service.cameraAzimuth, equals(-45.0));
      expect(service.cameraElevation, equals(30.0));
      expect(service.cameraZoom, equals(1.0));
      expect(service.cameraPanX, equals(0.0));
      expect(service.cameraPanY, equals(0.0));
    });

    test('workbench.camera.set_panning action', () {
      expect(service.isPanning, isFalse);
      final panOn = service.executeAction('workbench.camera.set_panning', {'panning': true});
      expect(panOn['success'], isTrue);
      expect(service.isPanning, isTrue);

      final panOff = service.executeAction('workbench.camera.set_panning', {'panning': false});
      expect(panOff['success'], isTrue);
      expect(service.isPanning, isFalse);
    });

    test('workbench.viewport.set_overlays action', () {
      final res = service.executeAction('workbench.viewport.set_overlays', {
        'map_axis': false,
        'world_axis': true,
        'crosshair': false,
      });
      expect(res['success'], isTrue);
      expect(service.showMapAxis, isFalse);
      expect(service.showWorldAxis, isTrue);
      expect(service.showCrosshair, isFalse);
    });

    test('workbench.viewport.screenshot action', () {
      bool screenshotTriggered = false;
      service.onScreenshotRequested = () {
        screenshotTriggered = true;
      };

      final res = service.executeAction('workbench.viewport.screenshot');
      expect(res['success'], isTrue);
      expect(screenshotTriggered, isTrue);
    });

    test('workbench.viewport.set_bg_color action', () {
      final res = service.executeAction('workbench.viewport.set_bg_color', {
        'color': '#000000',
      });
      expect(res['success'], isTrue);
      expect(service.viewportBackgroundColor.toARGB32(), equals(0xFF000000));
    });

    test('workbench.point_cloud appearance actions (size, shape, height, colormap)', () {
      // 1. Point size
      final sizeRes = service.executeAction('workbench.point_cloud.set_size', {'size': 4.5});
      expect(sizeRes['success'], isTrue);
      expect(service.pointSize, equals(4.5));

      // 2. Point shape
      final shapeRes = service.executeAction('workbench.point_cloud.set_shape', {'shape': '圆形'});
      expect(shapeRes['success'], isTrue);
      expect(service.pointShape, equals('圆形'));

      // 3. Height scale
      final scaleRes = service.executeAction('workbench.point_cloud.set_height_scale', {'scale': 2.0});
      expect(scaleRes['success'], isTrue);
      expect(service.heightScale, equals(2.0));

      // 4. Height source
      final srcRes = service.executeAction('workbench.point_cloud.set_height_source', {'source': 'Z'});
      expect(srcRes['success'], isTrue);
      expect(service.heightSource, equals('Z'));

      // 5. Color attribute
      final attrRes = service.executeAction('workbench.point_cloud.set_color_attribute', {'attribute': 'elevation'});
      expect(attrRes['success'], isTrue);
      expect(service.colorAttribute, equals('elevation'));

      // 6. Colormap
      final mapRes = service.executeAction('workbench.point_cloud.set_colormap', {'colormap': 'Turbo'});
      expect(mapRes['success'], isTrue);
      expect(service.colormap, equals('Turbo'));

      // 7. Reset scalar range
      final resetRes = service.executeAction('workbench.point_cloud.reset_scalar_range');
      expect(resetRes['success'], isTrue);
    });

    test('workbench.dock.toggle_left and toggle_right actions', () {
      expect(service.leftDockVisible, isTrue);
      expect(service.rightDockVisible, isTrue);

      // Toggle left
      service.executeAction('workbench.dock.toggle_left', {'visible': false});
      expect(service.leftDockVisible, isFalse);
      service.executeAction('workbench.dock.toggle_left');
      expect(service.leftDockVisible, isTrue);

      // Toggle right
      service.executeAction('workbench.dock.toggle_right', {'visible': false});
      expect(service.rightDockVisible, isFalse);
      service.executeAction('workbench.dock.toggle_right');
      expect(service.rightDockVisible, isTrue);
    });

    test('workbench.measure.calculate and workbench.stats.calculate actions', () {
      // 1. Distance measurement
      final distRes = service.executeAction('workbench.measure.calculate', {'type': 'distance'});
      expect(distRes['success'], isTrue);
      expect(distRes['data'], isMap);
      expect(distRes['data']['metric'], equals('3d_distance'));

      // 2. Area measurement
      final areaRes = service.executeAction('workbench.measure.calculate', {'type': 'area'});
      expect(areaRes['success'], isTrue);
      expect(areaRes['data']['metric'], equals('projected_area'));

      // 3. Strike & dip measurement
      final strikeRes = service.executeAction('workbench.measure.calculate', {'type': 'strike_dip'});
      expect(strikeRes['success'], isTrue);
      expect(strikeRes['data']['metric'], equals('strike_dip'));

      // 4. Region statistics calculation
      final statsRes = service.executeAction('workbench.stats.calculate');
      expect(statsRes['success'], isTrue);
      expect(statsRes['data']['histogram'], isList);
      expect((statsRes['data']['histogram'] as List).length, equals(5));
    });

    test('workbench actions callable via JSON-RPC 2.0', () {
      final rpcReq = jsonEncode({
        'jsonrpc': '2.0',
        'id': 100,
        'method': 'ui_action',
        'params': {
          'action': 'workbench.camera.set_view',
          'params': {
            'preset': 'iso',
          },
        },
      });

      final rpcRes = jsonDecode(service.executeJsonRpc(rpcReq));
      expect(rpcRes['result']['success'], isTrue);
      expect(service.cameraAzimuth, equals(-45.0));
      expect(service.cameraElevation, equals(30.0));
    });
  });

  group('Workbench UI Component and Key Tests', () {
    late GeoScatter3dService service;

    setUp(() {
      service = GeoScatter3dService();
      service.initialize();
      service.openWorkbench();
    });

    testWidgets('TopMenuBar renders correctly and exposes menu keys', (WidgetTester tester) async {
      tester.view.physicalSize = const Size(1920, 1080);
      tester.view.devicePixelRatio = 1.0;
      addTearDown(() {
        tester.view.resetPhysicalSize();
        tester.view.resetDevicePixelRatio();
      });

      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: TopMenuBar(service: service),
          ),
        ),
      );
      await tester.pump(const Duration(milliseconds: 100));

      expect(find.byKey(WorkbenchUiKeys.topMenuBar), findsOneWidget);
      expect(find.byKey(WorkbenchUiKeys.homeButton), findsOneWidget);
      expect(find.byKey(WorkbenchUiKeys.menuFile), findsOneWidget);
      expect(find.byKey(WorkbenchUiKeys.menuView), findsOneWidget);
      expect(find.byKey(WorkbenchUiKeys.menuWindow), findsOneWidget);
      expect(find.byKey(WorkbenchUiKeys.menuHelp), findsOneWidget);
      expect(find.byKey(WorkbenchUiKeys.layoutSwitcher), findsOneWidget);
      expect(find.byKey(WorkbenchUiKeys.quickLoadDemoButton), findsOneWidget);
    });

    testWidgets('CenterViewport renders canvas, overlays, and floating toolbar', (WidgetTester tester) async {
      tester.view.physicalSize = const Size(1920, 1080);
      tester.view.devicePixelRatio = 1.0;
      addTearDown(() {
        tester.view.resetPhysicalSize();
        tester.view.resetDevicePixelRatio();
      });

      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: CenterViewport(service: service),
          ),
        ),
      );
      await tester.pump(const Duration(milliseconds: 100));

      expect(find.byKey(WorkbenchUiKeys.viewportCanvas), findsOneWidget);
      expect(find.byKey(WorkbenchUiKeys.viewportResetButton), findsOneWidget);
      expect(find.byKey(WorkbenchUiKeys.viewportPanToggle), findsOneWidget);
      expect(find.byKey(WorkbenchUiKeys.viewportMapAxisToggle), findsOneWidget);
      expect(find.byKey(WorkbenchUiKeys.viewportWorldAxisToggle), findsOneWidget);
      expect(find.byKey(WorkbenchUiKeys.viewportCrosshairToggle), findsOneWidget);
      expect(find.byKey(WorkbenchUiKeys.viewportPresetViewDropdown), findsOneWidget);
      expect(find.byKey(WorkbenchUiKeys.viewportZoomInButton), findsOneWidget);
      expect(find.byKey(WorkbenchUiKeys.viewportZoomOutButton), findsOneWidget);
      expect(find.byKey(WorkbenchUiKeys.viewportScreenshotButton), findsOneWidget);
      expect(find.byKey(WorkbenchUiKeys.viewportHudCard), findsOneWidget);
      expect(find.byKey(WorkbenchUiKeys.viewportGizmo), findsOneWidget);

      // 触发复位视角
      await tester.tap(find.byKey(WorkbenchUiKeys.viewportResetButton));
      await tester.pump(const Duration(milliseconds: 50));
      expect(service.cameraAzimuth, equals(-45.0));
      expect(service.cameraElevation, equals(30.0));
    });

    testWidgets('LeftDockPanel tab navigation and action triggers', (WidgetTester tester) async {
      tester.view.physicalSize = const Size(1920, 1080);
      tester.view.devicePixelRatio = 1.0;
      addTearDown(() {
        tester.view.resetPhysicalSize();
        tester.view.resetDevicePixelRatio();
      });

      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: LeftDockPanel(service: service),
          ),
        ),
      );
      await tester.pump(const Duration(milliseconds: 100));

      expect(find.byKey(WorkbenchUiKeys.leftDockPanel), findsOneWidget);
      expect(find.byKey(WorkbenchUiKeys.leftDockTabProject), findsOneWidget);
      expect(find.byKey(WorkbenchUiKeys.leftDockTabMeasure), findsOneWidget);
      expect(find.byKey(WorkbenchUiKeys.leftDockTabStats), findsOneWidget);

      // 切换至测量选项卡
      await tester.tap(find.byKey(WorkbenchUiKeys.leftDockTabMeasure));
      await tester.pump(const Duration(milliseconds: 100));
      expect(find.byKey(WorkbenchUiKeys.leftDockMeasureDistButton), findsOneWidget);
      expect(find.byKey(WorkbenchUiKeys.leftDockMeasureAreaButton), findsOneWidget);
      expect(find.byKey(WorkbenchUiKeys.leftDockMeasureStrikeDipButton), findsOneWidget);

      // 点击测量按钮
      await tester.tap(find.byKey(WorkbenchUiKeys.leftDockMeasureDistButton));
      await tester.pump(const Duration(milliseconds: 50));

      // 切换至区域统计选项卡
      await tester.tap(find.byKey(WorkbenchUiKeys.leftDockTabStats));
      await tester.pump(const Duration(milliseconds: 100));
      expect(find.byKey(WorkbenchUiKeys.leftDockStatsRecalculateButton), findsOneWidget);
    });

    testWidgets('RightDockPanel binds controls and modifies service parameters', (WidgetTester tester) async {
      tester.view.physicalSize = const Size(1920, 1080);
      tester.view.devicePixelRatio = 1.0;
      addTearDown(() {
        tester.view.resetPhysicalSize();
        tester.view.resetDevicePixelRatio();
      });

      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: RightDockPanel(service: service),
          ),
        ),
      );
      await tester.pump(const Duration(milliseconds: 100));

      expect(find.byKey(WorkbenchUiKeys.rightDockPanel), findsOneWidget);
      expect(find.byKey(WorkbenchUiKeys.rightDockCloseButton), findsOneWidget);
      expect(find.byKey(WorkbenchUiKeys.rightDockPointSizeSlider), findsOneWidget);
      expect(find.byKey(WorkbenchUiKeys.rightDockShapeDropdown), findsOneWidget);
      expect(find.byKey(WorkbenchUiKeys.rightDockHeightSourceDropdown), findsOneWidget);
      expect(find.byKey(WorkbenchUiKeys.rightDockHeightScaleSlider), findsOneWidget);
      expect(find.byKey(WorkbenchUiKeys.rightDockColorAttrDropdown), findsOneWidget);
      expect(find.byKey(WorkbenchUiKeys.rightDockColormapDropdown), findsOneWidget);
      expect(find.byKey(WorkbenchUiKeys.rightDockResetScalarRangeButton), findsOneWidget);

      // 点击关闭右侧面板
      await tester.tap(find.byKey(WorkbenchUiKeys.rightDockCloseButton));
      await tester.pump(const Duration(milliseconds: 50));
      expect(service.rightDockVisible, isFalse);
    });

    testWidgets('BottomStatusBar renders and reflects status', (WidgetTester tester) async {
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: BottomStatusBar(service: service),
          ),
        ),
      );
      await tester.pump(const Duration(milliseconds: 100));

      expect(find.byKey(WorkbenchUiKeys.bottomStatusBar), findsOneWidget);
      expect(find.textContaining('FPS'), findsOneWidget);
      expect(find.textContaining('就绪'), findsOneWidget);
    });

    testWidgets('StandardWorkbenchLayout reacts to left and right dock visibility toggles', (WidgetTester tester) async {
      tester.view.physicalSize = const Size(1920, 1080);
      tester.view.devicePixelRatio = 1.0;
      addTearDown(() {
        tester.view.resetPhysicalSize();
        tester.view.resetDevicePixelRatio();
      });

      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: StandardWorkbenchLayout(service: service),
          ),
        ),
      );
      await tester.pump(const Duration(milliseconds: 100));

      expect(find.byKey(WorkbenchUiKeys.leftDockPanel), findsOneWidget);
      expect(find.byKey(WorkbenchUiKeys.rightDockPanel), findsOneWidget);

      // 隐藏左侧面板
      service.toggleLeftDock(false);
      await tester.pump(const Duration(milliseconds: 100));
      expect(find.byKey(WorkbenchUiKeys.leftDockPanel), findsNothing);
      expect(find.byKey(WorkbenchUiKeys.rightDockPanel), findsOneWidget);

      // 隐藏右侧面板
      service.toggleRightDock(false);
      await tester.pump(const Duration(milliseconds: 100));
      expect(find.byKey(WorkbenchUiKeys.leftDockPanel), findsNothing);
      expect(find.byKey(WorkbenchUiKeys.rightDockPanel), findsNothing);

      // 重新显示两侧面板
      service.toggleLeftDock(true);
      service.toggleRightDock(true);
      await tester.pump(const Duration(milliseconds: 100));
      expect(find.byKey(WorkbenchUiKeys.leftDockPanel), findsOneWidget);
      expect(find.byKey(WorkbenchUiKeys.rightDockPanel), findsOneWidget);
    });
  });
}