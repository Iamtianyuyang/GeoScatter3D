import 'package:flutter/material.dart';

import '../theme/app_tokens.dart';

/// 视图轴：六个正交视角 + 复位。
///
/// 用文字标签而不是立体 Gizmo：在测绘作业里「正好对到 -Y」比看一个
/// 会转的小方块更直接，也不抢点云的注意力。
class CameraGizmo extends StatelessWidget {
  const CameraGizmo({
    super.key,
    required this.onAxisSelected,
    required this.onReset,
    this.enabled = true,
  });

  final ValueChanged<int> onAxisSelected;
  final VoidCallback onReset;
  final bool enabled;

  static const _axes = [
    ('X−', '看向 −X（西）'),
    ('X+', '看向 +X（东）'),
    ('Y−', '看向 −Y（南）'),
    ('Y+', '看向 +Y（北）'),
    ('Z−', '俯视（−Z）'),
    ('Z+', '仰视（+Z）'),
  ];

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    return Container(
      padding: const EdgeInsets.all(6),
      decoration: BoxDecoration(
        color: tokens.colors.panel.withValues(alpha: 0.86),
        border: Border.all(color: tokens.colors.line),
        borderRadius: tokens.radii.smAll,
      ),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          for (var row = 0; row < 2; row++)
            Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                for (var col = 0; col < 3; col++)
                  _AxisButton(
                    index: row * 3 + col,
                    label: _axes[row * 3 + col].$1,
                    tooltip: _axes[row * 3 + col].$2,
                    enabled: enabled,
                    onTap: onAxisSelected,
                  ),
              ],
            ),
          const SizedBox(height: 4),
          SizedBox(
            width: 108,
            height: 24,
            child: TextButton.icon(
              key: const ValueKey('camera_reset'),
              onPressed: enabled ? onReset : null,
              style: TextButton.styleFrom(
                padding: EdgeInsets.zero,
                foregroundColor: tokens.colors.textMid,
                shape: RoundedRectangleBorder(
                  borderRadius: tokens.radii.smAll,
                ),
              ),
              icon: const Icon(Icons.restart_alt, size: 14),
              label: Text(
                '复位视图',
                style: tokens.type.meta.copyWith(
                  fontWeight: FontWeight.w700,
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _AxisButton extends StatelessWidget {
  const _AxisButton({
    required this.index,
    required this.label,
    required this.tooltip,
    required this.enabled,
    required this.onTap,
  });

  final int index;
  final String label;
  final String tooltip;
  final bool enabled;
  final ValueChanged<int> onTap;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    return Tooltip(
      message: tooltip,
      child: InkWell(
        key: ValueKey('camera_axis_$index'),
        onTap: enabled ? () => onTap(index) : null,
        borderRadius: tokens.radii.smAll,
        child: SizedBox(
          width: 34,
          height: 26,
          child: Center(
            child: Text(
              label,
              style: tokens.type.numeric.copyWith(
                fontSize: 11,
                color: enabled ? tokens.colors.textMid : tokens.colors.textLow,
              ),
            ),
          ),
        ),
      ),
    );
  }
}

/// 缩放按钮列：滚轮之外再给一条可点的路径（触屏与演示都用得到）。
class ZoomControls extends StatelessWidget {
  const ZoomControls({
    super.key,
    required this.onZoomIn,
    required this.onZoomOut,
    this.enabled = true,
  });

  final VoidCallback onZoomIn;
  final VoidCallback onZoomOut;
  final bool enabled;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    Widget button(Key key, String tooltip, IconData icon, VoidCallback onTap) {
      return Tooltip(
        message: tooltip,
        child: InkWell(
          key: key,
          onTap: enabled ? onTap : null,
          child: SizedBox(
            width: 30,
            height: 28,
            child: Icon(
              icon,
              size: 16,
              color: enabled ? tokens.colors.textMid : tokens.colors.textLow,
            ),
          ),
        ),
      );
    }

    return Container(
      decoration: BoxDecoration(
        color: tokens.colors.panel.withValues(alpha: 0.86),
        border: Border.all(color: tokens.colors.line),
        borderRadius: tokens.radii.smAll,
      ),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          button(
            const ValueKey('camera_zoom_in'),
            '放大',
            Icons.add,
            onZoomIn,
          ),
          Container(height: 1, width: 30, color: tokens.colors.line),
          button(
            const ValueKey('camera_zoom_out'),
            '缩小',
            Icons.remove,
            onZoomOut,
          ),
        ],
      ),
    );
  }
}
