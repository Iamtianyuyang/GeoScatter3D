import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/widgets/telemetry_hud.dart';

void main() {
  testWidgets('TelemetryHud renders all metrics cleanly', (tester) async {
    await tester.pumpWidget(
      const MaterialApp(
        home: Scaffold(
          body: TelemetryHud(
            fps: 60.0,
            frameTimeMs: 16.67,
            renderTimeMs: 12.5,
            visiblePoints: 10000,
            gpuName: 'NVIDIA GeForce RTX 4070',
            status: '就绪',
          ),
        ),
      ),
    );

    expect(find.byKey(const Key('telemetry_hud')), findsOneWidget);
    expect(find.text('GeoScatter3D Vulkan'), findsOneWidget);
    expect(find.text('60.0 FPS | 16.67 ms'), findsOneWidget);
    expect(find.text('引擎 12.50 ms'), findsOneWidget);
    expect(find.text('10000 可见点 | 就绪'), findsOneWidget);
    expect(find.text('NVIDIA GeForce RTX 4070'), findsOneWidget);
  });

  testWidgets('render cost row is hidden when the engine cannot report it', (
    tester,
  ) async {
    await tester.pumpWidget(
      const MaterialApp(
        home: Scaffold(
          body: TelemetryHud(
            fps: 60.0,
            frameTimeMs: 16.67,
            visiblePoints: 10000,
            gpuName: 'Vulkan GPU',
          ),
        ),
      ),
    );

    expect(find.byKey(const ValueKey('viewport_render_cost')), findsNothing);
  });
}
