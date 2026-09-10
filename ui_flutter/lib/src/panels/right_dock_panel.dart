import 'package:flutter/material.dart';
import '../theme/gs3d_tokens.dart';
import '../ffi/geoscatter3d_service.dart';
import '../welcome/welcome_ui_keys.dart';
import '../widgets/modern_card.dart';

class RightDockPanel extends StatelessWidget {
  final GeoScatter3dService service;

  const RightDockPanel({
    super.key,
    required this.service,
  });

  @override
  Widget build(BuildContext context) {
    return Container(
      key: WorkbenchUiKeys.rightDockPanel,
      width: 290,
      decoration: const BoxDecoration(
        color: Gs3dTokens.windowBg,
        border: Border(
          left: BorderSide(color: Gs3dTokens.border, width: 1),
        ),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // 顶部标题栏
          Padding(
            padding: const EdgeInsets.fromLTRB(14, 12, 10, 8),
            child: Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                const Text(
                  '属性与渲染',
                  style: TextStyle(
                    fontSize: 14,
                    fontWeight: FontWeight.w700,
                    color: Gs3dTokens.textPrimary,
                  ),
                ),
                IconButton(
                  key: WorkbenchUiKeys.rightDockCloseButton,
                  icon: const Icon(Icons.close, size: 16, color: Gs3dTokens.textSecondary),
                  onPressed: () => service.toggleRightDock(false),
                  splashRadius: 16,
                ),
              ],
            ),
          ),

          // 属性设置项列表
          Expanded(
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
                final attrs = service.summary.attributes.isNotEmpty
                    ? service.summary.attributes
                    : ['field_statics', 'elevation'];

                return ListView(
                  padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
                  children: [
                    CollapsibleCard(
                      title: '点云外观',
                      initialOpen: true,
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          // 点大小
                          Row(
                            mainAxisAlignment: MainAxisAlignment.spaceBetween,
                            children: [
                              const Text('点大小', style: TextStyle(fontSize: 12.5, color: Gs3dTokens.textBody)),
                              Text(pointSize.toStringAsFixed(1),
                                  style: const TextStyle(fontSize: 12.5, fontWeight: FontWeight.w600)),
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
                          const SizedBox(height: 8),

                          // 形状
                          const Text('形状', style: TextStyle(fontSize: 12.5, color: Gs3dTokens.textBody)),
                          const SizedBox(height: 4),
                          _buildDropdown(
                            key: WorkbenchUiKeys.rightDockShapeDropdown,
                            items: const ['方形', '圆形', '菱形', '三角形'],
                            value: pointShape,
                            onChanged: (v) {
                              if (v != null) service.setPointShape(v);
                            },
                          ),
                          const SizedBox(height: 10),

                          // 高度来源
                          const Text('高度来源', style: TextStyle(fontSize: 12.5, color: Gs3dTokens.textBody)),
                          const SizedBox(height: 4),
                          _buildDropdown(
                            key: WorkbenchUiKeys.rightDockHeightSourceDropdown,
                            items: const ['elevation', 'Z'],
                            value: heightSource,
                            onChanged: (v) {
                              if (v != null) service.setHeightSource(v);
                            },
                          ),
                          const SizedBox(height: 10),

                          // 高度缩放
                          Row(
                            mainAxisAlignment: MainAxisAlignment.spaceBetween,
                            children: [
                              const Text('高度缩放', style: TextStyle(fontSize: 12.5, color: Gs3dTokens.textBody)),
                              Text('${heightScale.toStringAsFixed(2)}x',
                                  style: const TextStyle(fontSize: 12.5, fontWeight: FontWeight.w600)),
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
                          const SizedBox(height: 10),

                          // 着色字段
                          const Text('着色字段', style: TextStyle(fontSize: 12.5, color: Gs3dTokens.textBody)),
                          const SizedBox(height: 4),
                          _buildDropdown(
                            key: WorkbenchUiKeys.rightDockColorAttrDropdown,
                            items: attrs,
                            value: attrs.contains(colorAttr) ? colorAttr : attrs.first,
                            onChanged: (v) {
                              if (v != null) service.setColorAttribute(v);
                            },
                          ),
                        ],
                      ),
                    ),
                    const SizedBox(height: 10),

                    CollapsibleCard(
                      title: '色调映射 (Colormap)',
                      initialOpen: true,
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          const Text('色标方案', style: TextStyle(fontSize: 12.5, color: Gs3dTokens.textBody)),
                          const SizedBox(height: 4),
                          _buildDropdown(
                            key: WorkbenchUiKeys.rightDockColormapDropdown,
                            items: const [
                              'Viridis',
                              'Plasma',
                              'Turbo',
                              'Jet',
                              'Coolwarm',
                              'Geo',
                              'Gray',
                              'Thermal',
                              'Rainbow256',
                            ],
                            value: _normalizeColormap(colormap),
                            onChanged: (v) {
                              if (v != null) service.setColormap(v);
                            },
                          ),
                          const SizedBox(height: 10),

                          // 渐变预览条
                          Container(
                            height: 14,
                            decoration: BoxDecoration(
                              borderRadius: BorderRadius.circular(2),
                              gradient: LinearGradient(
                                colors: _getGradientColors(colormap),
                              ),
                            ),
                          ),
                          const SizedBox(height: 8),

                          // 数据范围与复位按钮
                          Row(
                            mainAxisAlignment: MainAxisAlignment.spaceBetween,
                            children: [
                              Expanded(
                                child: Text(
                                  '区间: ${scalarMin.toStringAsFixed(1)} ~ ${scalarMax.toStringAsFixed(1)}',
                                  style: const TextStyle(fontSize: 11.5, color: Gs3dTokens.textSecondary, fontFamily: 'Consolas'),
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
                        ],
                      ),
                    ),
                    const SizedBox(height: 10),

                    CollapsibleCard(
                      title: '视口环境',
                      initialOpen: false,
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          const Text('视口底色', style: TextStyle(fontSize: 12.5, color: Gs3dTokens.textBody)),
                          const SizedBox(height: 4),
                          _buildDropdown(
                            key: WorkbenchUiKeys.rightDockBgColorDropdown,
                            items: const ['深黑蓝', '纯黑', '深灰', '浅灰'],
                            value: _colorToName(service.viewportBackgroundColor),
                            onChanged: (v) {
                              if (v != null) {
                                switch (v) {
                                  case '纯黑':
                                    service.setViewportBackgroundColor(const Color(0xFF000000));
                                    break;
                                  case '深灰':
                                    service.setViewportBackgroundColor(const Color(0xFF1E222A));
                                    break;
                                  case '浅灰':
                                    service.setViewportBackgroundColor(const Color(0xFFF0F2F5));
                                    break;
                                  case '深黑蓝':
                                  default:
                                    service.setViewportBackgroundColor(const Color(0xFF161A22));
                                    break;
                                }
                              }
                            },
                          ),
                          const SizedBox(height: 10),
                          const Text('界面主题', style: TextStyle(fontSize: 12.5, color: Gs3dTokens.textBody)),
                          const SizedBox(height: 4),
                          _buildDropdown(
                            key: const ValueKey('workbench.right_dock.theme_dropdown'),
                            items: GeoScatter3dService.kThemeNames,
                            value: service.themeName,
                            onChanged: (v) {
                              if (v == null) return;
                              final idx = GeoScatter3dService.kThemeNames.indexOf(v);
                              if (idx >= 0) service.setTheme(idx);
                            },
                          ),
                          const SizedBox(height: 10),
                          Row(
                            mainAxisAlignment: MainAxisAlignment.spaceBetween,
                            children: [
                              const Text(
                                '相机联动',
                                style: TextStyle(
                                  fontSize: 12.5,
                                  color: Gs3dTokens.textBody,
                                ),
                              ),
                              Switch(
                                value: service.cameraLinked,
                                onChanged: (v) => service.setCameraLinked(v),
                              ),
                            ],
                          ),
                        ],
                      ),
                    ),
                  ],
                );
              },
            ),
          ),
        ],
      ),
    );
  }

  String _colorToName(Color color) {
    final argb = color.toARGB32();
    if (argb == 0xFF000000) return '纯黑';
    if (argb == 0xFF1E222A) return '深灰';
    if (argb == 0xFFF0F2F5) return '浅灰';
    return '深黑蓝';
  }

  String _normalizeColormap(String name) {
    const valid = [
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
    for (final v in valid) {
      if (v.toLowerCase() == name.toLowerCase()) return v;
      if (v.toLowerCase() == 'rainbow256' && name.toLowerCase() == 'rainbow') {
        return v;
      }
      if (v.toLowerCase() == 'gray' && name.toLowerCase() == 'grey') {
        return v;
      }
      if (v.toLowerCase() == 'coolwarm' && name.toLowerCase() == 'cool-warm') {
        return v;
      }
    }
    return 'Viridis';
  }

  List<Color> _getGradientColors(String name) {
    switch (name.toLowerCase()) {
      case 'plasma':
        return const [Color(0xFF0D0887), Color(0xFF6A00A8), Color(0xFFB12A90), Color(0xFFE16462), Color(0xFFFCA636), Color(0xFFF0F921)];
      case 'turbo':
        return const [Color(0xFF30123B), Color(0xFF4686FB), Color(0xFF1AE4B6), Color(0xFFA2FC3C), Color(0xFFFABA39), Color(0xFFE4460A), Color(0xFF7A0403)];
      case 'jet':
        return const [Color(0xFF000080), Color(0xFF0000FF), Color(0xFF00FFFF), Color(0xFFFFFF00), Color(0xFFFF0000), Color(0xFF800000)];
      case 'coolwarm':
        return const [Color(0xFF3B4CC0), Color(0xFF8CBCF1), Color(0xFFDDDCDC), Color(0xFFF49A7B), Color(0xFFB40426)];
      case 'geo':
        return const [Color(0xFF1A9850), Color(0xFF91CF60), Color(0xFFD9EF8B), Color(0xFFFFEE66), Color(0xFFFC8D59), Color(0xFFD73027)];
      case 'gray':
        return const [Color(0xFF000000), Color(0xFFFFFFFF)];
      case 'thermal':
        return const [Color(0xFF000000), Color(0xFF3B0F70), Color(0xFF8C2981), Color(0xFFDE4968), Color(0xFFFE9F6D), Color(0xFFFCFDBF)];
      case 'rainbow256':
        return const [Color(0xFF0000FF), Color(0xFF00FFFF), Color(0xFF00FF00), Color(0xFFFFFF00), Color(0xFFFF0000)];
      case 'viridis':
      default:
        return const [Color(0xFF440154), Color(0xFF414487), Color(0xFF2A788E), Color(0xFF22A884), Color(0xFF7AD151), Color(0xFFFDE725)];
    }
  }

  Widget _buildDropdown({
    Key? key,
    required List<String> items,
    required String value,
    required ValueChanged<String?> onChanged,
  }) {
    return Container(
      key: key,
      height: 32,
      padding: const EdgeInsets.symmetric(horizontal: 10),
      decoration: BoxDecoration(
        color: Gs3dTokens.surface,
        borderRadius: BorderRadius.circular(Gs3dTokens.controlRadius),
        border: Border.all(color: Gs3dTokens.border),
      ),
      child: DropdownButtonHideUnderline(
        child: DropdownButton<String>(
          isExpanded: true,
          value: items.contains(value) ? value : items.first,
          icon: const Icon(Icons.keyboard_arrow_down, size: 16, color: Gs3dTokens.textSecondary),
          style: const TextStyle(fontSize: 12.5, color: Gs3dTokens.textPrimary),
          items: items.map((e) => DropdownMenuItem(value: e, child: Text(e))).toList(),
          onChanged: onChanged,
        ),
      ),
    );
  }
}
