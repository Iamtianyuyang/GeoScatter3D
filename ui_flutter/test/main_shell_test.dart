import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/ffi/geoscatter3d_service.dart';
import 'package:ui_flutter/src/layouts/main_shell.dart';
import 'package:ui_flutter/src/theme/app_tokens.dart';
import 'package:ui_flutter/src/widgets/measurement_panel.dart';
import 'package:ui_flutter/src/widgets/project_panel.dart';
import 'package:ui_flutter/src/widgets/render_panel.dart';
import 'package:ui_flutter/src/widgets/result_panel.dart';
import 'support/fake_engine.dart';

void main() {
  testWidgets('shell reads as one console: top bar, rail, panel, status', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(1668, 944);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);

    await tester.pumpWidget(_testApp(_shell()));

    expect(find.byKey(const ValueKey('top_navigation')), findsOneWidget);
    expect(find.byKey(const ValueKey('left_tool_rail')), findsOneWidget);
    expect(find.byKey(const ValueKey('center_viewport_panel')), findsOneWidget);
    expect(find.byKey(const ValueKey('bottom_status_bar')), findsOneWidget);
    // 默认停在工作台上：工程面板展开，视口让位。
    expect(find.byKey(const ValueKey('side_panel')), findsOneWidget);
    expect(find.byType(ProjectPanel), findsOneWidget);
    expect(find.text('GeoScatter 3D'), findsOneWidget);
    expect(find.text('顶部导航区'), findsNothing);
    expect(tester.takeException(), isNull);
  });

  testWidgets('panel expands and the viewport gives up width instead of '
      'being covered', (tester) async {
    tester.view.physicalSize = const Size(1668, 944);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);

    await tester.pumpWidget(_testApp(_shell()));
    final center = find.byKey(const ValueKey('center_viewport_panel'));
    final openWidth = tester.getSize(center).width;

    await tester.tap(find.byTooltip('关闭面板'));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 250));
    expect(find.byKey(const ValueKey('side_panel')), findsNothing);
    expect(tester.getSize(center).width, greaterThan(openWidth));

    await tester.tap(find.byKey(const ValueKey('rail_measure')));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 250));
    expect(find.byType(MeasurementPanel), findsOneWidget);
    expect(tester.getSize(center).width, closeTo(openWidth, 1));
    expect(tester.takeException(), isNull);
  });

  testWidgets('rail switches sections and closes on a second tap', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(1668, 944);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);

    await tester.pumpWidget(_testApp(_shell()));

    await tester.tap(find.byKey(const ValueKey('rail_render')));
    await tester.pump();
    expect(find.byType(RenderPanel), findsOneWidget);
    expect(find.byType(ProjectPanel), findsNothing);

    await tester.tap(find.byKey(const ValueKey('rail_render')));
    await tester.pump();
    expect(find.byType(RenderPanel), findsNothing);
    expect(tester.takeException(), isNull);
  });

  testWidgets('panels and status stay overflow-free at 200% text scale', (
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

    for (final rail in ['rail_project', 'rail_render', 'rail_measure']) {
      await tester.tap(find.byKey(ValueKey(rail)));
      await tester.pump();
      expect(tester.takeException(), isNull);
    }
  });

  testWidgets('compact 800x600 keeps the console usable', (tester) async {
    tester.view.physicalSize = const Size(800, 600);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);

    await tester.pumpWidget(_testApp(_shell()));

    expect(find.byKey(const ValueKey('left_tool_rail')), findsOneWidget);
    expect(find.byKey(const ValueKey('bottom_status_bar')), findsOneWidget);
    expect(find.byKey(const ValueKey('center_viewport_panel')), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('top bar opens a bundle off the UI thread', (tester) async {
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
    await tester.tap(find.byKey(const ValueKey('open_bundle_button')));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 50));

    expect(engine.openedPaths, ['/tmp/gs3d-test-bundle']);
    expect(service.datasetPath, '/tmp/gs3d-test-bundle');
    expect(find.text('gs3d-test-bundle'), findsWidgets);
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
    await tester.tap(find.byKey(const ValueKey('open_bundle_button')));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 50));

    expect(engine.openedPaths, isEmpty);
    expect(service.datasetPath, isNull);
  });

  testWidgets('result panel exports only within the synchronous budget', (
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
    await tester.tap(find.byKey(const ValueKey('rail_output')));
    await tester.pump();

    expect(find.byType(ResultPanel), findsOneWidget);
    VoidCallback? onTap(String key) =>
        tester.widget<InkWell>(find.byKey(ValueKey(key))).onTap;
    expect(onTap('export_ply_button'), isNotNull);
    expect(onTap('save_frame_button'), isNotNull);
    expect(tester.takeException(), isNull);

    // 超过同步上限时按钮禁用，并说明原因。
    engine.openSourcePoints = 5000000;
    engine.openDrawnPoints = 100000;
    expect(await service.openBundle('/tmp/gs3d-big-bundle'), isTrue);
    await tester.pump();
    expect(onTap('export_ply_button'), isNull);
    expect(find.textContaining('暂不支持'), findsOneWidget);
  });

  testWidgets('measure section hosts the measurement panel', (tester) async {
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
    await tester.tap(find.byKey(const ValueKey('rail_measure')));
    await tester.pump();

    expect(find.byType(MeasurementPanel), findsOneWidget);
    expect(find.text('还没有拾取点'), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('render panel queues colormap and point size off the UI thread', (
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
    await tester.tap(find.byKey(const ValueKey('rail_render')));
    await tester.pump();
    expect(find.byType(RenderPanel), findsOneWidget);

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
