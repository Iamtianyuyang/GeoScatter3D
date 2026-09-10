import 'package:flutter/material.dart';

import '../ffi/geoscatter3d_service.dart';
import '../theme/gs3d_form_factor.dart';
import '../theme/gs3d_tokens.dart';
import '../widgets/fluid.dart';
import 'new_project_dialog.dart';
import 'open_project_dialog.dart';
import 'welcome_ui_keys.dart';

/// 欢迎页动作区：一枚主行动 + 两枚次级，权重递减。
///
/// Apple「目的性」原则：首屏只应有**一个**强调色实心主按钮（新建工程），
/// 其余降级为描边 / 静默样式，替代早期"三张等权重卡片 + 一枚游离按钮"
/// 的扁平堆叠。交互遵循流体设计——按压即时收缩、悬停辉光渐入，全部
/// 时长与阴影取自 [Gs3dMotion] / [Gs3dTokens]。
class WelcomeActionCards extends StatelessWidget {
  const WelcomeActionCards({super.key, required this.service});

  final GeoScatter3dService service;

  Future<void> _onNewProject(BuildContext context) {
    return Gs3dResponsiveDialog.show(
      context,
      title: '新建散点工程',
      builder: (_) => NewProjectDialog(service: service),
    );
  }

  Future<void> _onOpenProject(BuildContext context) {
    return Gs3dResponsiveDialog.show(
      context,
      title: '打开工程数据包',
      builder: (_) => OpenProjectDialog(service: service),
    );
  }

  Future<void> _onQuickDemo(BuildContext context) async {
    final result = await service.executeActionAsync('welcome.quick_demo');
    if (!context.mounted) return;
    if (result['success'] != true) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(
            result['message'] as String? ??
                '未在当前环境找到 sample-points.gs3d.bundle 示例数据包',
          ),
          backgroundColor: Gs3dTokens.danger,
        ),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    // 触控档：动作行命中区 ≥48（视觉不变）；卡片间距相应放大。
    final touch = context.isTouchPreferred;
    return Wrap(
      spacing: touch ? Gs3dTokens.space16 : Gs3dTokens.space12,
      runSpacing: touch ? Gs3dTokens.space16 : Gs3dTokens.space12,
      children: [
        _HeroButton(
          key: WelcomeUiKeys.newProjectAction,
          icon: Icons.add,
          label: '新建工程',
          hint: 'CSV / DAT · LOD 金字塔',
          variant: _HeroVariant.primary,
          touchTarget: touch,
          onTap: () => _onNewProject(context),
        ),
        _HeroButton(
          key: WelcomeUiKeys.openProjectAction,
          icon: Icons.folder_open_rounded,
          label: '打开工程',
          hint: '.gs3d / .gs3d.bundle',
          variant: _HeroVariant.secondary,
          touchTarget: touch,
          onTap: () => _onOpenProject(context),
        ),
        _HeroButton(
          key: WelcomeUiKeys.quickDemoAction,
          icon: Icons.play_arrow_rounded,
          label: '快速体验示例',
          hint: '内置散点示例，直达三维视口',
          variant: _HeroVariant.quiet,
          touchTarget: touch,
          onTap: () => _onQuickDemo(context),
        ),
      ],
    );
  }
}

enum _HeroVariant { primary, secondary, quiet }

/// 单枚动作按钮：可中断弹簧按压 + 悬停态渐变。
class _HeroButton extends StatelessWidget {
  const _HeroButton({
    super.key,
    required this.icon,
    required this.label,
    required this.hint,
    required this.variant,
    required this.onTap,
    this.touchTarget = false,
  });

  final IconData icon;
  final String label;
  final String hint;
  final _HeroVariant variant;
  final VoidCallback onTap;

  /// 触控档命中区 ≥48。
  final bool touchTarget;

  @override
  Widget build(BuildContext context) {
    final a11y = context.gs3dA11y;
    return Gs3dPressable(
      onTap: onTap,
      hoverScale: variant == _HeroVariant.primary ? 0.015 : 0.008,
      minTouchSize: touchTarget ? Gs3dTokens.hitTargetTouch : null,
      builder: (context, state) {
        final hovered = state != Gs3dInteractionState.idle;

        final (bg, fg, borderColor) = switch (variant) {
          _HeroVariant.primary => (
            hovered ? Gs3dTokens.accentHover : Gs3dTokens.accent,
            Gs3dTokens.textOnAccent,
            Colors.transparent,
          ),
          _HeroVariant.secondary => (
            hovered ? Gs3dTokens.surfaceHover : Gs3dTokens.surface,
            Gs3dTokens.textPrimary,
            Gs3dTokens.border,
          ),
          _HeroVariant.quiet => (
            hovered ? Gs3dTokens.accentSubtle : Colors.transparent,
            Gs3dTokens.accent,
            Colors.transparent,
          ),
        };

        return AnimatedContainer(
          duration: a11y.resolve(Gs3dMotion.micro),
          curve: Gs3dMotion.enterCurve,
          height: 40,
          padding: const EdgeInsets.symmetric(horizontal: Gs3dTokens.space16),
          decoration: BoxDecoration(
            color: bg,
            borderRadius: BorderRadius.circular(Gs3dTokens.radiusMd + 1),
            border: Border.all(color: borderColor),
            // 仅主行动带辉光：把"该点哪里"压缩成一个视觉焦点。
            boxShadow: variant == _HeroVariant.primary && hovered
                ? Gs3dTokens.accentGlow(alpha: 0.32, blur: 18)
                : Gs3dTokens.elevation0,
          ),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(icon, size: 17, color: fg),
              const SizedBox(width: Gs3dTokens.space8),
              Text(
                label,
                style: Gs3dType.bodyStrong.copyWith(
                  fontSize: 13.5,
                  color: fg,
                ),
              ),
              // 次级按钮把提示写成第二段小字，替代早期卡片里的整行副标题。
              if (variant != _HeroVariant.quiet) ...[
                const SizedBox(width: Gs3dTokens.space8),
                Text(
                  hint,
                  style: Gs3dType.caption.copyWith(
                    fontSize: 11.5,
                    color: fg.withValues(alpha: variant == _HeroVariant.primary
                        ? 0.82
                        : 0.72),
                  ),
                ),
              ],
            ],
          ),
        );
      },
    );
  }
}
