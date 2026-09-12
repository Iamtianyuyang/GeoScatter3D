import 'package:flutter/material.dart';
import '../ffi/geoscatter3d_service.dart';
import '../theme/gs3d_tokens.dart';
import '../welcome/welcome_ui_keys.dart';

/// 散点核心渲染与外观设置悬浮卡片 (Render Settings Popover)
///
/// 极简全屏视口模式下的轻量设置浮层：
/// 支持点大小、形状、高度缩放、着色字段、色标方案、标量范围及底色。
class RenderSettingsPopover extends StatelessWidget {
  final GeoScatter3dService service;
  final VoidCallback? onClose;

  const RenderSettingsPopover({
    super.key,
    required this.service,
    this.onClose,
  });

  static const List<String> _colormaps = [
    'Viridis',
    'Plasma',
    'Turbo',
    'Jet',
    'Coolwarm',
    'Geo',
    'Gray',
    'Thermal',
    'Rainbow256',
  ];

  static const List<String> _shapes = ['方形', '圆形', '菱形', '三角形'];
  static const List<String> _heightSources = ['elevation', 'Z'];
  static const List<String> _bgColors = ['深黑蓝', '纯黑', '深灰', '浅灰'];

  String _normalizeColormap(String name) {
    for (final cm in _colormaps) {
      if (cm.toLowerCase() == name.toLowerCase()) return cm;
    }
    return 'Viridis';
  }

  String _colorToName(Color c) {
    final argb = c.toARGB32();
    if (argb == 0xFF000000) return '纯黑';
    if (argb == 0xFF1E222A) return '深灰';
    if (argb == 0xFFF0F2F5 || argb == 0xFFFFFFFF) return '浅灰';
    return '深黑蓝';
  }

  List<Color> _getGradientColors(String colormap) {
    switch (colormap.toLowerCase()) {
      case 'plasma':
        return const [Color(0xFF0D0887), Color(0xFF7E03A8), Color(0xFFCC4778), Color(0xFFF89540), Color(0xFFF0F921)];
      case 'turbo':
        return const [Color(0xFF30123B), Color(0xFF4686FB), Color(0xFF1AE4B6), Color(0xFFA2FC3C), Color(0xFFE23412)];
      case 'jet':
        return const [Color(0xFF00007F), Color(0xFF0000FF), Color(0xFF00FFFF), Color(0xFFFFFF00), Color(0xFFFF0000), Color(0xFF7F0000)];
      case 'coolwarm':
        return const [Color(0xFF3B4CC0), Color(0xFF8CBCF0), Color(0xFFDDDDDD), Color(0xFFF49A7B), Color(0xFFB40426)];
      case 'geo':
        return const [Color(0xFF2C7BB6), Color(0xFFABD9E9), Color(0xFFFFFFBF), Color(0xFFFDAE61), Color(0xFFD7191C)];
      case 'gray':
        return const [Color(0xFF000000), Color(0xFF444444), Color(0xFF888888), Color(0xFFCCCCCC), Color(0xFFFFFFFF)];
      case 'thermal':
        return const [Color(0xFF000000), Color(0xFFE50000), Color(0xFFE5E500), Color(0xFFFFFFFF)];
      case 'rainbow256':
        return const [Colors.purple, Colors.blue, Colors.green, Colors.yellow, Colors.orange, Colors.red];
      case 'viridis':
      default:
        return const [Color(0xFF440154), Color(0xFF3B528B), Color(0xFF21918C), Color(0xFF5EC962), Color(0xFFFDE725)];
    }
  }

  @override
  Widget build(BuildContext context) {
    return Material(
      color: Colors.transparent,
      child: Container(
        key: const ValueKey('render_settings_popover'),
        width: 320,
        constraints: const BoxConstraints(maxHeight: 560),
        decoration: BoxDecoration(
          color: Gs3dTokens.surfaceHeader.withAlpha(245),
          borderRadius: BorderRadius.circular(Gs3dTokens.radiusLg),
          border: Border.all(color: Gs3dTokens.hairline),
          boxShadow: Gs3dTokens.elevation3,
        ),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            // 顶栏：标题与关闭按钮
            Padding(
              padding: const EdgeInsets.fromLTRB(16, 12, 10, 8),
              child: Row(
                mainAxisAlignment: MainAxisAlignment.spaceBetween,
                children: [
                  const Row(
                    children: [
                      Icon(Icons.tune_rounded, size: 16, color: Gs3dTokens.accent),
                      SizedBox(width: 8),
                      Text(
                        '渲染与外观',
                        style: TextStyle(
                          fontSize: 14,
                          fontWeight: FontWeight.w700,
                          color: Gs3dTokens.textPrimary,
                        ),
                      ),
                    ],
                  ),
                  if (onClose != null)
                    IconButton(
                      icon: const Icon(Icons.close, size: 16, color: Gs3dTokens.textSecondary),
                      onPressed: onClose,
                      splashRadius: 16,
                    ),
                ],
              ),
            ),
            const Divider(height: 1, color: Gs3dTokens.hairline),

            // 可滚动控制区
            Flexible(
              child: ListenableBuilder(
                listenable: service,
                builder: (context, _) {
                  final pointSize = service.pointSize;
                  final pointShape = service.pointShape;
                  final heightSource = service.heightSource;
                  final heightScale = service.heightScale;
                  final colorAttr = service.colorAttribute;
                  final colormap = service.colormap;
                  final scalarMin = service.scalarMin;
                  final scalarMax = service.scalarMax;
                  final attrs = List<String>.of(service.summary.attributes);

                  return ListView(
                    shrinkWrap: true,
                    padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 10),
                    children: [
                      // 1. 点大小
                      Row(
                        mainAxisAlignment: MainAxisAlignment.spaceBetween,
                        children: [
                          const Text('点大小', style: TextStyle(fontSize: 12.5, color: Gs3dTokens.textBody)),
                          Text(
                            pointSize.toStringAsFixed(1),
                            style: const TextStyle(fontSize: 12.5, fontWeight: FontWeight.w600),
                          ),
                        ],
                      ),
                      SliderTheme(
                        data: SliderTheme.of(context).copyWith(
                          activeTrackColor: Gs3dTokens.accent,
                          thumbColor: Gs3dTokens.accent,
                          trackHeight: 3,
                          thumbShape: const RoundSliderThumbShape(enabledThumbRadius: 6),
                        ),
                        child: Slider(
                          key: WorkbenchUiKeys.rightDockPointSizeSlider,
                          value: pointSize.clamp(0.5, 8.0),
                          min: 0.5,
                          max: 8.0,
                          onChanged: (v) => service.setPointSize(v),
                        ),
                      ),
                      const SizedBox(height: 6),

                      // 2. 形状与高度来源
                      Row(
                        children: [
                          Expanded(
                            child: Column(
                              crossAxisAlignment: CrossAxisAlignment.start,
                              children: [
                                const Text('散点形状', style: TextStyle(fontSize: 12, color: Gs3dTokens.textBody)),
                                const SizedBox(height: 4),
                                _buildCompactDropdown(
                                  key: WorkbenchUiKeys.rightDockShapeDropdown,
                                  items: _shapes,
                                  value: pointShape,
                                  onChanged: (v) {
                                    if (v != null) service.setPointShape(v);
                                  },
                                ),
                              ],
                            ),
                          ),
                          const SizedBox(width: 10),
                          Expanded(
                            child: Column(
                              crossAxisAlignment: CrossAxisAlignment.start,
                              children: [
                                const Text('高度来源', style: TextStyle(fontSize: 12, color: Gs3dTokens.textBody)),
                                const SizedBox(height: 4),
                                _buildCompactDropdown(
                                  key: WorkbenchUiKeys.rightDockHeightSourceDropdown,
                                  items: _heightSources,
                                  value: heightSource,
                                  onChanged: (v) {
                                    if (v != null) service.setHeightSource(v);
                                  },
                                ),
                              ],
                            ),
                          ),
                        ],
                      ),
                      const SizedBox(height: 10),

                      // 3. 高度缩放
                      Row(
                        mainAxisAlignment: MainAxisAlignment.spaceBetween,
                        children: [
                          const Text('高度缩放', style: TextStyle(fontSize: 12.5, color: Gs3dTokens.textBody)),
                          Text(
                            '${heightScale.toStringAsFixed(2)}x',
                            style: const TextStyle(fontSize: 12.5, fontWeight: FontWeight.w600),
                          ),
                        ],
                      ),
                      SliderTheme(
                        data: SliderTheme.of(context).copyWith(
                          activeTrackColor: Gs3dTokens.accent,
                          thumbColor: Gs3dTokens.accent,
                          trackHeight: 3,
                          thumbShape: const RoundSliderThumbShape(enabledThumbRadius: 6),
                        ),
                        child: Slider(
                          key: WorkbenchUiKeys.rightDockHeightScaleSlider,
                          value: heightScale.clamp(0.1, 10.0),
                          min: 0.1,
                          max: 10.0,
                          onChanged: (v) => service.setHeightScale(v),
                        ),
                      ),
                      const SizedBox(height: 6),

                      // 4. 着色字段
                      const Text('着色字段', style: TextStyle(fontSize: 12, color: Gs3dTokens.textBody)),
                      const SizedBox(height: 4),
                      if (attrs.isEmpty)
                        const Text(
                          '当前数据集没有可用属性',
                          style: TextStyle(fontSize: 11.5, color: Gs3dTokens.textTertiary),
                        )
                      else
                        _buildCompactDropdown(
                          key: WorkbenchUiKeys.rightDockColorAttrDropdown,
                          items: attrs,
                          value: attrs.contains(colorAttr) ? colorAttr : attrs.first,
                          onChanged: (v) {
                            if (v != null) service.setColorAttribute(v);
                          },
                        ),
                      const SizedBox(height: 10),

                      // 5. 色调映射
                      const Text('色标方案', style: TextStyle(fontSize: 12, color: Gs3dTokens.textBody)),
                      const SizedBox(height: 4),
                      _buildCompactDropdown(
                        key: WorkbenchUiKeys.rightDockColormapDropdown,
                        items: _colormaps,
                        value: _normalizeColormap(colormap),
                        onChanged: (v) {
                          if (v != null) service.setColormap(v);
                        },
                      ),
                      const SizedBox(height: 6),
                      Container(
                        height: 12,
                        decoration: BoxDecoration(
                          borderRadius: BorderRadius.circular(2),
                          gradient: LinearGradient(colors: _getGradientColors(colormap)),
                        ),
                      ),
                      const SizedBox(height: 6),

                      // 6. 标量范围与复位
                      Row(
                        mainAxisAlignment: MainAxisAlignment.spaceBetween,
                        children: [
                          Expanded(
                            child: Text(
                              '区间: ${scalarMin.toStringAsFixed(1)} ~ ${scalarMax.toStringAsFixed(1)}',
                              style: const TextStyle(
                                fontSize: 11.5,
                                color: Gs3dTokens.textSecondary,
                                fontFamily: 'Consolas',
                              ),
                              overflow: TextOverflow.ellipsis,
                            ),
                          ),
                          TextButton(
                            key: WorkbenchUiKeys.rightDockResetScalarRangeButton,
                            style: TextButton.styleFrom(
                              padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
                              minimumSize: Size.zero,
                              tapTargetSize: MaterialTapTargetSize.shrinkWrap,
                            ),
                            onPressed: () => service.resetScalarRange(),
                            child: const Text('复位', style: TextStyle(fontSize: 11, color: Gs3dTokens.accent)),
                          ),
                        ],
                      ),
                      const SizedBox(height: 10),

                      // 7. 视口底色
                      const Text('视口底色', style: TextStyle(fontSize: 12, color: Gs3dTokens.textBody)),
                      const SizedBox(height: 4),
                      _buildCompactDropdown(
                        key: WorkbenchUiKeys.rightDockBgColorDropdown,
                        items: _bgColors,
                        value: _colorToName(service.viewportBackgroundColor),
                        onChanged: (v) {
                          if (v == null) return;
                          switch (v) {
                            case '纯黑':
                              service.setViewportBackgroundColor(Gs3dTokens.viewportBlack);
                              break;
                            case '深灰':
                              service.setViewportBackgroundColor(Gs3dTokens.viewportSlate);
                              break;
                            case '浅灰':
                              service.setViewportBackgroundColor(Gs3dTokens.viewportLight);
                              break;
                            case '深黑蓝':
                            default:
                              service.setViewportBackgroundColor(Gs3dTokens.viewportMidnight);
                              break;
                          }
                        },
                      ),
                    ],
                  );
                },
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildCompactDropdown({
    Key? key,
    required List<String> items,
    required String value,
    required ValueChanged<String?> onChanged,
  }) {
    return Container(
      height: 30,
      padding: const EdgeInsets.symmetric(horizontal: 8),
      decoration: BoxDecoration(
        color: Gs3dTokens.surface,
        borderRadius: BorderRadius.circular(Gs3dTokens.radiusSm),
        border: Border.all(color: Gs3dTokens.hairline),
      ),
      child: DropdownButtonHideUnderline(
        child: DropdownButton<String>(
          key: key,
          value: items.contains(value) ? value : items.first,
          isDense: true,
          isExpanded: true,
          icon: const Icon(Icons.arrow_drop_down, size: 16, color: Gs3dTokens.textSecondary),
          dropdownColor: Gs3dTokens.surface,
          style: const TextStyle(
            fontSize: 12,
            color: Gs3dTokens.textPrimary,
            fontFamily: Gs3dType.fontFamily,
          ),
          items: items
              .map((item) => DropdownMenuItem(
                    value: item,
                    child: Text(item, overflow: TextOverflow.ellipsis),
                  ))
              .toList(),
          onChanged: onChanged,
        ),
      ),
    );
  }
}
