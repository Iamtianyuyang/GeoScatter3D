import 'package:flutter/material.dart';

import '../theme/app_tokens.dart';

/// 色标图例：视口内垂直渐变条 + 值域端点。
///
/// 颜色只用于「读图」，所以用引擎同名的 9 套配色的近似渐变即可；
/// 端点数值取自数据集摘要（真实值域），不是装饰性数字。
class ColormapLegend extends StatelessWidget {
  const ColormapLegend({
    super.key,
    required this.colormapIndex,
    required this.minLabel,
    required this.maxLabel,
    this.unit,
  });

  final int colormapIndex;
  final String minLabel;
  final String maxLabel;
  final String? unit;

  static const List<List<Color>> palettes = [
    // 0 地形
    [
      Color(0xFF2B6B3F),
      Color(0xFF6D8F3F),
      Color(0xFFC9B36A),
      Color(0xFFB2764A),
      Color(0xFFF3EFE6),
    ],
    // 1 Viridis
    [
      Color(0xFF440154),
      Color(0xFF3B528B),
      Color(0xFF21918C),
      Color(0xFF5DC863),
      Color(0xFFFDE725),
    ],
    // 2 Jet
    [
      Color(0xFF000080),
      Color(0xFF0000FF),
      Color(0xFF00FFFF),
      Color(0xFF00FF00),
      Color(0xFFFFFF00),
      Color(0xFFFF0000),
      Color(0xFF800000),
    ],
    // 3 灰度
    [Color(0xFF000000), Color(0xFFFFFFFF)],
    // 4 热力
    [
      Color(0xFF0B0B0B),
      Color(0xFF7A1010),
      Color(0xFFD9531E),
      Color(0xFFF6C85F),
      Color(0xFFFFFFFF),
    ],
    // 5 Coolwarm
    [
      Color(0xFF3B4CC0),
      Color(0xFF8DA8F5),
      Color(0xFFEDEDED),
      Color(0xFFF4A07A),
      Color(0xFFB40426),
    ],
    // 6 Turbo
    [
      Color(0xFF30123B),
      Color(0xFF4668E8),
      Color(0xFF1BCD8E),
      Color(0xFFA2FC3C),
      Color(0xFFF6B020),
      Color(0xFFE2410C),
      Color(0xFF7A0403),
    ],
    // 7 Plasma
    [
      Color(0xFF0D0887),
      Color(0xFF6A00A8),
      Color(0xFFB12A90),
      Color(0xFFE16462),
      Color(0xFFFCA636),
      Color(0xFFF0F921),
    ],
    // 8 Rainbow
    [
      Color(0xFF6E40AA),
      Color(0xFF3E8FD6),
      Color(0xFF2ABE8F),
      Color(0xFF9FD939),
      Color(0xFFF6C85F),
      Color(0xFFE05A5A),
    ],
  ];

  List<Color> get _colors =>
      palettes[colormapIndex.clamp(0, palettes.length - 1)];

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    final colors = _colors;
    return Container(
      padding: const EdgeInsets.fromLTRB(8, 8, 8, 8),
      decoration: BoxDecoration(
        color: tokens.colors.panel.withValues(alpha: 0.86),
        border: Border.all(color: tokens.colors.line),
        borderRadius: tokens.radii.smAll,
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Container(
            width: 10,
            height: 108,
            decoration: BoxDecoration(
              borderRadius: BorderRadius.circular(2),
              gradient: LinearGradient(
                begin: Alignment.bottomCenter,
                end: Alignment.topCenter,
                colors: colors,
              ),
            ),
          ),
          const SizedBox(width: 8),
          SizedBox(
            width: 58,
            height: 108,
            child: Column(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  maxLabel,
                  key: const ValueKey('legend_max'),
                  style: tokens.type.numeric.copyWith(
                    fontSize: 10.5,
                    color: tokens.colors.textHigh,
                  ),
                ),
                Text(
                  unit ?? '值域',
                  style: tokens.type.meta.copyWith(
                    fontSize: 9.5,
                    color: tokens.colors.textLow,
                  ),
                ),
                Text(
                  minLabel,
                  key: const ValueKey('legend_min'),
                  style: tokens.type.numeric.copyWith(
                    fontSize: 10.5,
                    color: tokens.colors.textHigh,
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}
