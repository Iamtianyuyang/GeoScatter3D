import 'package:flutter/material.dart';

import '../theme/gs3d_tokens.dart';
import 'welcome_scatter_specimen.dart';
import 'welcome_ui_keys.dart';

/// 欢迎页 Hero：品牌标识 + 光学标题 + 价值主张 + 「散点标本」装饰画。
///
/// 纯展示组件，不依赖服务对象；首屏动作区见 [WelcomeActionCards]。
/// 入场为一次性淡入上浮（`TweenAnimationBuilder`，无周期定时器），并遵循
/// 「减弱动效」无障碍偏好自动退化为即时呈现。
class WelcomeHero extends StatelessWidget {
  const WelcomeHero({super.key});

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, constraints) {
        final stacked = constraints.maxWidth < 720;

        final text = Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            _RiseIn(
              delay: 0,
              child: _brandRow(),
            ),
            const SizedBox(height: Gs3dTokens.space16 + 2),
            _RiseIn(
              delay: 0.15,
              child: const Text('从一份点云，到可漫游的地形。', style: Gs3dType.display),
            ),
            const SizedBox(height: Gs3dTokens.space12),
            _RiseIn(
              delay: 0.3,
              child: ConstrainedBox(
                constraints: const BoxConstraints(maxWidth: 460),
                child: Text(
                  '导入 CSV / DAT 点云，自动构建八叉树 LOD 金字塔，'
                  '在 Vulkan 1.3 视口里实时漫游百万级散点。',
                  style: Gs3dType.body.copyWith(
                    color: Gs3dTokens.textSecondary,
                    height: 1.6,
                  ),
                ),
              ),
            ),
          ],
        );

        final specimen = _RiseIn(
          delay: 0.45,
          child: WelcomeScatterSpecimen(height: stacked ? 176 : 236),
        );

        if (stacked) {
          return Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              text,
              const SizedBox(height: Gs3dTokens.space20),
              SizedBox(width: double.infinity, child: specimen),
            ],
          );
        }

        return Row(
          crossAxisAlignment: CrossAxisAlignment.center,
          children: [
            Expanded(flex: 11, child: text),
            const SizedBox(width: Gs3dTokens.space32),
            Expanded(flex: 9, child: specimen),
          ],
        );
      },
    );
  }

  /// 品牌行：渐变字标 + 眉题。
  Widget _brandRow() {
    return Row(
      children: [
        // 渐变字标（与「关于」视图同一视觉语言，保证品牌一致）。
        Container(
          key: WelcomeUiKeys.heroLogo,
          width: 46,
          height: 46,
          decoration: BoxDecoration(
            gradient: const LinearGradient(
              colors: [Gs3dTokens.accent, Gs3dPalette.systemIndigo],
              begin: Alignment.topLeft,
              end: Alignment.bottomRight,
            ),
            borderRadius: BorderRadius.circular(Gs3dTokens.radiusLg),
            boxShadow: Gs3dTokens.accentGlow(alpha: 0.26, blur: 14),
          ),
          alignment: Alignment.center,
          child: const Text(
            'G',
            style: TextStyle(
              fontSize: 24,
              fontWeight: FontWeight.w800,
              color: Gs3dTokens.textOnAccent,
            ),
          ),
        ),
        const SizedBox(width: Gs3dTokens.space12 + 2),
        // KB-02：超窄视口下品牌行会挤压文字列，Expanded + 单行省略保证
        // 任意宽度不横向溢出；桌面宽度充裕时布局与视觉零变化。
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                '空间数据工作台',
                style: Gs3dType.caption.copyWith(
                  color: Gs3dTokens.accent,
                  fontWeight: FontWeight.w700,
                  letterSpacing: 2.4,
                ),
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
              ),
              const SizedBox(height: 2),
              Text(
                'GEOSCATTER 3D · v0.1.0',
                style: Gs3dType.micro.copyWith(
                  color: Gs3dTokens.textTertiary,
                  letterSpacing: 0.6,
                ),
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
              ),
            ],
          ),
        ),
      ],
    );
  }
}

/// 一次性入场动效：淡入 + 14px 上浮，[delay]（0..1）在总时长内错峰。
///
/// 减弱动效时时长归零，直接呈现静止态；无周期定时器，用例可安全结束。
class _RiseIn extends StatelessWidget {
  const _RiseIn({required this.delay, required this.child});

  final double delay;
  final Widget child;

  @override
  Widget build(BuildContext context) {
    final a11y = context.gs3dA11y;
    final base = a11y.resolve(Gs3dMotion.slow);
    final duration = base == Duration.zero
        ? Duration.zero
        : base + Duration(milliseconds: (delay * 240).round());

    return TweenAnimationBuilder<double>(
      tween: Tween(begin: 0, end: 1),
      duration: duration,
      curve: Interval(delay.clamp(0.0, 1.0), 1.0, curve: Gs3dMotion.enterCurve),
      builder: (context, t, child) => Opacity(
        opacity: t.clamp(0.0, 1.0),
        child: Transform.translate(
          offset: Offset(0, 14 * (1 - t)),
          child: child,
        ),
      ),
      child: child,
    );
  }
}
