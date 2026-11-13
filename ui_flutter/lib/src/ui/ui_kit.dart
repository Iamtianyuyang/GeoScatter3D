import 'package:flutter/material.dart';

import '../theme/app_tokens.dart';

/// 面板里的通用部件：发丝线、字段、分段控件、读数行。
///
/// 原则（与设计方向一致）：一层底色、一条线、一个信号色；不用卡片套卡片，
/// 不堆阴影，只靠留白与字重区分层级。

class Hairline extends StatelessWidget {
  const Hairline({super.key, this.indent = 0});

  final double indent;

  @override
  Widget build(BuildContext context) {
    final colors = context.tokens.colors;
    return Container(
      height: 1,
      margin: EdgeInsets.only(left: indent),
      color: colors.line,
    );
  }
}

/// 抽屉顶部的标题条：标题 + 可选说明 + 关闭。
class PanelHeader extends StatelessWidget {
  const PanelHeader({
    super.key,
    required this.title,
    this.subtitle,
    this.onClose,
    this.trailing,
  });

  final String title;
  final String? subtitle;
  final VoidCallback? onClose;
  final Widget? trailing;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    return Container(
      padding: const EdgeInsets.fromLTRB(16, 14, 8, 14),
      decoration: BoxDecoration(
        border: Border(bottom: BorderSide(color: tokens.colors.line)),
      ),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.center,
        children: [
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              mainAxisSize: MainAxisSize.min,
              children: [
                Text(
                  title,
                  style: tokens.type.panelTitle.copyWith(
                    color: tokens.colors.textHigh,
                  ),
                ),
                if (subtitle != null) ...[
                  const SizedBox(height: 2),
                  Text(
                    subtitle!,
                    style: tokens.type.meta.copyWith(
                      color: tokens.colors.textLow,
                    ),
                  ),
                ],
              ],
            ),
          ),
          if (trailing != null) trailing!,
          if (onClose != null)
            IconButton(
              tooltip: '关闭面板',
              onPressed: onClose,
              iconSize: 18,
              visualDensity: VisualDensity.compact,
              color: tokens.colors.textLow,
              icon: const Icon(Icons.close),
            ),
        ],
      ),
    );
  }
}

/// 分组标题 + 内容，自带上下呼吸。
class PanelSection extends StatelessWidget {
  const PanelSection({
    super.key,
    this.title,
    this.trailing,
    required this.child,
    this.padding = const EdgeInsets.fromLTRB(16, 4, 16, 16),
  });

  final String? title;
  final Widget? trailing;
  final Widget child;
  final EdgeInsets padding;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    return Padding(
      padding: padding,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        mainAxisSize: MainAxisSize.min,
        children: [
          if (title != null) ...[
            const SizedBox(height: 12),
            Row(
              children: [
                Expanded(
                  child: Text(
                    title!.toUpperCase(),
                    style: tokens.type.sectionLabel.copyWith(
                      color: tokens.colors.textLow,
                    ),
                  ),
                ),
                if (trailing != null) trailing!,
              ],
            ),
            const SizedBox(height: 10),
          ],
          child,
        ],
      ),
    );
  }
}

/// 左标签 / 右读数，两行时第二行折行，数值等宽右对齐。
class KeyValueRow extends StatelessWidget {
  const KeyValueRow({
    super.key,
    required this.label,
    required this.value,
    this.numeric = false,
    this.valueColor,
    this.maxLines = 2,
  });

  final String label;
  final String value;
  final bool numeric;
  final Color? valueColor;
  final int maxLines;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    final style = (numeric ? tokens.type.numeric : tokens.type.body).copyWith(
      color: valueColor ?? tokens.colors.textHigh,
    );
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 3),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          SizedBox(
            width: 76,
            child: Text(
              label,
              style: tokens.type.meta.copyWith(color: tokens.colors.textLow),
            ),
          ),
          const SizedBox(width: 8),
          Expanded(
            child: Text(
              value,
              maxLines: maxLines,
              overflow: TextOverflow.ellipsis,
              textAlign: TextAlign.right,
              style: style,
            ),
          ),
        ],
      ),
    );
  }
}

class FieldLabel extends StatelessWidget {
  const FieldLabel({super.key, required this.text, this.help});

  final String text;
  final String? help;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    return Padding(
      padding: const EdgeInsets.only(bottom: 6),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.baseline,
        textBaseline: TextBaseline.alphabetic,
        children: [
          Text(
            text,
            style: tokens.type.meta.copyWith(color: tokens.colors.textMid),
          ),
          if (help != null) ...[
            const SizedBox(width: 8),
            Expanded(
              child: Text(
                help!,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: tokens.type.meta.copyWith(
                  color: tokens.colors.textLow,
                  fontSize: 11,
                ),
              ),
            ),
          ],
        ],
      ),
    );
  }
}

/// 分段选择：一条边线框住若干互斥项，选中项用信号色的低透明度底。
class Segmented<T> extends StatelessWidget {
  const Segmented({
    super.key,
    required this.options,
    required this.value,
    required this.onChanged,
    this.expand = true,
  });

  final List<(T, String)> options;
  final T value;
  final ValueChanged<T>? onChanged;
  final bool expand;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    final children = <Widget>[];
    for (var i = 0; i < options.length; i++) {
      final (option, label) = options[i];
      final selected = option == value;
      if (i > 0) {
        children.add(Container(width: 1, color: tokens.colors.line));
      }
      children.add(
        Expanded(
          flex: expand ? 1 : 0,
          child: GestureDetector(
            behavior: HitTestBehavior.opaque,
            onTap: onChanged == null ? null : () => onChanged!(option),
            child: AnimatedContainer(
              duration: tokens.motion.fast,
              curve: tokens.motion.ease,
              height: 30,
              alignment: Alignment.center,
              padding: const EdgeInsets.symmetric(horizontal: 10),
              color: selected ? tokens.colors.signalSoft : Colors.transparent,
              child: Text(
                label,
                style: tokens.type.meta.copyWith(
                  color: selected
                      ? tokens.colors.signal
                      : tokens.colors.textMid,
                  fontWeight: selected ? FontWeight.w700 : FontWeight.w400,
                ),
              ),
            ),
          ),
        ),
      );
    }
    return Container(
      decoration: BoxDecoration(
        border: Border.all(color: tokens.colors.line),
        borderRadius: tokens.radii.smAll,
      ),
      clipBehavior: Clip.antiAlias,
      child: Row(children: children),
    );
  }
}

/// 单行下拉：选择项不多时比拼多按钮省地方。
class SelectField<T> extends StatelessWidget {
  const SelectField({
    super.key,
    required this.label,
    required this.value,
    required this.items,
    required this.onChanged,
    this.dropdownKey,
  });

  final String label;
  final T value;
  final List<(T, String)> items;
  final ValueChanged<T?>? onChanged;
  final Key? dropdownKey;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        FieldLabel(text: label),
        Container(
          height: 32,
          padding: const EdgeInsets.symmetric(horizontal: 8),
          decoration: BoxDecoration(
            border: Border.all(color: tokens.colors.line),
            borderRadius: tokens.radii.smAll,
            color: tokens.colors.raised,
          ),
          child: DropdownButtonHideUnderline(
            child: DropdownButton<T>(
              key: dropdownKey,
              value: value,
              isExpanded: true,
              isDense: true,
              borderRadius: tokens.radii.mdAll,
              dropdownColor: tokens.colors.raised,
              icon: Icon(
                Icons.keyboard_arrow_down,
                size: 16,
                color: tokens.colors.textLow,
              ),
              style: tokens.type.body.copyWith(color: tokens.colors.textHigh),
              items: [
                for (final (value, text) in items)
                  DropdownMenuItem<T>(value: value, child: Text(text)),
              ],
              onChanged: onChanged,
            ),
          ),
        ),
      ],
    );
  }
}

/// 标签 + 数值 + 滑块；拖动中只更新本地预览，松手才提交。
class SliderField extends StatelessWidget {
  const SliderField({
    super.key,
    required this.label,
    required this.value,
    required this.min,
    required this.max,
    required this.display,
    required this.onChanged,
    this.onChangeEnd,
    this.divisions,
    this.sliderKey,
  });

  final String label;
  final double value;
  final double min;
  final double max;
  final String display;
  final ValueChanged<double>? onChanged;
  final ValueChanged<double>? onChangeEnd;
  final int? divisions;
  final Key? sliderKey;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Row(
          children: [
            Expanded(
              child: Text(
                label,
                style: tokens.type.meta.copyWith(color: tokens.colors.textMid),
              ),
            ),
            Text(
              display,
              style: tokens.type.numeric.copyWith(color: tokens.colors.textHigh),
            ),
          ],
        ),
        SizedBox(
          height: 28,
          child: Slider(
            key: sliderKey,
            value: value.clamp(min, max),
            min: min,
            max: max,
            divisions: divisions,
            onChanged: onChanged,
            onChangeEnd: onChangeEnd,
          ),
        ),
      ],
    );
  }
}

class ToggleRow extends StatelessWidget {
  const ToggleRow({
    super.key,
    required this.label,
    required this.value,
    required this.onChanged,
    this.help,
  });

  final String label;
  final bool value;
  final ValueChanged<bool>? onChanged;
  final String? help;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    return InkWell(
      onTap: onChanged == null ? null : () => onChanged!(!value),
      borderRadius: tokens.radii.smAll,
      child: Padding(
        padding: const EdgeInsets.symmetric(vertical: 4),
        child: Row(
          children: [
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                mainAxisSize: MainAxisSize.min,
                children: [
                  Text(
                    label,
                    style: tokens.type.body.copyWith(color: tokens.colors.textHigh),
                  ),
                  if (help != null)
                    Text(
                      help!,
                      style: tokens.type.meta.copyWith(
                        color: tokens.colors.textLow,
                        fontSize: 11,
                      ),
                    ),
                ],
              ),
            ),
            Switch(
              value: value,
              onChanged: onChanged,
              materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
            ),
          ],
        ),
      ),
    );
  }
}

enum ButtonTone { primary, ghost, danger }

/// 平面按钮：主操作是实心信号色，其余只描边。
class AppButton extends StatelessWidget {
  const AppButton({
    super.key,
    required this.label,
    this.icon,
    this.onPressed,
    this.tone = ButtonTone.ghost,
    this.expand = true,
    this.dense = false,
    this.buttonKey,
  });

  final String label;
  final IconData? icon;
  final VoidCallback? onPressed;
  final ButtonTone tone;
  final bool expand;
  final bool dense;
  final Key? buttonKey;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    final colors = tokens.colors;
    final enabled = onPressed != null;
    final (background, foreground, border) = switch (tone) {
      ButtonTone.primary => (
        enabled ? colors.signal : colors.lineStrong,
        enabled ? const Color(0xFF04120D) : colors.textLow,
        Colors.transparent,
      ),
      ButtonTone.danger => (
        enabled ? colors.alert.withValues(alpha: 0.12) : colors.raised,
        enabled ? colors.alert : colors.textLow,
        enabled ? colors.alert.withValues(alpha: 0.5) : colors.line,
      ),
      ButtonTone.ghost => (
        colors.raised,
        enabled ? colors.textHigh : colors.textLow,
        colors.line,
      ),
    };
    final child = Row(
      mainAxisSize: expand ? MainAxisSize.max : MainAxisSize.min,
      mainAxisAlignment: MainAxisAlignment.center,
      children: [
        if (icon != null) ...[
          Icon(icon, size: dense ? 14 : 16, color: foreground),
          const SizedBox(width: 6),
        ],
        Flexible(
          child: Text(
            label,
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: (dense ? tokens.type.meta : tokens.type.body).copyWith(
              color: foreground,
              fontWeight: FontWeight.w700,
            ),
          ),
        ),
      ],
    );
    return Material(
      color: background,
      borderRadius: tokens.radii.smAll,
      child: InkWell(
        key: buttonKey,
        onTap: onPressed,
        borderRadius: tokens.radii.smAll,
        child: Container(
          height: dense ? 28 : 34,
          padding: const EdgeInsets.symmetric(horizontal: 12),
          decoration: BoxDecoration(
            border: Border.all(color: border),
            borderRadius: tokens.radii.smAll,
          ),
          alignment: Alignment.center,
          child: child,
        ),
      ),
    );
  }
}

/// 提示条：失败用警示色、进行中用信号色，不用满屏红色弹窗。
class NoticeBanner extends StatelessWidget {
  const NoticeBanner({
    super.key,
    required this.text,
    this.tone = ButtonTone.ghost,
    this.icon,
  });

  final String text;
  final ButtonTone tone;
  final IconData? icon;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    final color = switch (tone) {
      ButtonTone.danger => tokens.colors.alert,
      ButtonTone.primary => tokens.colors.signal,
      ButtonTone.ghost => tokens.colors.textMid,
    };
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.08),
        border: Border.all(color: color.withValues(alpha: 0.35)),
        borderRadius: tokens.radii.smAll,
      ),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Icon(icon ?? Icons.info_outline, size: 14, color: color),
          const SizedBox(width: 8),
          Expanded(
            child: Text(
              text,
              style: tokens.type.meta.copyWith(color: tokens.colors.textHigh),
            ),
          ),
        ],
      ),
    );
  }
}

/// 空状态：说明为什么空、下一步做什么。
class EmptyHint extends StatelessWidget {
  const EmptyHint({
    super.key,
    required this.text,
    this.hint,
    this.icon = Icons.inbox_outlined,
    this.action,
  });

  final String text;
  final String? hint;
  final IconData icon;
  final Widget? action;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 18),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(icon, size: 15, color: tokens.colors.textLow),
              const SizedBox(width: 8),
              Expanded(
                child: Text(
                  text,
                  style: tokens.type.body.copyWith(color: tokens.colors.textMid),
                ),
              ),
            ],
          ),
          if (hint != null) ...[
            const SizedBox(height: 4),
            Padding(
              padding: const EdgeInsets.only(left: 23),
              child: Text(
                hint!,
                style: tokens.type.meta.copyWith(color: tokens.colors.textLow),
              ),
            ),
          ],
          if (action != null) ...[
            const SizedBox(height: 12),
            Padding(
              padding: const EdgeInsets.only(left: 23),
              child: action!,
            ),
          ],
        ],
      ),
    );
  }
}

/// 进度：管线阶段、导出这类耗时动作共用一条细线。
class ThinProgress extends StatelessWidget {
  const ThinProgress({super.key, this.value, this.tone});

  /// null 表示不确定进度。
  final double? value;
  final Color? tone;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    return ClipRRect(
      borderRadius: BorderRadius.circular(1),
      child: LinearProgressIndicator(
        value: value == null || value! <= 0 ? null : value!.clamp(0, 1),
        minHeight: 2,
        backgroundColor: tokens.colors.line,
        valueColor: AlwaysStoppedAnimation(
          tone ?? tokens.colors.signal,
        ),
      ),
    );
  }
}

/// 抽屉内容统一内边距与滚动。
class PanelBody extends StatelessWidget {
  const PanelBody({super.key, required this.children, this.controller});

  final List<Widget> children;
  final ScrollController? controller;

  @override
  Widget build(BuildContext context) {
    return ListView(
      controller: controller,
      padding: const EdgeInsets.only(bottom: 24),
      children: children,
    );
  }
}
