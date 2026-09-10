import 'package:flutter/material.dart';

import '../../theme/gs3d_form_factor.dart';
import '../../theme/gs3d_tokens.dart';
import '../../widgets/fluid.dart';

/// 平板工作台抽屉脚手架。
///
/// 形态（PRD §5.2）：
///   * 中央视口充满全幅（抽屉收起不占布局位）；
///   * 左右边缘把手（宽 [Gs3dFormFactorDims.drawerHandleWidth]，常驻可见，
///     命中区 ≥48）点按滑出 320–400 宽的浮层抽屉（`Gs3dGlass.panel` 材质 +
///     `Gs3dMotion.sheet` 弹簧入场），浮在视口之上、不遮挡主区布局；
///   * 抽屉内容复用既有 `LeftDockPanel` / `RightDockPanel`，零改写。
///
/// 开合状态是**纯布局局部 State**（架构红线：不进 service、不与
/// `leftDockVisible` 等引擎面板状态混淆）。
class TabletDrawerScaffold extends StatefulWidget {
  const TabletDrawerScaffold({
    super.key,
    required this.center,
    this.leftPanel,
    this.rightPanel,
  });

  /// 中央视口。
  final Widget center;

  /// 左侧停靠面板内容（工区 / 测量 / 统计）。
  final Widget? leftPanel;

  /// 右侧停靠面板内容（渲染参数）。
  final Widget? rightPanel;

  /// 首次进入手势引导「仅显示一次」（进程级一次；模拟器实测沿用同规则）。
  /// 公开静态标记便于测试在用例间复位。
  static bool _gestureGuideSeen = false;

  /// 复位引导卡显示标记（测试辅助；生产代码无需调用）。
  static void resetGestureGuide() => _gestureGuideSeen = false;

  /// 跳过手势引导卡（离屏预览工具 / 自动化截图使用）。
  static void suppressGestureGuide() => _gestureGuideSeen = true;

  @override
  State<TabletDrawerScaffold> createState() => _TabletDrawerScaffoldState();
}

class _TabletDrawerScaffoldState extends State<TabletDrawerScaffold> {
  bool _leftOpen = false;
  bool _rightOpen = false;

  void _toggleLeft() => setState(() => _leftOpen = !_leftOpen);

  void _toggleRight() => setState(() => _rightOpen = !_rightOpen);

  void _dismissGuide() =>
      setState(() => TabletDrawerScaffold._gestureGuideSeen = true);

  @override
  Widget build(BuildContext context) {
    return Stack(
      children: [
        // 1. 中央视口充满全幅。
        Positioned.fill(child: widget.center),

        // 2. 左侧滑出抽屉（浮层，不占布局位）。
        if (widget.leftPanel != null && _leftOpen)
          _DrawerPanel(
            key: const ValueKey('tablet.drawer.panel.left'),
            side: _DrawerSide.left,
            onClose: _toggleLeft,
            child: widget.leftPanel!,
          ),

        // 3. 右侧滑出抽屉。
        if (widget.rightPanel != null && _rightOpen)
          _DrawerPanel(
            key: const ValueKey('tablet.drawer.panel.right'),
            side: _DrawerSide.right,
            onClose: _toggleRight,
            child: widget.rightPanel!,
          ),

        // 4. 左右边缘把手（宽 24，常驻，命中 ≥48）。
        Positioned(
          key: const ValueKey('tablet.drawer.handle.left'),
          left: 0,
          top: 0,
          bottom: 0,
          width: Gs3dFormFactorDims.drawerHandleWidth,
          child: _DrawerHandle(
            side: _DrawerSide.left,
            active: _leftOpen,
            onTap: _toggleLeft,
          ),
        ),
        if (widget.rightPanel != null)
          Positioned(
            key: const ValueKey('tablet.drawer.handle.right'),
            right: 0,
            top: 0,
            bottom: 0,
            width: Gs3dFormFactorDims.drawerHandleWidth,
            child: _DrawerHandle(
              side: _DrawerSide.right,
              active: _rightOpen,
              onTap: _toggleRight,
            ),
          ),

        // 5. 首次进入手势引导卡（Gs3dMotion 弹簧入场，仅显示一次）。
        if (!TabletDrawerScaffold._gestureGuideSeen)
          Positioned.fill(
            child: _GestureGuide(onDismiss: _dismissGuide),
          ),
      ],
    );
  }
}

enum _DrawerSide { left, right }

/// 边缘把手：细长条 + 中央指示柄，触控命中区撑到 48。
class _DrawerHandle extends StatelessWidget {
  const _DrawerHandle({
    required this.side,
    required this.active,
    required this.onTap,
  });

  final _DrawerSide side;
  final bool active;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return Gs3dPressable(
      onTap: onTap,
      hoverScale: 0,
      minTouchSize: Gs3dTokens.hitTargetTouch,
      builder: (context, state) {
        final hovered = state != Gs3dInteractionState.idle;
        return AnimatedContainer(
          duration: Gs3dMotion.micro,
          curve: Gs3dMotion.standardCurve,
          alignment: Alignment.center,
          decoration: BoxDecoration(
            color: active
                ? Gs3dTokens.accentSubtle
                : (hovered ? Gs3dTokens.surfaceHover : Colors.transparent),
            border: Border(
              left: side == _DrawerSide.right
                  ? const BorderSide(color: Gs3dTokens.hairline)
                  : BorderSide.none,
              right: side == _DrawerSide.left
                  ? const BorderSide(color: Gs3dTokens.hairline)
                  : BorderSide.none,
            ),
          ),
          child: Container(
            width: 4,
            height: 56,
            decoration: BoxDecoration(
              color: active ? Gs3dTokens.accent : Gs3dTokens.borderStrong,
              borderRadius: BorderRadius.circular(Gs3dTokens.radiusPill),
            ),
          ),
        );
      },
    );
  }
}

/// 滑出抽屉面板：`Gs3dMotion.sheet` 弹簧入场（平移 + 淡入），玻璃材质。
class _DrawerPanel extends StatelessWidget {
  const _DrawerPanel({
    super.key,
    required this.side,
    required this.child,
    required this.onClose,
  });

  final _DrawerSide side;
  final Widget child;
  final VoidCallback onClose;

  @override
  Widget build(BuildContext context) {
    final fromLeft = side == _DrawerSide.left;
    final a11y = context.gs3dA11y;
    return Positioned(
      left: fromLeft ? Gs3dFormFactorDims.drawerHandleWidth + 6 : null,
      right: fromLeft ? null : Gs3dFormFactorDims.drawerHandleWidth + 6,
      top: Gs3dTokens.space8,
      bottom: Gs3dTokens.bottomBarTouchHeight + Gs3dTokens.space12,
      width: Gs3dFormFactorDims.tabletDrawerWidth,
      child: TweenAnimationBuilder<double>(
        tween: Tween(begin: 0.0, end: 1.0),
        duration: a11y.resolve(const Duration(milliseconds: 420)),
        curve: Gs3dSpringCurve(Gs3dMotion.sheet),
        builder: (context, t, childRef) {
          final clamped = t.clamp(0.0, 1.0);
          return Opacity(
            opacity: clamped,
            child: Transform.translate(
              offset: Offset(
                fromLeft ? -(1 - clamped) * 48 : (1 - clamped) * 48,
                0,
              ),
              child: childRef,
            ),
          );
        },
        child: Gs3dGlass(
          spec: Gs3dMaterial.panel,
          padding: EdgeInsets.zero,
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              // 抽屉头部：标题 + 关闭按钮（命中 ≥48）。
              Container(
                padding: const EdgeInsets.only(
                  left: Gs3dTokens.space12,
                  right: Gs3dTokens.space4,
                ),
                decoration: const BoxDecoration(
                  border: Border(
                    bottom: BorderSide(color: Gs3dTokens.hairline),
                  ),
                ),
                child: Row(
                  children: [
                    Icon(
                      fromLeft
                          ? Icons.view_sidebar_outlined
                          : Icons.tune_rounded,
                      size: 16,
                      color: Gs3dTokens.textTertiary,
                    ),
                    const SizedBox(width: Gs3dTokens.space8),
                    Expanded(
                      child: Text(
                        fromLeft ? '数据与测量' : '渲染与外观',
                        style: Gs3dType.overline,
                      ),
                    ),
                    IconButton(
                      key: ValueKey(
                        'tablet.drawer.close.${fromLeft ? 'left' : 'right'}',
                      ),
                      tooltip: '收起抽屉',
                      icon: const Icon(
                        Icons.close_rounded,
                        size: 18,
                        color: Gs3dTokens.textSecondary,
                      ),
                      constraints: const BoxConstraints(
                        minWidth: Gs3dTokens.hitTargetTouch,
                        minHeight: Gs3dTokens.hitTargetTouch,
                      ),
                      onPressed: onClose,
                    ),
                  ],
                ),
              ),
              Expanded(child: child),
            ],
          ),
        ),
      ),
    );
  }
}

/// 首次进入触控工作台的一次性手势引导卡。
class _GestureGuide extends StatelessWidget {
  const _GestureGuide({required this.onDismiss});

  final VoidCallback onDismiss;

  @override
  Widget build(BuildContext context) {
    final a11y = context.gs3dA11y;
    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: onDismiss,
      child: Container(
        color: Gs3dMaterial.scrim,
        alignment: Alignment.center,
        child: TweenAnimationBuilder<double>(
          tween: Tween(begin: 0.9, end: 1.0),
          duration: a11y.resolve(const Duration(milliseconds: 360)),
          curve: Gs3dSpringCurve(Gs3dMotion.sheet),
          builder: (context, t, childRef) {
            return Opacity(
              opacity: t.clamp(0.0, 1.0),
              child: Transform.scale(scale: t, child: childRef),
            );
          },
          child: Gs3dGlass(
            spec: Gs3dMaterial.overlay,
            padding: const EdgeInsets.all(Gs3dTokens.space24),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    const Icon(
                      Icons.touch_app_rounded,
                      size: 22,
                      color: Gs3dTokens.accent,
                    ),
                    const SizedBox(width: Gs3dTokens.space8),
                    Text('触控手势指南', style: Gs3dType.title2),
                  ],
                ),
                const SizedBox(height: Gs3dTokens.space12),
                const _GuideRow(
                  icon: Icons.pan_tool_alt_rounded,
                  label: '单指拖拽',
                  desc: '旋转三维视角',
                ),
                const _GuideRow(
                  icon: Icons.open_with_rounded,
                  label: '双指拖动',
                  desc: '平移视口',
                ),
                const _GuideRow(
                  icon: Icons.zoom_in_rounded,
                  label: '双指捏合',
                  desc: '缩放视野',
                ),
                const _GuideRow(
                  icon: Icons.ads_click_rounded,
                  label: '长按视口',
                  desc: '打开上下文菜单',
                ),
                const _GuideRow(
                  icon: Icons.swipe_left_alt_rounded,
                  label: '边缘把手',
                  desc: '唤出左右数据抽屉',
                ),
                const SizedBox(height: Gs3dTokens.space16),
                Align(
                  alignment: Alignment.centerRight,
                  child: FilledButton.icon(
                    key: const ValueKey('tablet.gesture_guide.dismiss'),
                    onPressed: onDismiss,
                    icon: const Icon(Icons.check_rounded, size: 16),
                    label: const Text('知道了'),
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

class _GuideRow extends StatelessWidget {
  const _GuideRow({
    required this.icon,
    required this.label,
    required this.desc,
  });

  final IconData icon;
  final String label;
  final String desc;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: Gs3dTokens.space4),
      child: Row(
        children: [
          Icon(icon, size: 18, color: Gs3dTokens.accent),
          const SizedBox(width: Gs3dTokens.space8),
          Text(label, style: Gs3dType.bodyStrong),
          const SizedBox(width: Gs3dTokens.space12),
          Expanded(
            child: Text(desc, style: Gs3dType.body, textAlign: TextAlign.right),
          ),
        ],
      ),
    );
  }
}
