import 'package:flutter/material.dart';
import '../theme/gs3d_tokens.dart';

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
    return Container(
      width: double.infinity,
      padding: padding,
      decoration: BoxDecoration(
        color: Gs3dTokens.surface,
        borderRadius: BorderRadius.circular(Gs3dTokens.cardRadius),
        border: Border.all(color: Gs3dTokens.border, width: 1),
        boxShadow: Gs3dTokens.elevation1,
      ),
      child: child,
    );
  }
}

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
    return Container(
      width: double.infinity,
      decoration: BoxDecoration(
        color: Gs3dTokens.surface,
        borderRadius: BorderRadius.circular(Gs3dTokens.cardRadius),
        border: Border.all(color: Gs3dTokens.border, width: 1),
        boxShadow: Gs3dTokens.elevation1,
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          InkWell(
            borderRadius: BorderRadius.circular(Gs3dTokens.cardRadius),
            onTap: () {
              setState(() {
                _isOpen = !_isOpen;
              });
            },
            child: Padding(
              padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
              child: Row(
                children: [
                  Icon(
                    _isOpen
                        ? Icons.keyboard_arrow_down_rounded
                        : Icons.keyboard_arrow_right_rounded,
                    color: Gs3dTokens.accent,
                    size: 20,
                  ),
                  const SizedBox(width: 6),
                  Expanded(
                    child: Text(
                      widget.title,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: const TextStyle(
                        fontSize: 14,
                        fontWeight: FontWeight.w700,
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
          AnimatedCrossFade(
            duration: const Duration(milliseconds: 180),
            crossFadeState:
                _isOpen ? CrossFadeState.showFirst : CrossFadeState.showSecond,
            firstChild: Padding(
              padding: const EdgeInsets.only(left: 14, right: 14, bottom: 12),
              child: widget.child,
            ),
            secondChild: const SizedBox(width: double.infinity),
          ),
        ],
      ),
    );
  }
}
