import 'dart:math' as math;
import 'package:flutter/material.dart';
import '../theme/app_theme.dart';
import '../theme/macos_tokens.dart';
import 'welcome_ui_keys.dart';

class WelcomeHero extends StatefulWidget {
  const WelcomeHero({super.key});

  @override
  State<WelcomeHero> createState() => _WelcomeHeroState();
}

class _WelcomeHeroState extends State<WelcomeHero> with SingleTickerProviderStateMixin {
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
      _particles.add(_HeroParticle(
        x: _random.nextDouble() * 800,
        y: _random.nextDouble() * 180,
        vx: (_random.nextDouble() - 0.5) * 24,
        vy: (_random.nextDouble() - 0.5) * 16,
        radius: _random.nextDouble() * 2.5 + 1.2,
        alpha: _random.nextDouble() * 0.45 + 0.15,
      ));
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
      margin: const EdgeInsets.only(bottom: 24),
      padding: const EdgeInsets.symmetric(vertical: 8),
      child: Stack(
        alignment: Alignment.center,
        children: [
          // 粒子流动态画布 (Apple 蓝紫粒子流)
          Positioned.fill(
            child: AnimatedBuilder(
              animation: _controller,
              builder: (context, _) {
                return CustomPaint(
                  painter: _HeroParticlePainter(
                    particles: _particles,
                  ),
                );
              },
            ),
          ),

          // 核心 Brand 标题与 Logo
          Row(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.center,
            children: [
              // macOS 连续曲率 Squircle 渐变 App Icon (符合 Apple HIG 设计规范)
              Container(
                key: WelcomeUiKeys.heroLogo,
                width: 76,
                height: 76,
                decoration: BoxDecoration(
                  gradient: const LinearGradient(
                    colors: [
                      Color(0xFF0F62FE),
                      MacOsTokens.systemBlue,
                      MacOsTokens.systemIndigo,
                    ],
                    begin: Alignment.topLeft,
                    end: Alignment.bottomRight,
                  ),
                  borderRadius: BorderRadius.circular(MacOsTokens.radiusIcon),
                  border: Border.all(
                    color: Colors.white.withValues(alpha: 0.4),
                    width: 1.2,
                  ),
                  boxShadow: MacOsTokens.heroIconShadow,
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

              // 品牌字样、版本与副标题
              Flexible(
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Wrap(
                      crossAxisAlignment: WrapCrossAlignment.center,
                      spacing: 12,
                      runSpacing: 4,
                      children: [
                        RichText(
                          text: const TextSpan(
                            children: [
                              TextSpan(
                                text: 'Geo',
                                style: TextStyle(
                                  fontSize: 32,
                                  fontWeight: FontWeight.w800,
                                  color: MacOsTokens.textPrimary,
                                  letterSpacing: -0.5,
                                ),
                              ),
                              TextSpan(
                                text: 'Scatter',
                                style: TextStyle(
                                  fontSize: 32,
                                  fontWeight: FontWeight.w800,
                                  color: MacOsTokens.systemBlue,
                                  letterSpacing: -0.5,
                                ),
                              ),
                              TextSpan(
                                text: ' 3D',
                                style: TextStyle(
                                  fontSize: 32,
                                  fontWeight: FontWeight.w800,
                                  color: MacOsTokens.textPrimary,
                                  letterSpacing: -0.5,
                                ),
                              ),
                            ],
                          ),
                        ),
                        // macOS 胶囊版本徽章
                        Container(
                          padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 3.5),
                          decoration: BoxDecoration(
                            color: MacOsTokens.systemBlueLight,
                            borderRadius: BorderRadius.circular(MacOsTokens.radiusPill),
                            border: Border.all(
                              color: MacOsTokens.systemBlue.withValues(alpha: 0.25),
                            ),
                          ),
                          child: const Text(
                            'v2.0 · Mac Edition',
                            style: TextStyle(
                              fontSize: 11,
                              fontWeight: FontWeight.w600,
                              color: MacOsTokens.systemBlue,
                              letterSpacing: 0.2,
                            ),
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(height: 6),
                    const Text(
                      '高精度大规模三维散点时空流式渲染系统 · macOS 现代图形工坊',
                      style: TextStyle(
                        fontSize: 13.5,
                        fontWeight: FontWeight.w400,
                        color: MacOsTokens.textSecondary,
                        letterSpacing: -0.1,
                      ),
                      overflow: TextOverflow.ellipsis,
                      maxLines: 1,
                    ),
                    const SizedBox(height: 10),
                    // macOS 风格技术特性微标签
                    Wrap(
                      spacing: 8,
                      runSpacing: 4,
                      children: const [
                        _FeaturePill(text: '八叉树 LOD 金字塔', color: MacOsTokens.systemBlue),
                        _FeaturePill(text: '纯 C-ABI 跨语言引擎', color: MacOsTokens.systemPurple),
                        _FeaturePill(text: '毫秒级亿级点云裁剪', color: MacOsTokens.systemGreen),
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
  final String text;
  final Color color;

  const _FeaturePill({required this.text, required this.color});

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.08),
        borderRadius: BorderRadius.circular(MacOsTokens.radiusPill),
        border: Border.all(color: color.withValues(alpha: 0.15)),
      ),
      child: Text(
        text,
        style: TextStyle(
          fontSize: 10.5,
          fontWeight: FontWeight.w500,
          color: color,
        ),
      ),
    );
  }
}

class _HeroParticle {
  double x;
  double y;
  double vx;
  double vy;
  double radius;
  double alpha;

  _HeroParticle({
    required this.x,
    required this.y,
    required this.vx,
    required this.vy,
    required this.radius,
    required this.alpha,
  });
}

class _HeroParticlePainter extends CustomPainter {
  final List<_HeroParticle> particles;

  _HeroParticlePainter({required this.particles});

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

      paint.color = MacOsTokens.systemBlue.withValues(alpha: p.alpha);
      canvas.drawCircle(Offset(p.x, p.y), p.radius, paint);
    }
  }

  @override
  bool shouldRepaint(covariant CustomPainter oldDelegate) => true;
}
