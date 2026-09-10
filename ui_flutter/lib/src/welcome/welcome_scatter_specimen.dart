import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../theme/gs3d_tokens.dart';

/// 欢迎页 Hero 右侧的「散点标本」装饰画。
///
/// **这是纯装饰性的品牌插画**：固定种子的确定性点阵，不读取任何数据集、
/// 不参与任何渲染路径 —— 与工程红线 R3（视口渲染必须走 `gs3d_engine` 单一
/// 渲染核心、禁止 Dart 软件点云预览）无关。它只负责让首屏在一秒内传达
/// 产品气质：散点、高程、色标。
///
/// 用固定种子而非每次随机：每台机器、每次启动看到同一幅"标本"，
/// 离屏预览截图也因此可复现。
class WelcomeScatterSpecimen extends StatelessWidget {
  const WelcomeScatterSpecimen({super.key, this.height = 232});

  final double height;

  @override
  Widget build(BuildContext context) {
    return Container(
      height: height,
      decoration: BoxDecoration(
        gradient: const LinearGradient(
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
          colors: [Gs3dDark.cabinBg, Gs3dDark.railBg],
        ),
        borderRadius: BorderRadius.circular(Gs3dTokens.radiusXl),
        border: Border.all(color: Gs3dDark.railBorder),
        boxShadow: Gs3dTokens.elevation1,
      ),
      child: ClipRRect(
        borderRadius: BorderRadius.circular(Gs3dTokens.radiusXl - 1),
        child: Stack(
          children: [
            const Positioned.fill(child: CustomPaint(painter: _SpecimenPainter())),

            // 顶部内侧高光：让深色面板带一点玻璃的"呼吸感"。
            Positioned.fill(
              child: DecoratedBox(
                decoration: BoxDecoration(
                  gradient: LinearGradient(
                    begin: Alignment.topCenter,
                    end: const Alignment(0, 0.4),
                    colors: [Gs3dDark.fillFaint, Colors.transparent],
                  ),
                ),
              ),
            ),

            // 左下角：渲染内核徽标。
            Positioned(
              left: 14,
              bottom: 12,
              child: _SpecimenChip(
                icon: Icons.bolt_rounded,
                label: 'VULKAN 1.3 · 实时八叉树 LOD',
              ),
            ),

            // 右下角：色标图例，与视口色标条同一语言。
            const Positioned(right: 14, bottom: 12, child: _RampLegend()),
          ],
        ),
      ),
    );
  }
}

/// 深色面板上的徽标胶囊。
class _SpecimenChip extends StatelessWidget {
  const _SpecimenChip({required this.icon, required this.label});

  final IconData icon;
  final String label;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(
        horizontal: Gs3dTokens.space8 + 2,
        vertical: 4,
      ),
      decoration: BoxDecoration(
        color: Gs3dDark.fillWeak,
        borderRadius: BorderRadius.circular(Gs3dTokens.radiusPill),
        border: Border.all(color: Gs3dDark.hairline),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(icon, size: 11, color: Gs3dDark.textSecondary),
          const SizedBox(width: Gs3dTokens.space4 + 1),
          Text(
            label,
            style: Gs3dType.micro.copyWith(
              color: Gs3dDark.textSecondary,
              letterSpacing: 0.4,
            ),
          ),
        ],
      ),
    );
  }
}

/// 迷你色标图例：低 → 高，与工作台视口色标条同一语言。
class _RampLegend extends StatelessWidget {
  const _RampLegend();

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(
        horizontal: Gs3dTokens.space8 + 2,
        vertical: 4,
      ),
      decoration: BoxDecoration(
        color: Gs3dDark.fillWeak,
        borderRadius: BorderRadius.circular(Gs3dTokens.radiusPill),
        border: Border.all(color: Gs3dDark.hairline),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Text('低', style: Gs3dType.micro.copyWith(color: Gs3dDark.textTertiary)),
          const SizedBox(width: 6),
          Container(
            width: 44,
            height: 4,
            decoration: BoxDecoration(
              gradient: const LinearGradient(
                colors: [Gs3dDark.accentFrom, Gs3dDark.accentTo],
              ),
              borderRadius: BorderRadius.circular(Gs3dTokens.radiusPill),
            ),
          ),
          const SizedBox(width: 6),
          Text('高', style: Gs3dType.micro.copyWith(color: Gs3dDark.textTertiary)),
        ],
      ),
    );
  }
}

class _SpecimenDot {
  const _SpecimenDot({
    required this.x,
    required this.y,
    required this.radius,
    required this.t,
  });

  /// 归一化坐标（0..1）。
  final double x;
  final double y;

  /// 点半径（逻辑像素）。
  final double radius;

  /// 色标位置：0 = 山顶（蓝），1 = 山脚（青绿）。
  final double t;
}

class _SpecimenPainter extends CustomPainter {
  const _SpecimenPainter();

  /// 固定种子 + 山脊轮廓：点云读起来像一片带高程起伏的地形。
  static final List<_SpecimenDot> _dots = _generate(
    seed: 20260910,
    count: 110,
  );

  /// 近邻连线（星座式细线），让点阵有"被组织起来"的结构感。
  static final List<(int, int)> _links = _generateLinks(_dots, maxDist: 0.10);

  /// 山脊基线：两个不同频率的正弦叠出柔和起伏（值越小越靠上）。
  static double _ridgeY(double x) =>
      0.32 + 0.16 * math.sin(x * 5.2) + 0.10 * math.sin(x * 11.7 + 1.3);

  static List<_SpecimenDot> _generate({
    required int seed,
    required int count,
  }) {
    final rng = math.Random(seed);
    final dots = <_SpecimenDot>[];
    for (var i = 0; i < count; i++) {
      final x = 0.06 + rng.nextDouble() * 0.88;
      final y = (_ridgeY(x) + (rng.nextDouble() - 0.5) * 0.34).clamp(0.06, 0.90);
      final t = ((y - 0.06) / 0.84).clamp(0.0, 1.0);
      // 山顶（t 小）的点略大，形成"高处在上"的视觉暗示。
      final radius = 1.2 + rng.nextDouble() * 2.8 + (1 - t) * 1.5;
      dots.add(_SpecimenDot(x: x, y: y, radius: radius, t: t));
    }
    return dots;
  }

  static List<(int, int)> _generateLinks(
    List<_SpecimenDot> dots, {
    required double maxDist,
  }) {
    final links = <(int, int)>[];
    for (var i = 0; i < dots.length; i++) {
      for (var j = i + 1; j < dots.length; j++) {
        final dx = dots[i].x - dots[j].x;
        final dy = dots[i].y - dots[j].y;
        if (dx * dx + dy * dy < maxDist * maxDist) {
          links.add((i, j));
          if (links.length >= 70) return links;
        }
      }
    }
    return links;
  }

  @override
  void paint(Canvas canvas, Size size) {
    // 1) 山体剪影：沿山脊线向下闭合，填充自脊线向下消散的同色渐变，
    //    让散点明确"长在地形上"，而不是漂浮的星点。
    final ridgePath = Path()..moveTo(0, size.height);
    const step = 0.02;
    for (var x = 0.0; x <= 1.0 + step / 2; x += step) {
      ridgePath.lineTo(x * size.width, _ridgeY(x) * size.height);
    }
    ridgePath.lineTo(size.width, size.height);
    ridgePath.close();
    canvas.drawPath(
      ridgePath,
      Paint()
        ..shader = LinearGradient(
          begin: Alignment.topCenter,
          end: Alignment.bottomCenter,
          colors: [
            Gs3dDark.accentFrom.withValues(alpha: 0.20),
            Gs3dDark.accentTo.withValues(alpha: 0.02),
          ],
        ).createShader(Offset.zero & size),
    );

    // 2) 脊线发丝：一条高亮的山脊轮廓。
    final crest = Path();
    for (var x = 0.0; x <= 1.0 + step / 2; x += step) {
      final p = Offset(x * size.width, _ridgeY(x) * size.height);
      x == 0 ? crest.moveTo(p.dx, p.dy) : crest.lineTo(p.dx, p.dy);
    }
    canvas.drawPath(
      crest,
      Paint()
        ..color = Gs3dDark.accentTo.withValues(alpha: 0.45)
        ..strokeWidth = 1.1
        ..style = PaintingStyle.stroke,
    );

    // 3) 近邻细线：结构感，不抢点的戏。
    final linkPaint = Paint()
      ..color = Gs3dDark.hairline
      ..strokeWidth = 0.8
      ..style = PaintingStyle.stroke;
    for (final (i, j) in _links) {
      canvas.drawLine(
        Offset(_dots[i].x * size.width, _dots[i].y * size.height),
        Offset(_dots[j].x * size.width, _dots[j].y * size.height),
        linkPaint,
      );
    }

    // 4) 散点。
    for (final dot in _dots) {
      final color = Color.lerp(
        Gs3dDark.accentFrom,
        Gs3dDark.accentTo,
        dot.t,
      )!;
      final center = Offset(dot.x * size.width, dot.y * size.height);

      // 大点带一圈同色辉光：山顶的"高光"。
      if (dot.radius > 3.4) {
        canvas.drawCircle(
          center,
          dot.radius * 3.2,
          Paint()
            ..color = color.withValues(alpha: 0.10)
            ..maskFilter = const MaskFilter.blur(BlurStyle.normal, 6),
        );
      }

      canvas.drawCircle(
        center,
        dot.radius,
        Paint()..color = color.withValues(alpha: 0.55 + 0.45 * (1 - dot.t)),
      );
    }
  }

  @override
  bool shouldRepaint(covariant _SpecimenPainter oldDelegate) => false;
}
