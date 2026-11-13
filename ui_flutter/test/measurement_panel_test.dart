import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/ffi/geoscatter3d_service.dart';
import 'package:ui_flutter/src/theme/app_tokens.dart';
import 'package:ui_flutter/src/widgets/measurement_panel.dart';
import 'support/fake_engine.dart';

void main() {
  testWidgets('measurement panel shows picked points, distance and height', (
    tester,
  ) async {
    final engine = FakeEngine();
    final service = GeoScatter3dService(engine: engine);
    await service.initialize();
    addTearDown(service.dispose);

    await tester.pumpWidget(
      MaterialApp(
        theme: ThemeData(
          brightness: Brightness.dark,
          extensions: const [AppTokens.dark],
        ),
        home: Scaffold(body: MeasurementPanel(service: service)),
      ),
    );
    expect(find.textContaining('拾取起点和终点'), findsOneWidget);
    expect(find.byKey(const ValueKey('measurement_distance')), findsNothing);

    engine.pickResult = (x: 0.0, y: 0.0, z: 10.0, value: 0.0);
    service.pickPointAt(100, 100);
    await tester.pump();
    expect(find.byKey(const ValueKey('measurement_point_0')), findsOneWidget);

    engine.pickResult = (x: 3.0, y: 4.0, z: 12.5, value: 0.0);
    service.pickPointAt(120, 130);
    await tester.pump();
    expect(find.text('距离 5.590'), findsOneWidget);
    expect(find.text('高差 2.500'), findsOneWidget);

    await tester.tap(find.byKey(const ValueKey('measurement_clear')));
    await tester.pump();
    expect(service.pickedPoints, isEmpty);
    expect(find.textContaining('拾取起点和终点'), findsOneWidget);
    expect(tester.takeException(), isNull);
  });
}
