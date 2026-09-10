import 'dart:math' as math;
import 'package:flutter/material.dart';

import '../theme/gs3d_tokens.dart';

/// 绘制原生相机投影后的纯像素几何，与 PNG 使用同一 contain 变换。
class NativeOverlayPainter extends CustomPainter {
  final Map<String, dynamic> state;
  final bool worldAxis;
  final bool crosshair;
  final Offset? selectionStart;
  final Offset? selectionEnd;
  NativeOverlayPainter(
    this.state, {
    required this.worldAxis,
    required this.crosshair,
    this.selectionStart,
    this.selectionEnd,
  });

  @override
  void paint(Canvas canvas, Size size) {
    final width = (state['width'] as num?)?.toDouble() ?? 0;
    final height = (state['height'] as num?)?.toDouble() ?? 0;
    if (width <= 0 || height <= 0) return;
    final scale = math.min(size.width / width, size.height / height);
    final offset = Offset(
      (size.width - width * scale) / 2,
      (size.height - height * scale) / 2,
    );
    Offset point(List value) =>
        offset +
        Offset((value[0] as num).toDouble(), (value[1] as num).toDouble()) *
            scale;
    final paint = Paint()
      ..color = const Color(0xfff1c21b)
      ..strokeWidth = 1.5;
    void label(String text, Offset at) {
      final painter = TextPainter(
        text: TextSpan(
          text: text,
          style: const TextStyle(
            // `TextPainter` 不参与 `DefaultTextStyle` 继承，必须显式指定字体族，
            // 否则会退回系统默认字体，与全站排版不一致。
            fontFamily: Gs3dType.fontFamily,
            color: Colors.white,
            fontSize: 11,
            backgroundColor: Color(0xb3000000),
          ),
        ),
        textDirection: TextDirection.ltr,
      )..layout(maxWidth: 250);
      painter.paint(canvas, at);
    }

    if (worldAxis) {
      paint.color = Colors.white54;
      for (final line in state['axis_lines'] as List? ?? const []) {
        canvas.drawLine(
          point(line.sublist(0, 2)),
          point(line.sublist(2, 4)),
          paint,
        );
      }
      for (final tick in state['axis_ticks'] as List? ?? const []) {
        label(tick['text'].toString(), point([tick['x'], tick['y']]));
      }
    }
    for (final line in state['measurement_overlays'] as List? ?? const []) {
      final rgba = (line['color'] as num).toInt();
      paint.color = Color(
        (rgba & 0xff00ff00) | ((rgba & 0xff) << 16) | ((rgba >> 16) & 0xff),
      );
      final a = point(line['a']);
      final b = point(line['b']);
      canvas.drawLine(a, b, paint);
      canvas.drawCircle(a, 3, paint);
      canvas.drawCircle(b, 3, paint);
      label(line['label'].toString(), (a + b) / 2);
    }
    paint.color = const Color(0xfff1c21b);
    for (final entry in ['selected', 'pending_point']) {
      final position =
          state[entry == 'selected' ? 'selected_screen' : 'pending_screen'];
      if (state[entry] != null && position is List) {
        canvas.drawCircle(
          point(position),
          5,
          paint..style = PaintingStyle.stroke,
        );
      }
    }
    if (crosshair && state['hover'] != null && state['hover_screen'] is List) {
      final at = point(state['hover_screen']);
      canvas.drawLine(
        Offset(offset.dx, at.dy),
        Offset(size.width - offset.dx, at.dy),
        paint,
      );
      canvas.drawLine(
        Offset(at.dx, offset.dy),
        Offset(at.dx, size.height - offset.dy),
        paint,
      );
    }
    if (selectionStart != null && selectionEnd != null) {
      canvas.drawRect(
        Rect.fromPoints(selectionStart!, selectionEnd!),
        paint..style = PaintingStyle.stroke,
      );
    }
  }

  @override
  bool shouldRepaint(covariant NativeOverlayPainter oldDelegate) => true;
}
