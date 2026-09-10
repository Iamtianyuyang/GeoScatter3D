import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter/physics.dart';

import '../theme/gs3d_tokens.dart';

/// GeoScatter3D 流体交互组件库。
///
/// 遵循 Apple 流体界面三原则：
///   1. **即时响应** —— 按下即反馈，不等抬起。
///   2. **连续与可中断** —— 动画任意时刻可被新目标打断，并从当前位置与
///      速度续接（[Gs3dSpringValue] 用 `SpringSimulation` 实现，非固定时长）。
///   3. **尊重动量与边界** —— 动量投射 [Gs3dMotion.project]、橡皮筋
///      [Gs3dMotion.rubberBand]、速度移交 [Gs3dSpring.handoffVelocity]。
///
/// 所有材质与颜色一律取自 [Gs3dTokens] / [Gs3dMaterial]，并遵守无障碍偏好
/// （减弱动效 → 弹簧退化为即时；降低透明度 → 毛玻璃退化为实心）。

// ============================================================================
// 1. 可中断弹簧数值
// ============================================================================

/// 用弹簧物理驱动的数值动画。
///
/// 与 `AnimatedContainer` 等固定时长隐式动画不同：[value] 每次变更都会以
/// **当前值与当前速度**为新起点重算弹簧，因此手势/悬停高频切换不会出现
/// 顿挫或从头重播。
class Gs3dSpringValue extends StatefulWidget {
  const Gs3dSpringValue({
    super.key,
    required this.value,
    required this.builder,
    this.spring = Gs3dMotion.ui,
    this.initialValue,
    this.child,
  });

  /// 目标值。
  final double value;

  final Gs3dSpring spring;

  /// 初始值（缺省与 [value] 相同，避免首帧从 0 突变）。
  final double? initialValue;

  /// 透传给 [builder] 的子树，避免每帧重建。
  final Widget? child;

  final ValueWidgetBuilder<double> builder;

  @override
  State<Gs3dSpringValue> createState() => _Gs3dSpringValueState();
}

class _Gs3dSpringValueState extends State<Gs3dSpringValue>
    with SingleTickerProviderStateMixin {
  late final AnimationController _controller;

  @override
  void initState() {
    super.initState();
    _controller = AnimationController.unbounded(
      vsync: this,
      value: widget.initialValue ?? widget.value,
    );
  }

  @override
  void didUpdateWidget(Gs3dSpringValue oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (widget.value != oldWidget.value) {
      _animateTo(widget.value);
    }
  }

  void _animateTo(double target) {
    final a11y = Gs3dA11yScope.maybeOf(context);
    if (a11y?.reduceMotion ?? false) {
      _controller.value = target;
      return;
    }
    final simulation = SpringSimulation(
      widget.spring.toSpringDescription(),
      _controller.value,
      target,
      _controller.velocity,
    );
    _controller.animateWith(simulation);
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: _controller,
      builder: (context, childRef) =>
          widget.builder(context, _controller.value, childRef),
      child: widget.child,
    );
  }
}

// ============================================================================
// 2. 悬停 / 按压反馈
// ============================================================================

/// 交互态枚举，用于统一 hover / press 视觉。
enum Gs3dInteractionState { idle, hovered, pressed }

/// 悬停与按压弹簧反馈包装。
///
/// - 指针进入 → 立即提升（`response` 由 [Gs3dMotion.feedback] 控制）。
/// - 指针按下 → **立即**收缩，不等抬起（Apple 即时响应原则）。
/// - 指针移出 → 弹簧回位。
class Gs3dPressable extends StatefulWidget {
  const Gs3dPressable({
    super.key,
    this.child,
    this.onTap,
    this.onSecondaryTap,
    this.hoverScale = 0.0,
    this.pressScale = 0.018,
    this.cursor = SystemMouseCursors.click,
    this.builder,
  }) : assert(child != null || builder != null, 'child 与 builder 至少提供其一');

  /// 静态子树（未提供 [builder] 时使用）。
  final Widget? child;
  final VoidCallback? onTap;
  final VoidCallback? onSecondaryTap;

  /// 悬停时的放大比例（0 表示不缩放，仅走 [builder] 外观变化）。
  final double hoverScale;

  /// 按压时的收缩比例。
  final double pressScale;

  final MouseCursor cursor;

  /// 自定义外观：接收当前交互态。
  final Widget Function(BuildContext context, Gs3dInteractionState state)?
  builder;

  @override
  State<Gs3dPressable> createState() => _Gs3dPressableState();
}

class _Gs3dPressableState extends State<Gs3dPressable> {
  Gs3dInteractionState _state = Gs3dInteractionState.idle;

  void _set(Gs3dInteractionState next) {
    if (_state == next) return;
    setState(() => _state = next);
  }

  @override
  Widget build(BuildContext context) {
    final scale = switch (_state) {
      Gs3dInteractionState.pressed => 1 - widget.pressScale,
      Gs3dInteractionState.hovered => 1 + widget.hoverScale,
      Gs3dInteractionState.idle => 1.0,
    };

    final spring = switch (_state) {
      Gs3dInteractionState.pressed => Gs3dMotion.feedback,
      _ => Gs3dMotion.ui,
    };

    Widget content = widget.builder != null
        ? widget.builder!(context, _state)
        : widget.child!;

    content = Gs3dSpringValue(
      value: scale,
      spring: spring,
      builder: (context, value, child) => Transform.scale(
        scale: value,
        child: child,
      ),
      child: content,
    );

    return MouseRegion(
      cursor: widget.onTap != null ? widget.cursor : MouseCursor.defer,
      onEnter: (_) => _set(Gs3dInteractionState.hovered),
      onExit: (_) => _set(Gs3dInteractionState.idle),
      child: GestureDetector(
        behavior: HitTestBehavior.opaque,
        onTapDown: widget.onTap == null
            ? null
            : (_) => _set(Gs3dInteractionState.pressed),
        onTapCancel: () => _set(Gs3dInteractionState.hovered),
        onTapUp: widget.onTap == null
            ? null
            : (_) {
                _set(Gs3dInteractionState.hovered);
                widget.onTap!();
              },
        onSecondaryTap: widget.onSecondaryTap,
        child: content,
      ),
    );
  }
}

// ============================================================================
// 3. 玻璃材质容器
// ============================================================================

/// 由 [Gs3dGlassSpec] 驱动的毛玻璃容器，自动适配「降低透明度」偏好。
class Gs3dGlass extends StatelessWidget {
  const Gs3dGlass({
    super.key,
    required this.child,
    this.spec = Gs3dMaterial.panel,
    this.borderRadius,
    this.padding,
    this.margin,
    this.width,
    this.height,
    this.clipBehavior = Clip.antiAlias,
  });

  final Widget child;
  final Gs3dGlassSpec spec;
  final BorderRadius? borderRadius;
  final EdgeInsetsGeometry? padding;
  final EdgeInsetsGeometry? margin;
  final double? width;
  final double? height;
  final Clip clipBehavior;

  @override
  Widget build(BuildContext context) {
    final effective = context.gs3dA11y.resolveGlass(spec);
    final radius = borderRadius ?? effective.radius;
    final hasBlur = effective.blurSigma > 0;

    Widget content = Padding(
      padding: padding ?? EdgeInsets.zero,
      child: child,
    );
    if (hasBlur) {
      content = ClipRRect(
        borderRadius: radius,
        child: BackdropFilter(
          filter: ui.ImageFilter.blur(
            sigmaX: effective.blurSigma,
            sigmaY: effective.blurSigma,
            tileMode: TileMode.clamp,
          ),
          child: content,
        ),
      );
    }

    return Container(
      width: width,
      height: height,
      margin: margin,
      decoration: BoxDecoration(
        color: effective.tint,
        borderRadius: radius,
        border: Border.all(
          color: effective.borderColor,
          width: effective.borderWidth,
        ),
        boxShadow: effective.shadows,
      ),
      clipBehavior: hasBlur ? clipBehavior : Clip.none,
      child: content,
    );
  }
}

// ============================================================================
// 4. 卡片
// ============================================================================

/// 令牌化卡片：静止 z1 阴影，可选悬停抬升。
class Gs3dCard extends StatelessWidget {
  const Gs3dCard({
    super.key,
    required this.child,
    this.padding = const EdgeInsets.all(Gs3dTokens.space16),
    this.margin,
    this.onTap,
    this.interactive = false,
    this.accentBorder = false,
    this.radius = Gs3dTokens.radiusLg,
  });

  final Widget child;
  final EdgeInsetsGeometry padding;
  final EdgeInsetsGeometry? margin;
  final VoidCallback? onTap;

  /// 悬停时抬升并轻微放大。
  final bool interactive;

  /// 是否使用强调色描边（用于选中/主卡片）。
  final bool accentBorder;

  final double radius;

  @override
  Widget build(BuildContext context) {
    final a11y = context.gs3dA11y;
    Widget card = Container(
      margin: margin,
      padding: padding,
      decoration: BoxDecoration(
        color: Gs3dTokens.surface,
        borderRadius: BorderRadius.circular(radius),
        border: Border.all(
          color: accentBorder ? Gs3dTokens.accentBorder : Gs3dTokens.border,
        ),
        boxShadow: Gs3dTokens.elevation1,
      ),
      child: child,
    );

    if (!interactive) return card;

    return Gs3dPressable(
      onTap: onTap,
      hoverScale: 0.006,
      builder: (context, state) => AnimatedContainer(
        duration: a11y.resolve(Gs3dMotion.micro),
        curve: Gs3dMotion.standardCurve,
        decoration: BoxDecoration(
          color: state == Gs3dInteractionState.idle
              ? Gs3dTokens.surface
              : Gs3dTokens.surfaceHover,
          borderRadius: BorderRadius.circular(radius),
          border: Border.all(
            color: state == Gs3dInteractionState.idle
                ? (accentBorder ? Gs3dTokens.accentBorder : Gs3dTokens.border)
                : Gs3dTokens.accentBorder,
          ),
          boxShadow: state == Gs3dInteractionState.idle
              ? Gs3dTokens.elevation1
              : Gs3dTokens.elevation2,
        ),
        padding: padding,
        margin: margin,
        child: child,
      ),
    );
  }
}

// ============================================================================
// 5. 菜单项 / 分组标签
// ============================================================================

/// 统一菜单行：图标 + 标签 + 可选快捷键提示 + 可选勾选态。
///
/// 取代各处重复的 `Row(Icon, SizedBox, Text)`，确保 8pt 栅格、对比度与
/// 语义层级一致。
class Gs3dMenuItem extends StatelessWidget {
  const Gs3dMenuItem({
    super.key,
    required this.label,
    this.icon,
    this.iconColor,
    this.shortcut,
    this.checked,
    this.enabled = true,
    this.trailing,
  });

  final String label;
  final IconData? icon;
  final Color? iconColor;
  final String? shortcut;
  final bool? checked;
  final bool enabled;
  final Widget? trailing;

  @override
  Widget build(BuildContext context) {
    final fg = enabled ? Gs3dTokens.textPrimary : Gs3dTokens.textDisabled;
    return Row(
      children: [
        if (icon != null) ...[
          Icon(
            checked == null
                ? icon
                : (checked! ? Icons.check_box_rounded : Icons.check_box_outline_blank_rounded),
            size: 15,
            color: enabled
                ? (iconColor ?? Gs3dTokens.textSecondary)
                : Gs3dTokens.textDisabled,
          ),
          const SizedBox(width: Gs3dTokens.space8),
        ],
        Expanded(
          child: Text(
            label,
            overflow: TextOverflow.ellipsis,
            style: Gs3dType.caption.copyWith(
              fontSize: 12.5,
              color: fg,
              fontWeight: FontWeight.w500,
            ),
          ),
        ),
        ?trailing,
        if (shortcut != null) ...[
          const SizedBox(width: Gs3dTokens.space16),
          Text(
            shortcut!,
            style: Gs3dType.micro.copyWith(
              color: Gs3dTokens.textTertiary,
              fontFamily: Gs3dType.mono.fontFamily,
              fontWeight: FontWeight.w500,
            ),
          ),
        ],
      ],
    );
  }
}

/// 分区小标签（全大写 + 放宽字距）。
class Gs3dSectionLabel extends StatelessWidget {
  const Gs3dSectionLabel(this.text, {super.key, this.trailing});

  final String text;
  final Widget? trailing;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(
        left: Gs3dTokens.space2,
        bottom: Gs3dTokens.space8,
      ),
      child: Row(
        children: [
          Expanded(child: Text(text, style: Gs3dType.overline)),
          ?trailing,
        ],
      ),
    );
  }
}

/// 细发丝分隔线。
class Gs3dDivider extends StatelessWidget {
  const Gs3dDivider({super.key, this.height = Gs3dTokens.space16});

  final double height;

  @override
  Widget build(BuildContext context) => SizedBox(
    height: height,
    child: const Center(child: Divider(height: 1, thickness: 1)),
  );
}

// ============================================================================
// 6. 胶囊按钮 / 状态点
// ============================================================================

/// 令牌化胶囊按钮（工具栏 / 快捷操作）。
class Gs3dPillButton extends StatelessWidget {
  const Gs3dPillButton({
    super.key,
    required this.label,
    this.icon,
    this.onPressed,
    this.active = false,
    this.tooltip,
    this.dense = false,
  });

  final String label;
  final IconData? icon;
  final VoidCallback? onPressed;
  final bool active;
  final String? tooltip;
  final bool dense;

  @override
  Widget build(BuildContext context) {
    final enabled = onPressed != null;
    final fg = !enabled
        ? Gs3dTokens.textDisabled
        : (active ? Gs3dTokens.accent : Gs3dTokens.textBody);

    final button = Gs3dPressable(
      onTap: onPressed,
      hoverScale: 0.012,
      pressScale: 0.03,
      builder: (context, state) {
        final bg = switch (state) {
          Gs3dInteractionState.pressed => active
              ? Gs3dTokens.accentSubtle
              : Gs3dTokens.surfaceSunken,
          Gs3dInteractionState.hovered =>
            active ? Gs3dTokens.accentSubtle : Gs3dTokens.surfaceHover,
          Gs3dInteractionState.idle =>
            active ? Gs3dTokens.accentSubtle : Colors.transparent,
        };
        return AnimatedContainer(
          duration: Gs3dMotion.micro,
          curve: Gs3dMotion.standardCurve,
          padding: EdgeInsets.symmetric(
            horizontal: dense ? Gs3dTokens.space8 : Gs3dTokens.space12,
            vertical: dense ? Gs3dTokens.space4 : 6,
          ),
          decoration: BoxDecoration(
            color: bg,
            borderRadius: BorderRadius.circular(Gs3dTokens.radiusMd),
            border: Border.all(
              color: active ? Gs3dTokens.accentBorder : Colors.transparent,
            ),
          ),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              if (icon != null) ...[
                Icon(icon, size: dense ? 14 : 15, color: fg),
                const SizedBox(width: 6),
              ],
              Text(
                label,
                style: TextStyle(
                  fontSize: dense ? 12 : 12.5,
                  fontWeight: active ? FontWeight.w600 : FontWeight.w500,
                  color: fg,
                ),
              ),
            ],
          ),
        );
      },
    );

    return tooltip == null ? button : Tooltip(message: tooltip!, child: button);
  }
}

/// 带脉冲的状态指示点。
class Gs3dStatusDot extends StatelessWidget {
  const Gs3dStatusDot({
    super.key,
    required this.color,
    this.size = 7,
    this.pulse = false,
    this.label,
  });

  final Color color;
  final double size;
  final bool pulse;
  final String? label;

  @override
  Widget build(BuildContext context) {
    final dot = Container(
      width: size,
      height: size,
      decoration: BoxDecoration(
        color: color,
        shape: BoxShape.circle,
        boxShadow: [BoxShadow(color: color.withValues(alpha: 0.4), blurRadius: 6)],
      ),
    );

    final wrapped = pulse && !context.gs3dA11y.reduceMotion
        ? _PulsingDot(color: color, size: size)
        : dot;

    if (label == null) return wrapped;
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        wrapped,
        const SizedBox(width: 6),
        Text(label!, style: Gs3dType.micro.copyWith(color: Gs3dTokens.textSecondary)),
      ],
    );
  }
}

class _PulsingDot extends StatefulWidget {
  const _PulsingDot({required this.color, required this.size});

  final Color color;
  final double size;

  @override
  State<_PulsingDot> createState() => _PulsingDotState();
}

class _PulsingDotState extends State<_PulsingDot>
    with SingleTickerProviderStateMixin {
  late final AnimationController _c = AnimationController(
    vsync: this,
    duration: const Duration(milliseconds: 1800),
  )..repeat(reverse: true);

  @override
  void dispose() {
    _c.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: _c,
      builder: (context, _) {
        final t = Curves.easeInOut.transform(_c.value);
        return Container(
          width: widget.size,
          height: widget.size,
          decoration: BoxDecoration(
            color: widget.color.withValues(alpha: 0.75 + 0.25 * t),
            shape: BoxShape.circle,
            boxShadow: [
              BoxShadow(
                color: widget.color.withValues(alpha: 0.18 + 0.32 * t),
                blurRadius: 5 + 5 * t,
              ),
            ],
          ),
        );
      },
    );
  }
}

// ============================================================================
// 7. 指标磁贴
// ============================================================================

/// 紧凑指标磁贴：标签 + 数值 + 可选单位，用于状态栏 / 性能面板。
class Gs3dMetricTile extends StatelessWidget {
  const Gs3dMetricTile({
    super.key,
    required this.label,
    required this.value,
    this.unit,
    this.color,
    this.icon,
    this.dense = false,
  });

  final String label;
  final String value;
  final String? unit;
  final Color? color;
  final IconData? icon;
  final bool dense;

  @override
  Widget build(BuildContext context) {
    final valueColor = color ?? Gs3dTokens.textPrimary;
    return Container(
      padding: EdgeInsets.symmetric(
        horizontal: dense ? Gs3dTokens.space8 : Gs3dTokens.space12,
        vertical: dense ? Gs3dTokens.space4 : Gs3dTokens.space8,
      ),
      decoration: BoxDecoration(
        color: Gs3dTokens.surfaceSunken,
        borderRadius: BorderRadius.circular(Gs3dTokens.radiusMd),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        mainAxisSize: MainAxisSize.min,
        children: [
          Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              if (icon != null) ...[
                Icon(icon, size: 11, color: Gs3dTokens.textTertiary),
                const SizedBox(width: 4),
              ],
              Text(label, style: Gs3dType.overline),
            ],
          ),
          const SizedBox(height: 2),
          Row(
            crossAxisAlignment: CrossAxisAlignment.baseline,
            textBaseline: TextBaseline.alphabetic,
            mainAxisSize: MainAxisSize.min,
            children: [
              Text(
                value,
                style: Gs3dType.mono.copyWith(
                  fontSize: dense ? 12.5 : 15,
                  color: valueColor,
                  fontWeight: FontWeight.w700,
                ),
              ),
              if (unit != null) ...[
                const SizedBox(width: 3),
                Text(unit!, style: Gs3dType.micro),
              ],
            ],
          ),
        ],
      ),
    );
  }
}

// ============================================================================
// 8. 弹簧模态路由（Apple 风格 Sheet 呈现）
// ============================================================================

/// 弹簧驱动的模态对话框路由。
///
/// 入场：从触发点方向轻微上浮 + 缩放，使用 [Gs3dMotion.sheet] 弹簧；
/// 出场对称。遮罩淡入淡出。遵守「减弱动效」偏好。
class Gs3dDialogRoute<T> extends PopupRoute<T> {
  Gs3dDialogRoute({
    required this.builder,
    this.barrierLabel = '关闭',
    this.barrierColor = const Color(0x33000000),
  });

  final WidgetBuilder builder;

  @override
  final String barrierLabel;

  @override
  final Color? barrierColor;

  @override
  bool get barrierDismissible => true;

  @override
  bool get semanticsDismissible => true;

  @override
  Duration get transitionDuration =>
      const Duration(milliseconds: 320);

  @override
  Widget buildPage(
    BuildContext context,
    Animation<double> animation,
    Animation<double> secondaryAnimation,
  ) {
    return builder(context);
  }

  @override
  Widget buildTransitions(
    BuildContext context,
    Animation<double> animation,
    Animation<double> secondaryAnimation,
    Widget child,
  ) {
    final a11y = Gs3dA11yScope.maybeOf(context) ?? Gs3dA11y.standard;
    if (a11y.reduceMotion) return child;

    final curved = CurvedAnimation(
      parent: animation,
      curve: Gs3dSpringCurve(Gs3dMotion.sheet),
      reverseCurve: Curves.easeInCubic,
    );
    return FadeTransition(
      opacity: animation.drive(Tween<double>(begin: 0, end: 1)),
      child: AnimatedBuilder(
        animation: curved,
        child: child,
        builder: (context, c) {
          final t = curved.value;
          return Opacity(
            opacity: t.clamp(0.0, 1.0),
            child: Transform.translate(
              offset: Offset(0, (1 - t) * 14),
              child: Transform.scale(scale: 0.975 + 0.025 * t, child: c),
            ),
          );
        },
      ),
    );
  }
}
