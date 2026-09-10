import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../theme/gs3d_tokens.dart';
import 'welcome_ui_keys.dart';

/// 欢迎页品牌头图。
///
/// 视觉取自统一令牌 [Gs3dTokens] / [Gs3dType]；粒子流为纯装饰，遵循
/// 「减弱动效」无障碍偏好而自动静止。
class WelcomeHero extends StatefulWidget {
  const WelcomeHero({super.key});

  @override
  State<WelcomeHero> createState() => _WelcomeHeroState();
}

class _WelcomeHeroState extends State<WelcomeHero>
    with SingleTickerProviderStateMixin {
  late final AnimationController _controller;
  final List<_HeroParticle> _particles = [];
  final math.Random _random = math.Random(42);

  @override
  void initState() {
    super.initState();
    _controller = AnimationController(
      vsync: this,
      duration: const Duration(seconds: 10),
    )..repeat();

    for (int i = 0; i < 48; i++) {
      _particles.add(
        _HeroParticle(
          x: _random.nextDouble() * 800,
          y: _random.nextDouble() * 180,
          vx: (_random.nextDouble() - 0.5) * 24,
          vy: (_random.nextDouble() - 0.5) * 16,
          radius: _random.nextDouble() * 2.5 + 1.2,
          alpha: _random.nextDouble() * 0.45 + 0.15,
        ),
      );
    }
  }

  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    // 尊重「减弱动效」：停用装饰性粒子流。
    final reduce = context.gs3dA11y.reduceMotion;
    if (reduce && _controller.isAnimating) {
      _controller.stop();
    } else if (!reduce && !_controller.isAnimating) {
      _controller.repeat();
    }
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      width: double.infinity,
      margin: const EdgeInsets.only(bottom: Gs3dTokens.space24),
      padding: const EdgeInsets.symmetric(vertical: Gs3dTokens.space8),
      child: Stack(
        alignment: Alignment.center,
        children: [
          // 粒子流动态画布（装饰层，置于内容之下）。
          Positioned.fill(
            child: AnimatedBuilder(
              animation: _controller,
              builder: (context, _) => CustomPaint(
                painter: _HeroParticlePainter(particles: _particles),
              ),
            ),
          ),

          // 品牌标识 + 名称 + 版本徽章 + 特性胶囊。
          Row(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.center,
            children: [
              // 连续曲率 Squircle 渐变 App Icon（Apple HIG）。
              Container(
                key: WelcomeUiKeys.heroLogo,
                width: 76,
                height: 76,
                decoration: BoxDecoration(
                  gradient: const LinearGradient(
                    colors: [
                      Gs3dPalette.systemBluePressed,
                      Gs3dTokens.accent,
                      Gs3dPalette.systemIndigo,
                    ],
                    begin: Alignment.topLeft,
                    end: Alignment.bottomRight,
                  ),
                  borderRadius: BorderRadius.circular(Gs3dTokens.radiusXl),
                  border: Border.all(
                    color: Colors.white.withValues(alpha: 0.4),
                    width: 1.2,
                  ),
                  boxShadow: Gs3dTokens.accentGlow(alpha: 0.25, blur: 28),
                ),
                alignment: Alignment.center,
                child: const Text(
                  'G',
                  style: TextStyle(
                    fontSize: 38,
                    fontWeight: FontWeight.w800,
                    color: Colors.white,
                    letterSpacing: -1,
                  ),
                ),
              ),
              const SizedBox(width: 22),

              // 品牌字样、版本与副标题。
              Flexible(
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Wrap(
                      crossAxisAlignment: WrapCrossAlignment.center,
                      spacing: Gs3dTokens.space12,
                      runSpacing: Gs3dTokens.space4,
                      children: [
                        Text.rich(
                          const TextSpan(
                            children: [
                              TextSpan(
                                text: 'Geo',
                                style: TextStyle(color: Gs3dTokens.textPrimary),
                              ),
                              TextSpan(
                                text: 'Scatter',
                                style: TextStyle(color: Gs3dTokens.accent),
                              ),
                              TextSpan(
                                text: ' 3D',
                                style: TextStyle(color: Gs3dTokens.textPrimary),
                              ),
                            ],
                          ),
                          style: Gs3dType.display.copyWith(
                            fontWeight: FontWeight.w800,
                            letterSpacing: -0.5,
                          ),
                        ),
                        // 胶囊版本徽章。
                        Container(
                          padding: const EdgeInsets.symmetric(
                            horizontal: Gs3dTokens.space8 + 2,
                            vertical: 3.5,
                          ),
                          decoration: BoxDecoration(
                            color: Gs3dTokens.accentSubtle,
                            borderRadius: BorderRadius.circular(
                              Gs3dTokens.radiusPill,
                            ),
                            border: Border.all(color: Gs3dTokens.accentBorder),
                          ),
                          child: Text(
                            'v2.0 · Windows Edition',
                            style: Gs3dType.micro.copyWith(
                              color: Gs3dTokens.accent,
                              letterSpacing: 0.2,
                            ),
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(height: 6),
                    Text(
                      '高精度大规模三维散点时空流式渲染系统 · 现代图形工坊',
                      style: Gs3dType.body.copyWith(fontSize: 13.5),
                      overflow: TextOverflow.ellipsis,
                      maxLines: 1,
                    ),
                    const SizedBox(height: Gs3dTokens.space8 + 2),
                    // 技术特性微标签。
                    const Wrap(
                      spacing: Gs3dTokens.space8,
                      runSpacing: Gs3dTokens.space4,
                      children: [
                        _FeaturePill(
                          text: '八叉树 LOD 金字塔',
                          color: Gs3dPalette.systemBlue,
                        ),
                        _FeaturePill(
                          text: '纯 C-ABI 跨语言引擎',
                          color: Gs3dPalette.systemPurple,
                        ),
                        _FeaturePill(
                          text: '毫秒级亿级点云裁剪',
                          color: Gs3dPalette.systemGreen,
                        ),
                      ],
                    ),
                  ],
                ),
              ),
            ],
          ),
        ],
      ),
    );
  }
}

class _FeaturePill extends StatelessWidget {
  const _FeaturePill({required this.text, required this.color});

  final String text;
  final Color color;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(
        horizontal: Gs3dTokens.space8,
        vertical: 2,
      ),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.08),
        borderRadius: BorderRadius.circular(Gs3dTokens.radiusPill),
        border: Border.all(color: color.withValues(alpha: 0.15)),
      ),
      child: Text(
        text,
        style: Gs3dType.micro.copyWith(
          color: color,
          fontWeight: FontWeight.w500,
          letterSpacing: 0,
        ),
      ),
    );
  }
}

class _HeroParticle {
  _HeroParticle({
    required this.x,
    required this.y,
    required this.vx,
    required this.vy,
    required this.radius,
    required this.alpha,
  });

  double x;
  double y;
  double vx;
  double vy;
  double radius;
  double alpha;
}

class _HeroParticlePainter extends CustomPainter {
  _HeroParticlePainter({required this.particles});

  final List<_HeroParticle> particles;

  @override
  void paint(Canvas canvas, Size size) {
    final paint = Paint();
    const dt = 0.016;

    for (final p in particles) {
      p.x += p.vx * dt;
      p.y += p.vy * dt;

      if (p.x < 0) p.x = size.width;
      if (p.x > size.width) p.x = 0;
      if (p.y < 0) p.y = size.height;
      if (p.y > size.height) p.y = 0;

      paint.color = Gs3dTokens.accent.withValues(alpha: p.alpha);
      canvas.drawCircle(Offset(p.x, p.y), p.radius, paint);
    }
  }

  @override
  bool shouldRepaint(covariant CustomPainter oldDelegate) => true;
}
