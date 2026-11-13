import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/ffi/geoscatter3d_service.dart';
import 'package:ui_flutter/src/layouts/main_shell.dart';
import 'package:ui_flutter/src/theme/app_tokens.dart';
import 'package:ui_flutter/src/widgets/display_panel.dart';
import 'package:ui_flutter/src/widgets/measurement_panel.dart';
import 'package:ui_flutter/src/widgets/result_panel.dart';
import 'support/fake_engine.dart';

void main() {
  testWidgets('MainShell keeps the viewport as the full-screen base layer', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(1668, 944);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);

    await tester.pumpWidget(_testApp(_shell()));

    expect(find.byKey(const ValueKey('top_navigation')), findsOneWidget);
    expect(find.byKey(const ValueKey('left_tool_rail')), findsOneWidget);
    expect(find.byKey(const ValueKey('right_tool_rail')), findsOneWidget);
    expect(find.byKey(const ValueKey('center_viewport_panel')), findsOneWidget);
    expect(find.byKey(const ValueKey('bottom_status_bar')), findsOneWidget);
    expect(find.byKey(const ValueKey('telemetry_status_bar')), findsOneWidget);
    expect(find.byKey(const ValueKey('left_project_drawer')), findsNothing);
    expect(find.byKey(const ValueKey('right_property_drawer')), findsNothing);

    expect(find.text('GeoScatter 3D'), findsOneWidget);
    expect(find.text('顶部导航区'), findsOneWidget);
    expect(find.text('地形扫描演示场景'), findsNothing);
    expect(tester.takeException(), isNull);
  });

  testWidgets('left rail opens contextual drawers without resizing viewport', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(1668, 944);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);

    await tester.pumpWidget(_testApp(_shell()));
    final center = find.byKey(const ValueKey('center_viewport_panel'));
    final initialSize = tester.getSize(center);

    await tester.tap(find.byKey(const ValueKey('rail_project')));
    await tester.pump();
    expect(find.byKey(const ValueKey('left_project_drawer')), findsOneWidget);
    expect(find.text('项目资源'), findsOneWidget);
    expect(tester.getSize(center), initialSize);

    await tester.tap(find.byKey(const ValueKey('rail_data')));
    await tester.pump();
    expect(find.text('数据图层'), findsOneWidget);
    expect(find.text('项目资源'), findsNothing);

    await tester.tap(find.byTooltip('关闭面板'));
    await tester.pump();
    expect(find.byKey(const ValueKey('left_project_drawer')), findsNothing);
    expect(tester.takeException(), isNull);
  });

  testWidgets('right rail opens the contextual property drawer', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(1668, 944);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);

    await tester.pumpWidget(_testApp(_shell()));

    await tester.tap(find.byKey(const ValueKey('rail_properties')));
    await tester.pump();
    expect(find.byKey(const ValueKey('right_property_drawer')), findsOneWidget);
    expect(find.text('属性检查器'), findsOneWidget);

    await tester.tap(find.byKey(const ValueKey('rail_layers')));
    await tester.pump();
    expect(find.text('图层控制'), findsOneWidget);
    expect(find.text('属性检查器'), findsNothing);
    expect(tester.takeException(), isNull);
  });

  testWidgets('collapsed status keeps telemetry and viewport size', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(1668, 944);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);

    await tester.pumpWidget(_testApp(_shell()));
    final center = find.byKey(const ValueKey('center_viewport_panel'));
    final initialSize = tester.getSize(center);

    await tester.tap(find.byKey(const ValueKey('toggle_bottom_status')));
    await tester.pump();

    expect(
      find.byKey(const ValueKey('bottom_status_collapsed')),
      findsOneWidget,
    );
    expect(find.byKey(const ValueKey('telemetry_status_bar')), findsOneWidget);
    expect(find.textContaining('FPS'), findsOneWidget);
    expect(tester.getSize(center), initialSize);
    expect(tester.takeException(), isNull);
  });

  testWidgets('drawers and telemetry remain overflow-free at 200% text scale', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(1668, 944);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);

    await tester.pumpWidget(
      _testApp(
        const MediaQuery(
          data: MediaQueryData(textScaler: TextScaler.linear(2)),
          child: MainShell(viewport: SizedBox.expand()),
        ),
      ),
    );

    await tester.tap(find.byKey(const ValueKey('rail_project')));
    await tester.pump();
    expect(find.byKey(const ValueKey('left_project_drawer')), findsOneWidget);
    expect(tester.takeException(), isNull);

    await tester.tap(find.byKey(const ValueKey('toggle_bottom_status')));
    await tester.pump();
    expect(
      find.byKey(const ValueKey('bottom_status_collapsed')),
      findsOneWidget,
    );
    expect(tester.takeException(), isNull);
  });

  testWidgets('compact layouts default to a collapsed edge status', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(800, 600);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);

    await tester.pumpWidget(_testApp(_shell()));

    expect(find.byKey(const ValueKey('left_tool_rail')), findsOneWidget);
    expect(find.byKey(const ValueKey('right_tool_rail')), findsOneWidget);
    expect(
      find.byKey(const ValueKey('bottom_status_collapsed')),
      findsOneWidget,
    );
    expect(find.byKey(const ValueKey('center_viewport_panel')), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('data drawer opens a bundle off the UI thread', (tester) async {
    tester.view.physicalSize = const Size(1668, 944);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);

    final engine = FakeEngine();
    final service = GeoScatter3dService(engine: engine);
    await service.initialize();
    addTearDown(service.dispose);

    await tester.pumpWidget(
      _testApp(_shellWith(service, () async => '/tmp/gs3d-test-bundle')),
    );
    await tester.tap(find.byKey(const ValueKey('rail_data')));
    await tester.pump();

    await tester.tap(find.byKey(const ValueKey('open_bundle_button')));
    await tester.pumpAndSettle();

    expect(engine.openedPaths, ['/tmp/gs3d-test-bundle']);
    expect(service.datasetPath, '/tmp/gs3d-test-bundle');
    expect(find.text('/tmp/gs3d-test-bundle'), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('cancelled picker leaves the current dataset alone', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(1668, 944);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);

    final engine = FakeEngine();
    final service = GeoScatter3dService(engine: engine);
    await service.initialize();
    addTearDown(service.dispose);

    await tester.pumpWidget(_testApp(_shellWith(service, () async => null)));
    await tester.tap(find.byKey(const ValueKey('rail_data')));
    await tester.pump();
    await tester.tap(find.byKey(const ValueKey('open_bundle_button')));
    await tester.pumpAndSettle();

    expect(engine.openedPaths, isEmpty);
    expect(service.datasetPath, isNull);
  });

  testWidgets('result drawer exports only within the synchronous budget', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(1668, 944);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);

    final engine = FakeEngine();
    final service = GeoScatter3dService(engine: engine);
    await service.initialize();
    addTearDown(service.dispose);

    await tester.pumpWidget(
      _testApp(MainShell(viewport: const SizedBox.expand(), service: service)),
    );
    await tester.pump();
    await tester.tap(find.byKey(const ValueKey('rail_result')));
    await tester.pump();

    expect(find.byType(ResultPanel), findsOneWidget);
    OutlinedButton button(String key) =>
        tester.widget<OutlinedButton>(find.byKey(ValueKey(key)));
    expect(button('export_ply_button').onPressed, isNotNull);
    expect(button('save_frame_button').onPressed, isNotNull);
    expect(tester.takeException(), isNull);

    // 超过同步上限时按钮禁用，并说明原因。
    engine.openSourcePoints = 5000000;
    engine.openDrawnPoints = 100000;
    expect(await service.openBundle('/tmp/gs3d-big-bundle'), isTrue);
    await tester.pump();
    expect(button('export_ply_button').onPressed, isNull);
    expect(button('export_ply_button').onPressed, isNull);
    expect(find.textContaining('暂不支持'), findsOneWidget);
  });

  testWidgets('properties drawer hosts the measurement panel', (tester) async {
    tester.view.physicalSize = const Size(1668, 944);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);

    final engine = FakeEngine();
    final service = GeoScatter3dService(engine: engine);
    await service.initialize();
    addTearDown(service.dispose);

    await tester.pumpWidget(
      _testApp(MainShell(viewport: const SizedBox.expand(), service: service)),
    );
    await tester.pump();
    await tester.tap(find.byKey(const ValueKey('rail_properties')));
    await tester.pump();

    expect(find.byType(MeasurementPanel), findsOneWidget);
    expect(find.textContaining('拾取起点和终点'), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('display drawer queues colormap and point size off the UI thread', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(1668, 944);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);

    final engine = FakeEngine();
    final service = GeoScatter3dService(engine: engine);
    await service.initialize();
    addTearDown(service.dispose);

    await tester.pumpWidget(
      _testApp(
        MainShell(viewport: const SizedBox.expand(), service: service),
      ),
    );
    await tester.pump();
    await tester.tap(find.byKey(const ValueKey('rail_display')));
    await tester.pump();
    expect(find.byType(DisplayPanel), findsOneWidget);

    await tester.tap(find.byKey(const ValueKey('colormap_dropdown')));
    await tester.pump(const Duration(milliseconds: 400));
    await tester.tap(find.text('Jet').last);
    await tester.pump(const Duration(milliseconds: 400));

    expect(engine.submittedColormaps, [2]);
    expect(engine.colormap, 2);
    expect(service.renderSettings.colormapIndex, 2);

    await tester.drag(
      find.byKey(const ValueKey('point_size_slider')),
      const Offset(-40, 0),
    );
    await tester.pump(const Duration(milliseconds: 400));

    expect(engine.submittedPointSizes, isNotEmpty);
    expect(service.renderSettings.pointSize, engine.submittedPointSizes.last);
    expect(tester.takeException(), isNull);
  });
}

Widget _shell() => const MainShell(viewport: SizedBox.expand());

Widget _shellWith(
  GeoScatter3dService service,
  Future<String?> Function() pickBundle,
) => MainShell(
  viewport: const SizedBox.expand(),
  service: service,
  pickBundle: pickBundle,
);

Widget _testApp(Widget home) {
  return MaterialApp(
    theme: ThemeData(
      brightness: Brightness.dark,
      extensions: const [AppTokens.dark],
    ),
    home: home,
  );
}
