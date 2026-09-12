import 'package:flutter/material.dart';
import 'package:flutter/foundation.dart';
import 'dart:ui' as ui;
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:ui_flutter/main.dart' as app;

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  testWidgets('Vulkan sample, orbit, pinch, pan and reset render real frames', (
    tester,
  ) async {
    app.main();
    addTearDown(() async {
      await tester.pumpWidget(const SizedBox.shrink());
      await tester.pump();
    });
    await tester.pump();

    final errorFinder = find.byKey(const ValueKey('vulkan_engine_error'));
    final frameFinder = find.byKey(const ValueKey('viewport_frame'));

    // ViewportCanvas uses a live ticker, so wait for explicit milestones
    // instead of pumpAndSettle, which cannot settle an intentional frame loop.
    for (var attempt = 0; attempt < 40; attempt += 1) {
      if (errorFinder.evaluate().isNotEmpty ||
          frameFinder.evaluate().isNotEmpty) {
        break;
      }
      await tester.pump(const Duration(milliseconds: 250));
    }

    final errorText = tester
        .widgetList<Text>(
          find.descendant(of: errorFinder, matching: find.byType(Text)),
        )
        .map((text) => text.data ?? '')
        .join('\n');
    expect(errorFinder, findsNothing, reason: errorText);
    expect(find.byKey(const ValueKey('telemetry_hud')), findsOneWidget);
    expect(find.byKey(const ValueKey('viewport_status')), findsOneWidget);
    expect(find.textContaining('10000 可见点'), findsOneWidget);
    expect(frameFinder, findsOneWidget);

    Future<void> nextFrames() async {
      var previous = tester.widget<RawImage>(frameFinder).image;
      var completed = 0;
      for (var attempt = 0; attempt < 80 && completed < 2; attempt++) {
        await tester.pump(const Duration(milliseconds: 50));
        expect(errorFinder, findsNothing);
        final current = tester.widget<RawImage>(frameFinder).image;
        if (!identical(previous, current)) {
          previous = current;
          completed++;
        }
      }
      expect(completed, 2, reason: '操作后必须继续产生新帧');
    }

    Future<List<int>> snapshot() async {
      final image = tester.widget<RawImage>(frameFinder).image!.clone();
      try {
        final pixels = await image.toByteData(
          format: ui.ImageByteFormat.rawRgba,
        );
        return pixels!.buffer.asUint8List(
          pixels.offsetInBytes,
          pixels.lengthInBytes,
        );
      } finally {
        image.dispose();
      }
    }

    await nextFrames();
    final frame = tester.widget<RawImage>(frameFinder);
    expect(frame.image, isNotNull);
    final pixels = await frame.image!.toByteData(
      format: ui.ImageByteFormat.rawRgba,
    );
    expect(pixels, isNotNull);
    final bytes = pixels!.buffer.asUint8List(
      pixels.offsetInBytes,
      pixels.lengthInBytes,
    );
    var differentPixels = 0;
    for (var offset = 4; offset < bytes.length; offset += 4) {
      if (bytes[offset] != bytes[0] ||
          bytes[offset + 1] != bytes[1] ||
          bytes[offset + 2] != bytes[2]) {
        differentPixels++;
      }
    }
    expect(differentPixels, greaterThan(100), reason: '首帧必须包含点云，不能只有清屏色');

    final initial = await snapshot();
    final gestures = find.byKey(const ValueKey('viewport_gestures'));
    await tester.drag(gestures, const Offset(100, 40));
    await nextFrames();
    expect(
      listEquals(initial, await snapshot()),
      isFalse,
      reason: '单指旋转必须改变原生帧',
    );

    await tester.tap(find.byKey(const ValueKey('camera_reset')));
    await nextFrames();
    expect(
      listEquals(initial, await snapshot()),
      isTrue,
      reason: '复位应恢复相同相机和原生画面',
    );

    final center = tester.getCenter(gestures);
    Future<void> twoFinger({required bool pinch}) async {
      final first = await tester.startGesture(
        center - const Offset(60, 0),
        pointer: 1,
      );
      final second = await tester.startGesture(
        center + const Offset(60, 0),
        pointer: 2,
      );
      try {
        for (var step = 1; step <= 4; step++) {
          final delta = Offset(step * 15.0, pinch ? 0 : step * 8.0);
          await first.moveTo(
            center - const Offset(60, 0) + (pinch ? -delta : delta),
          );
          await second.moveTo(center + const Offset(60, 0) + delta);
          await tester.pump();
        }
      } finally {
        await first.up();
        await second.up();
      }
    }

    await twoFinger(pinch: true);
    await nextFrames();
    final zoomed = await snapshot();
    expect(listEquals(initial, zoomed), isFalse, reason: '双指缩放必须改变原生帧');
    await twoFinger(pinch: false);
    await nextFrames();
    expect(
      listEquals(zoomed, await snapshot()),
      isFalse,
      reason: '双指平移必须改变原生帧',
    );
    await tester.tap(find.byKey(const ValueKey('camera_reset')));
    await nextFrames();
    expect(listEquals(initial, await snapshot()), isTrue);
    expect(tester.takeException(), isNull);
  });
}
