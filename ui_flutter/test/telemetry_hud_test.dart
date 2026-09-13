import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/theme/app_tokens.dart';
import 'package:ui_flutter/src/widgets/telemetry_hud.dart';

void main() {
  testWidgets('TelemetryHud renders all metrics cleanly', (tester) async {
    await tester.pumpWidget(
      MaterialApp(
        theme: ThemeData(extensions: const [AppTokens.dark]),
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
    expect(find.text('GEOSCATTER 3D'), findsOneWidget);
    expect(find.text('60.0'), findsOneWidget);
    expect(find.text('FPS'), findsOneWidget);
    expect(find.text('16.67'), findsOneWidget);
    expect(find.text('ms'), findsOneWidget);
    expect(find.text('帧耗时'), findsOneWidget);
    expect(find.text('12.50 ms'), findsOneWidget);
    expect(find.text('引擎'), findsOneWidget);
    expect(find.text('10,000'), findsOneWidget);
    expect(find.text('可见点'), findsOneWidget);
    expect(find.text('VULKAN'), findsOneWidget);
    expect(find.text('就绪'), findsOneWidget);
    expect(find.text('NVIDIA GeForce RTX 4070'), findsOneWidget);
    // 未传通路时不追加标签（保持既有外观）。
    expect(find.textContaining('零拷贝纹理'), findsNothing);
  });

  testWidgets('render path label shows which frame transport is live', (
    tester,
  ) async {
    await tester.pumpWidget(
      MaterialApp(
        theme: ThemeData(extensions: const [AppTokens.dark]),
        home: Scaffold(
          body: TelemetryHud(
            fps: 120,
            frameTimeMs: 8.22,
            visiblePoints: 72569,
            status: '就绪',
            renderPath: '零拷贝纹理',
          ),
        ),
      ),
    );

    // 真机排障：一眼看出引擎是接管了纹理还是仍在 RGBA 回读。
    expect(find.text('72,569'), findsOneWidget);
    expect(find.text('VULKAN · 零拷贝纹理'), findsOneWidget);

    await tester.pumpWidget(
      MaterialApp(
        theme: ThemeData(extensions: const [AppTokens.dark]),
        home: Scaffold(
          body: TelemetryHud(
            fps: 40,
            frameTimeMs: 25,
            visiblePoints: 72569,
            status: '就绪',
            renderPath: 'RGBA 回读',
          ),
        ),
      ),
    );
    expect(find.text('VULKAN · RGBA 回读'), findsOneWidget);
  });

  testWidgets('render cost row is hidden when the engine cannot report it', (
    tester,
  ) async {
    await tester.pumpWidget(
      MaterialApp(
        theme: ThemeData(extensions: const [AppTokens.dark]),
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

  testWidgets('TelemetryHud remains readable at 200% text scale', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(2000, 1800);
    tester.view.devicePixelRatio = 2;
    addTearDown(tester.view.reset);
    await tester.pumpWidget(
      MaterialApp(
        theme: ThemeData(extensions: const [AppTokens.dark]),
        home: MediaQuery(
          data: const MediaQueryData(textScaler: TextScaler.linear(2)),
          child: const Scaffold(
            body: SizedBox(
              width: 360,
              child: TelemetryHud(
                fps: 60,
                frameTimeMs: 16.67,
                renderTimeMs: 12.5,
                visiblePoints: 10000,
                lodSummary: '1 万 / 100 万 点 · LOD 2/3',
                gpuName: 'NVIDIA GeForce RTX 4070',
                status: '就绪',
                renderPath: '零拷贝纹理',
              ),
            ),
          ),
        ),
      ),
    );

    expect(tester.takeException(), isNull);
  });

  testWidgets('TelemetryStatusBar renders telemetry in the bottom strip', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(1600, 600);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);

    await tester.pumpWidget(
      MaterialApp(
        theme: ThemeData(extensions: const [AppTokens.dark]),
        home: const Scaffold(
          body: SizedBox(
            height: 56,
            child: TelemetryStatusBar(
              key: ValueKey('telemetry_status_bar'),
              fps: 125.6,
              frameTimeMs: 7.96,
              renderTimeMs: 2.34,
              visiblePoints: 10000,
              gpuName: 'Adreno (TM) 650',
              status: '就绪',
              renderPath: '零拷贝纹理',
            ),
          ),
        ),
      ),
    );

    expect(find.byKey(const ValueKey('telemetry_status_bar')), findsOneWidget);
    expect(find.text('VULKAN · 零拷贝纹理'), findsOneWidget);
    expect(find.text('125.6'), findsOneWidget);
    expect(find.text('7.96 ms'), findsOneWidget);
    expect(find.text('10,000'), findsOneWidget);
    expect(find.text('2.34 ms'), findsOneWidget);
    expect(find.text('Adreno (TM) 650'), findsOneWidget);
    expect(tester.takeException(), isNull);
  });
}
