import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:ui_flutter/main.dart' as app;

/// 仅在显式提供数据集时运行（CI 无数据，默认跳过）。
const String kDatasetPath = String.fromEnvironment('GS3D_BENCH_DATASET');

/// 真机验收：粗尺度 LOD → 拉近后细尺度 tile 叠加全精度原始点。
///
/// 需要带 sidecar 的数据集：
///   `flutter test integration_test/tile_streaming_device_test.dart -d <device>` \
///     `--dart-define=GS3D_BENCH_DATASET=<bundle 路径>`
void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('zooming in activates full-resolution tile streaming', (
    tester,
  ) async {
    app.main();
    addTearDown(() async {
      await tester.pumpWidget(const SizedBox.shrink());
      await tester.pump();
    });
    await tester.pump();

    final errorFinder = find.byKey(const ValueKey('vulkan_engine_error'));
    final lodFinder = find.byKey(const ValueKey('viewport_lod'));

    Future<String> lodText() async {
      if (lodFinder.evaluate().isEmpty) return '';
      return tester
          .widgetList<Text>(
            find.descendant(of: lodFinder, matching: find.byType(Text)),
          )
          .map((text) => text.data ?? '')
          .join();
    }

    // 1. 等首帧与 LOD 规模行出现（粗尺度）。
    for (var attempt = 0; attempt < 180; attempt++) {
      if (errorFinder.evaluate().isNotEmpty ||
          (await lodText()).isNotEmpty) {
        break;
      }
      await tester.pump(const Duration(milliseconds: 250));
    }
    expect(errorFinder, findsNothing);
    final coarse = await lodText();
    expect(coarse, isNotEmpty, reason: '应显示 LOD 规模行');
    expect(
      coarse.contains('tile'),
      isFalse,
      reason: '贴合视图下应走粗尺度 LOD，而不是 tile（SSE 未达阈值）',
    );

    // 2. 双指放大（细尺度）：投影尺寸超过阈值后应转为 tile 全精度。
    final gestures = find.byKey(const ValueKey('viewport_gestures'));
    final center = tester.getCenter(gestures);
    Future<void> pinch({required int steps}) async {
      final first = await tester.startGesture(
        center - const Offset(80, 0),
        pointer: 1,
      );
      final second = await tester.startGesture(
        center + const Offset(80, 0),
        pointer: 2,
      );
      try {
        for (var step = 1; step <= steps; step++) {
          final delta = step * 24.0;
          await first.moveTo(center - Offset(80 + delta, 0));
          await second.moveTo(center + Offset(80 + delta, 0));
          await tester.pump(const Duration(milliseconds: 40));
        }
      } finally {
        await first.up();
        await second.up();
      }
    }

    var tileSummary = '';
    for (var round = 0; round < 6; round++) {
      await pinch(steps: 4);
      for (var attempt = 0; attempt < 40; attempt++) {
        await tester.pump(const Duration(milliseconds: 150));
        final text = await lodText();
        if (text.contains('tile')) {
          tileSummary = text;
          break;
        }
      }
      if (tileSummary.isNotEmpty) break;
    }

    expect(errorFinder, findsNothing);
    expect(
      tileSummary,
      isNotEmpty,
      reason: '放大后应进入细尺度 tile 流式（当前 LOD 行：${await lodText()}）',
    );
    expect(tileSummary, contains('驻留'));

    // 3. 画面必须仍在出帧（流式未卡死）。
    final frameFinder = find.byKey(const ValueKey('viewport_frame'));
    expect(frameFinder, findsOneWidget);
    final before = tester.widget<RawImage>(frameFinder).image;
    var changed = false;
    for (var attempt = 0; attempt < 60 && !changed; attempt++) {
      await tester.pump(const Duration(milliseconds: 100));
      changed = !identical(
        before,
        tester.widget<RawImage>(frameFinder).image,
      );
    }
    expect(changed, isTrue, reason: 'tile 流式期间必须继续出帧');
    expect(tester.takeException(), isNull);
  },
      // 未提供数据集时跳过（CI 无 bundle）。
      skip: kDatasetPath.isEmpty);
}
