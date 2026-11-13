import 'package:flutter/material.dart';

import '../ffi/geoscatter3d_service.dart';
import '../ffi/render_settings.dart';
import '../theme/app_tokens.dart';
import '../ui/format.dart';
import '../ui/ui_kit.dart';

/// 配色名与引擎的 colormap 索引一一对应（0..8）。
const List<(int, String)> kColormaps = [
  (0, '地形'),
  (1, 'Viridis'),
  (2, 'Jet'),
  (3, '灰度'),
  (4, '热力'),
  (5, 'Coolwarm'),
  (6, 'Turbo'),
  (7, 'Plasma'),
  (8, 'Rainbow'),
];

const List<(int, String)> kPointShapes = [
  (0, '圆点'),
  (1, '方块'),
  (2, '菱形'),
  (3, '三角'),
];

const List<(int, String)> kBackgrounds = [
  (0x0B1015, '深空'),
  (0x000000, '纯黑'),
  (0x1E2A33, '石墨'),
];

const List<(int, String)> kBudgets = [
  (0, '自动'),
  (1000000, '100 万'),
  (2000000, '200 万'),
  (5000000, '500 万'),
  (10000000, '1000 万'),
  (25000000, '2500 万'),
  (50000000, '5000 万'),
];

/// 渲染面板：外观、几何映射与预算。
///
/// 滑块拖动期间只改本地预览，松手（onChangeEnd）才下发引擎并落盘：
/// 50M 点上每次重绘都可能吃掉一帧，逐像素提交会明显拖慢拖动。
class RenderPanel extends StatefulWidget {
  const RenderPanel({super.key, required this.service});

  final GeoScatter3dService service;

  @override
  State<RenderPanel> createState() => _RenderPanelState();
}

class _RenderPanelState extends State<RenderPanel> {
  double? _draggingPointSize;
  double? _draggingExaggeration;
  double? _draggingClipMin;
  double? _draggingClipMax;

  void _apply(RenderSettings next) {
    widget.service.updateRenderSettings(next);
    widget.service.saveRenderSettings().catchError((Object error) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('保存显示设置失败：$error')),
      );
    });
  }

  (double, double) _clipBounds(RenderSettings settings) {
    final info = widget.service.datasetInfo;
    final min = (info['value_min'] as num?)?.toDouble() ??
        settings.valueClipMin ??
        0.0;
    final max = (info['value_max'] as num?)?.toDouble() ??
        settings.valueClipMax ??
        1.0;
    return max > min ? (min, max) : (min, min + 1.0);
  }

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    return ListenableBuilder(
      listenable: widget.service,
      builder: (context, _) {
        final settings = widget.service.renderSettings;
        final disabled = !widget.service.ready;
        final (clipMin, clipMax) = _clipBounds(settings);
        final currentMin = _draggingClipMin ?? settings.valueClipMin ?? clipMin;
        final currentMax = _draggingClipMax ?? settings.valueClipMax ?? clipMax;
        final step = (clipMax - clipMin) / 200;

        return ListView(
          padding: const EdgeInsets.only(bottom: 24),
          children: [
            if (disabled)
              const PanelSection(
                padding: EdgeInsets.fromLTRB(16, 12, 16, 0),
                child: NoticeBanner(text: '引擎未就绪，设置会在启动后应用。'),
              ),
            PanelSection(
              title: '外观',
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  SelectField<int>(
                    label: '配色',
                    dropdownKey: const ValueKey('colormap_dropdown'),
                    value: settings.colormapIndex,
                    items: kColormaps,
                    onChanged: (value) {
                      if (value == null) return;
                      _apply(settings.copyWith(colormapIndex: value));
                    },
                  ),
                  const SizedBox(height: 12),
                  SliderField(
                    label: '点大小',
                    sliderKey: const ValueKey('point_size_slider'),
                    value: _draggingPointSize ?? settings.pointSize,
                    min: RenderSettings.minPointSize,
                    max: RenderSettings.maxPointSize,
                    divisions: 30,
                    display: (_draggingPointSize ?? settings.pointSize)
                        .toStringAsFixed(1),
                    onChanged: (value) =>
                        setState(() => _draggingPointSize = value),
                    onChangeEnd: (value) {
                      setState(() => _draggingPointSize = null);
                      _apply(settings.copyWith(pointSize: value));
                    },
                  ),
                  const SizedBox(height: 8),
                  const FieldLabel(text: '点形状'),
                  Segmented<int>(
                    value: settings.pointShape,
                    options: kPointShapes,
                    onChanged: (value) =>
                        _apply(settings.copyWith(pointShape: value)),
                  ),
                  const SizedBox(height: 14),
                  const FieldLabel(text: '背景'),
                  Segmented<int>(
                    value: settings.background,
                    options: kBackgrounds,
                    onChanged: (value) =>
                        _apply(settings.copyWith(background: value)),
                  ),
                ],
              ),
            ),
            const Hairline(),
            PanelSection(
              title: '高程与着色',
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  const FieldLabel(text: '高程来源'),
                  Segmented<int>(
                    value: settings.heightSource,
                    options: const [(0, 'Z 坐标'), (1, '属性值')],
                    onChanged: (value) =>
                        _apply(settings.copyWith(heightSource: value)),
                  ),
                  const SizedBox(height: 14),
                  const FieldLabel(text: '颜色来源'),
                  Segmented<int>(
                    value: settings.colorSource,
                    options: const [(0, '高程'), (1, '属性值')],
                    onChanged: (value) =>
                        _apply(settings.copyWith(colorSource: value)),
                  ),
                  const SizedBox(height: 12),
                  SliderField(
                    label: '高程夸张',
                    value: _draggingExaggeration ?? settings.heightExaggeration,
                    min: RenderSettings.minHeightExaggeration,
                    max: RenderSettings.maxHeightExaggeration,
                    divisions: 40,
                    display:
                        '×${(_draggingExaggeration ?? settings.heightExaggeration).toStringAsFixed(1)}',
                    onChanged: (value) =>
                        setState(() => _draggingExaggeration = value),
                    onChangeEnd: (value) {
                      setState(() => _draggingExaggeration = null);
                      _apply(settings.copyWith(heightExaggeration: value));
                    },
                  ),
                ],
              ),
            ),
            const Hairline(),
            PanelSection(
              title: '值域裁剪',
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  ToggleRow(
                    label: '只显示选中值域',
                    value: settings.valueClipEnabled,
                    help: '按属性值过滤点；裁剪不会改动工程包。',
                    onChanged: (enabled) => _apply(
                      settings.copyWith(valueClipEnabled: enabled),
                    ),
                  ),
                  if (settings.valueClipEnabled) ...[
                    const SizedBox(height: 6),
                    SliderField(
                      label: '下限',
                      value: currentMin.clamp(clipMin, clipMax),
                      min: clipMin,
                      max: clipMax,
                      display: formatNumber(currentMin),
                      onChanged: currentMax <= clipMin
                          ? null
                          : (value) => setState(
                                () => _draggingClipMin =
                                    value.clamp(clipMin, currentMax - step),
                              ),
                      onChangeEnd: (value) {
                        final min = value.clamp(clipMin, currentMax - step);
                        setState(() => _draggingClipMin = null);
                        _apply(settings.copyWith(
                          valueClipMin: min,
                          valueClipMax: currentMax,
                        ));
                      },
                    ),
                    SliderField(
                      label: '上限',
                      value: currentMax.clamp(clipMin, clipMax),
                      min: clipMin,
                      max: clipMax,
                      display: formatNumber(currentMax),
                      onChanged: (value) => setState(
                        () => _draggingClipMax =
                            value.clamp(currentMin + step, clipMax),
                      ),
                      onChangeEnd: (value) {
                        final max = value.clamp(currentMin + step, clipMax);
                        setState(() => _draggingClipMax = null);
                        _apply(settings.copyWith(
                          valueClipMin: currentMin,
                          valueClipMax: max,
                        ));
                      },
                    ),
                    Align(
                      alignment: Alignment.centerRight,
                      child: TextButton(
                        onPressed: () =>
                            _apply(settings.copyWith(clearValueClip: true)),
                        style: TextButton.styleFrom(
                          padding: EdgeInsets.zero,
                          minimumSize: const Size(0, 24),
                          tapTargetSize: MaterialTapTargetSize.shrinkWrap,
                        ),
                        child: Text(
                          '恢复完整值域',
                          style: tokens.type.meta.copyWith(
                            color: tokens.colors.signal,
                          ),
                        ),
                      ),
                    ),
                  ],
                ],
              ),
            ),
            const Hairline(),
            PanelSection(
              title: '性能',
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  SelectField<int>(
                    label: '绘制点数上限',
                    value: kBudgets.any((e) => e.$1 == settings.pointBudget)
                        ? settings.pointBudget
                        : 0,
                    items: kBudgets,
                    onChanged: (value) {
                      if (value == null) return;
                      _apply(settings.copyWith(pointBudget: value));
                    },
                  ),
                  const SizedBox(height: 8),
                  Text(
                    '自动 = 由 LOD 按屏幕像素决定；上限只影响绘制，不改数据集。',
                    style: tokens.type.meta.copyWith(
                      color: tokens.colors.textLow,
                      fontSize: 10.5,
                    ),
                  ),
                ],
              ),
            ),
          ],
        );
      },
    );
  }
}
