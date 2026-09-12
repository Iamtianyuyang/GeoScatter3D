import 'dart:ui' as ui;

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
      // 注意：viewport_lod 这个 key 直接挂在 Text 上（不是容器），
      // 所以必须直接读该 widget，用 descendant 找子节点永远是空。
      if (lodFinder.evaluate().isEmpty) return '';
      return tester.widget<Text>(lodFinder).data ?? '';
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
    // 证据输出：粗尺度/细尺度规模行 + HUD 性能读数。
    String hudText(String key) {
      final finder = find.byKey(ValueKey(key));
      if (finder.evaluate().isEmpty) return '';
      return tester.widget<Text>(finder).data ?? '';
    }

    debugPrint('[ACCEPTANCE] coarse="$coarse"');
    debugPrint('[ACCEPTANCE] fine="$tileSummary"');
    debugPrint('[ACCEPTANCE] metrics="${hudText('viewport_metrics')}"'
        ' core="${hudText('viewport_render_cost')}"'
        ' points="${hudText('viewport_status')}"');

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

    // 覆盖度回归：瓦片全部驻留后 LOD 会在 tile 并集内被裁剪，若叠加层
    // 自身被误裁（历史上曾把带 kSpatialClip 的 push 传给 tile），画面会全黑。
    // 因此必须断言合成后的帧仍含点云像素，而不是只看“帧变了”。
    final frame = tester.widget<RawImage>(frameFinder).image!;
    final pixels = await frame.toByteData(format: ui.ImageByteFormat.rawRgba);
    expect(pixels, isNotNull);
    final bytes = pixels!.buffer.asUint8List(
      pixels.offsetInBytes,
      pixels.lengthInBytes,
    );
    var litPixels = 0;
    for (var offset = 0; offset < bytes.length; offset += 4) {
      if (bytes[offset] != bytes[0] ||
          bytes[offset + 1] != bytes[1] ||
          bytes[offset + 2] != bytes[2]) {
        litPixels++;
      }
    }
    expect(
      litPixels,
      greaterThan(1000),
      reason: '细尺度合成后必须仍有点云像素（tile 不得被 kSpatialClip 裁掉）',
    );
    expect(tester.takeException(), isNull);
  },
      // 未提供数据集时跳过（CI 无 bundle）。
      skip: kDatasetPath.isEmpty);
}
