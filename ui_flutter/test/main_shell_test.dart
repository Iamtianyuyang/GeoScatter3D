import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/layouts/main_shell.dart';
import 'package:ui_flutter/src/theme/app_tokens.dart';

void main() {
  testWidgets('MainShell renders the five static workspace regions', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(1668, 944);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);

    await tester.pumpWidget(
      _testApp(const MainShell(viewport: SizedBox.expand())),
    );

    expect(find.byKey(const ValueKey('top_navigation')), findsOneWidget);
    expect(find.byKey(const ValueKey('left_project_panel')), findsOneWidget);
    expect(find.byKey(const ValueKey('center_viewport_panel')), findsOneWidget);
    expect(find.byKey(const ValueKey('right_property_panel')), findsOneWidget);
    expect(find.byKey(const ValueKey('bottom_status_bar')), findsOneWidget);
    expect(find.byKey(const ValueKey('telemetry_status_bar')), findsOneWidget);

    expect(find.text('GeoScatter 3D'), findsOneWidget);
    expect(find.text('顶部导航区'), findsOneWidget);
    expect(find.text('左侧项目区'), findsOneWidget);
    expect(find.text('中央视图区'), findsOneWidget);
    expect(find.text('右侧属性区'), findsOneWidget);
    expect(find.text('底部状态区 / LOD'), findsOneWidget);
    expect(find.text('项目'), findsOneWidget);
    expect(find.text('数据'), findsOneWidget);
    expect(find.text('场景'), findsOneWidget);
    expect(find.text('成果'), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('MainShell remains overflow-free at 200% text scale', (
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

    expect(find.text('顶部导航区'), findsOneWidget);
    expect(find.text('底部状态区 / LOD'), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('collapsing side and bottom panels expands the viewport', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(1668, 944);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);

    await tester.pumpWidget(
      _testApp(const MainShell(viewport: SizedBox.expand())),
    );

    final center = find.byKey(const ValueKey('center_viewport_panel'));
    final initialCenter = tester.getSize(center);

    await tester.tap(find.byKey(const ValueKey('toggle_left_project')));
    await tester.pump();
    expect(find.byKey(const ValueKey('left_project_rail')), findsOneWidget);
    final afterLeftCollapse = tester.getSize(center);
    expect(afterLeftCollapse.width, greaterThan(initialCenter.width));

    await tester.tap(find.byKey(const ValueKey('toggle_right_property')));
    await tester.pump();
    expect(find.byKey(const ValueKey('right_property_rail')), findsOneWidget);
    final afterRightCollapse = tester.getSize(center);
    expect(afterRightCollapse.width, greaterThan(afterLeftCollapse.width));

    final heightBeforeFooterCollapse = afterRightCollapse.height;
    await tester.tap(find.byKey(const ValueKey('toggle_bottom_status')));
    await tester.pump();
    expect(
      find.byKey(const ValueKey('bottom_status_collapsed')),
      findsOneWidget,
    );
    expect(find.byKey(const ValueKey('telemetry_status_bar')), findsOneWidget);
    expect(find.textContaining('FPS'), findsOneWidget);
    expect(
      tester.getSize(center).height,
      greaterThan(heightBeforeFooterCollapse),
    );

    await tester.tap(find.byTooltip('展开左侧项目区'));
    await tester.pump();
    expect(find.byKey(const ValueKey('left_project_panel')), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('compact layouts default to collapsed utility panels', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(800, 600);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);

    await tester.pumpWidget(
      _testApp(const MainShell(viewport: SizedBox.expand())),
    );

    expect(find.byKey(const ValueKey('left_project_rail')), findsOneWidget);
    expect(find.byKey(const ValueKey('right_property_rail')), findsOneWidget);
    expect(
      find.byKey(const ValueKey('bottom_status_collapsed')),
      findsOneWidget,
    );
    expect(find.byKey(const ValueKey('center_viewport_panel')), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('collapsed status telemetry fits at 200% text scale', (
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

    await tester.tap(find.byKey(const ValueKey('toggle_bottom_status')));
    await tester.pump();

    expect(
      find.byKey(const ValueKey('bottom_status_collapsed')),
      findsOneWidget,
    );
    expect(find.byKey(const ValueKey('telemetry_status_bar')), findsOneWidget);
    expect(tester.takeException(), isNull);
  });
}

Widget _testApp(Widget home) {
  return MaterialApp(
    theme: ThemeData(
      brightness: Brightness.dark,
      extensions: const [AppTokens.dark],
    ),
    home: home,
  );
}
