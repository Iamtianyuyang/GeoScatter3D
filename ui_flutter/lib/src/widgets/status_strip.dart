import 'package:flutter/material.dart';

import '../theme/app_tokens.dart';

/// 底部状态条：一行放完「数据规模 / 性能 / GPU」。
///
/// 数值用等宽数字，刷新时不左右跳；读不到的值显示「—」而不是 0，
/// 避免把「没有数据」误读成「零」。
class StatusStrip extends StatelessWidget {
  const StatusStrip({
    super.key,
    required this.state,
    required this.sourcePoints,
    required this.drawnPoints,
    required this.fps,
    required this.frameMs,
    required this.renderMs,
    required this.gpuName,
    required this.lodSummary,
    required this.datasetName,
    this.error,
    this.onOpenBundle,
  });

  /// 0 就绪 / 1 载入中 / 2 错误
  final int state;
  final int sourcePoints;
  final int drawnPoints;
  final double fps;
  final double frameMs;
  final double renderMs;
  final String gpuName;
  final String lodSummary;
  final String? datasetName;
  final String? error;
  final VoidCallback? onOpenBundle;

  String _count(int value) {
    if (value <= 0) return '—';
    final text = value.toString();
    final buffer = StringBuffer();
    for (var i = 0; i < text.length; i++) {
      if (i > 0 && (text.length - i) % 3 == 0) buffer.write(',');
      buffer.write(text[i]);
    }
    return buffer.toString();
  }

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    final colors = tokens.colors;
    final (label, dot) = switch (state) {
      1 => ('载入中', colors.warn),
      2 => ('需要处理', colors.alert),
      _ => ('就绪', colors.signal),
    };

    Widget metric(String key, String name, String value, {bool first = false}) {
      return Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          if (!first)
            Container(
              width: 1,
              height: 12,
              margin: const EdgeInsets.symmetric(horizontal: 10),
              color: colors.line,
            ),
          Text(
            name,
            style: tokens.type.meta.copyWith(
              color: colors.textLow,
              fontSize: 10.5,
            ),
          ),
          const SizedBox(width: 5),
          Text(
            value,
            key: ValueKey(key),
            style: tokens.type.numeric.copyWith(
              fontSize: 11,
              color: colors.textHigh,
            ),
          ),
        ],
      );
    }

    return Container(
      key: const ValueKey('bottom_status_bar'),
      height: 30,
      padding: const EdgeInsets.symmetric(horizontal: 12),
      decoration: BoxDecoration(
        color: colors.ink,
        border: Border(top: BorderSide(color: colors.line)),
      ),
      child: Row(
        children: [
          Container(
            width: 6,
            height: 6,
            decoration: BoxDecoration(color: dot, shape: BoxShape.circle),
          ),
          const SizedBox(width: 6),
          Text(
            label,
            key: const ValueKey('status_state'),
            style: tokens.type.meta.copyWith(
              color: colors.textMid,
              fontWeight: FontWeight.w700,
              fontSize: 10.5,
            ),
          ),
          const SizedBox(width: 12),
          Flexible(
            child: Text(
              datasetName == null || datasetName!.isEmpty
                  ? '未打开数据集'
                  : datasetName!,
              key: const ValueKey('status_dataset'),
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: tokens.type.meta.copyWith(
                color: colors.textHigh,
                fontSize: 11,
              ),
            ),
          ),
          const SizedBox(width: 12),
          Text(
            '${_count(sourcePoints)} → ${_count(drawnPoints)}',
            key: const ValueKey('status_points'),
            style: tokens.type.numeric.copyWith(
              fontSize: 11,
              color: colors.textMid,
            ),
          ),
          const Spacer(),
          if (error == null && sourcePoints <= 0 && onOpenBundle != null) ...[
            InkWell(
              onTap: onOpenBundle,
              child: Text(
                '打开工程包',
                style: tokens.type.meta.copyWith(
                  fontSize: 10.5,
                  color: colors.signal,
                  fontWeight: FontWeight.w700,
                ),
              ),
            ),
            const SizedBox(width: 12),
          ],
          Text(
            lodSummary.isEmpty ? 'LOD —' : lodSummary,
            key: const ValueKey('status_lod'),
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: tokens.type.meta.copyWith(
              color: colors.textLow,
              fontSize: 10.5,
            ),
          ),
          const SizedBox(width: 10),
          metric('status_fps', 'FPS', fps > 0 ? fps.toStringAsFixed(0) : '—'),
          metric(
            'status_frame_ms',
            '帧',
            frameMs > 0 ? '${frameMs.toStringAsFixed(1)} ms' : '—',
          ),
          metric(
            'status_render_ms',
            '渲染',
            renderMs > 0 ? '${renderMs.toStringAsFixed(1)} ms' : '—',
          ),
          metric(
            'status_gpu',
            'GPU',
            gpuName.isEmpty ? '—' : gpuName,
            first: false,
          ),
        ],
      ),
    );
  }
}
