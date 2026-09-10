import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../theme/gs3d_tokens.dart';
import 'fluid.dart';

/// 通用卡片基座。
///
/// 直接委托给令牌化 [Gs3dCard]，保证全应用统一的圆角、描边与 z1 阴影，
/// 不再各自拼装 `BoxDecoration`。
class ModernCard extends StatelessWidget {
  final Widget child;
  final EdgeInsetsGeometry padding;

  const ModernCard({
    super.key,
    required this.child,
    this.padding = const EdgeInsets.symmetric(horizontal: 14, vertical: 12),
  });

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: double.infinity,
      child: Gs3dCard(padding: padding, child: child),
    );
  }
}

/// 可折叠卡片。
///
/// 展开 / 收起使用**可中断弹簧**（快速连点会从当前位置续接，不重排），
/// 折叠箭头由同一弹簧驱动旋转，头部按下即时响应、悬停浮现底色。
class CollapsibleCard extends StatefulWidget {
  final String title;
  final bool initialOpen;
  final Widget child;
  final Widget? trailing;

  const CollapsibleCard({
    super.key,
    required this.title,
    this.initialOpen = true,
    required this.child,
    this.trailing,
  });

  @override
  State<CollapsibleCard> createState() => _CollapsibleCardState();
}

class _CollapsibleCardState extends State<CollapsibleCard> {
  late bool _isOpen;

  @override
  void initState() {
    super.initState();
    _isOpen = widget.initialOpen;
  }

  @override
  Widget build(BuildContext context) {
    final a11y = context.gs3dA11y;
    final radius = BorderRadius.circular(Gs3dTokens.cardRadius);

    return Gs3dCard(
      padding: EdgeInsets.zero,
      radius: Gs3dTokens.cardRadius,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // 头部：按压即时收缩 + 悬停底色，折叠箭头弹簧旋转。
          Gs3dPressable(
            onTap: () => setState(() => _isOpen = !_isOpen),
            builder: (context, state) => AnimatedContainer(
              duration: a11y.resolve(Gs3dMotion.micro),
              curve: Gs3dMotion.standardCurve,
              decoration: BoxDecoration(
                color: state == Gs3dInteractionState.idle
                    ? Colors.transparent
                    : Gs3dTokens.surfaceHover,
                borderRadius: radius,
              ),
              padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
              child: Row(
                children: [
                  Gs3dSpringValue(
                    value: _isOpen ? 1.0 : 0.0,
                    spring: a11y.resolveSpring(Gs3dMotion.rotate),
                    builder: (context, t, child) => Transform.rotate(
                      // 收起 → 箭头指向右（-90°）；展开 → 指向下（0°）。
                      angle: (1 - t) * -math.pi / 2,
                      child: child,
                    ),
                    child: const Icon(
                      Icons.keyboard_arrow_down_rounded,
                      color: Gs3dTokens.accent,
                      size: 20,
                    ),
                  ),
                  const SizedBox(width: 6),
                  Expanded(
                    child: Text(
                      widget.title,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: Gs3dType.bodyStrong.copyWith(
                        fontSize: 14,
                        color: Gs3dTokens.textPrimary,
                      ),
                    ),
                  ),
                  if (widget.trailing != null) ...[
                    const SizedBox(width: 6),
                    widget.trailing!,
                  ],
                ],
              ),
            ),
          ),

          // 内容：AnimatedSize 走标准缓动，高度变化平滑不跳动。
          AnimatedSize(
            duration: a11y.resolve(Gs3dMotion.normal),
            curve: Gs3dMotion.standardCurve,
            alignment: Alignment.topCenter,
            child: _isOpen
                ? Padding(
                    padding: const EdgeInsets.only(
                      left: 14,
                      right: 14,
                      bottom: 12,
                    ),
                    child: widget.child,
                  )
                : const SizedBox(width: double.infinity, height: 0),
          ),
        ],
      ),
    );
  }
}
