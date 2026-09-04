import 'package:flutter/material.dart';
import '../ffi/geoscatter3d_service.dart';
import '../theme/app_theme.dart';
import '../widgets/modern_card.dart';

class RightDockPanel extends StatefulWidget {
  final GeoScatter3dService service;

  const RightDockPanel({
    super.key,
    required this.service,
  });

  @override
  State<RightDockPanel> createState() => _RightDockPanelState();
}

class _RightDockPanelState extends State<RightDockPanel> {
  String _shape = '方形';
  String _heightSource = 'elevation';
  final double _heightScale = 1.0;
  String _colorSource = 'field_statics';

  @override
  Widget build(BuildContext context) {
    return Container(
      width: 290,
      decoration: const BoxDecoration(
        color: AppTheme.background,
        border: Border(
          left: BorderSide(color: AppTheme.border, width: 1),
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
                    color: AppTheme.textTitle,
                  ),
                ),
                IconButton(
                  icon: const Icon(Icons.close, size: 16, color: AppTheme.textDim),
                  onPressed: () {},
                  splashRadius: 16,
                ),
              ],
            ),
          ),

          // 属性设置项列表
          Expanded(
            child: ListenableBuilder(
              listenable: widget.service,
              builder: (context, _) {
                final pointSize = widget.service.pointSize;
                final colormap = widget.service.colormap;
                final scalarMin = widget.service.scalarMin;
                final scalarMax = widget.service.scalarMax;
                final attrs = widget.service.summary.attributes.isNotEmpty
                    ? widget.service.summary.attributes
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
                              const Text('点大小', style: TextStyle(fontSize: 12.5, color: AppTheme.textBody)),
                              Text(pointSize.toStringAsFixed(1),
                                  style: const TextStyle(fontSize: 12.5, fontWeight: FontWeight.w600)),
                            ],
                          ),
                          SliderTheme(
                            data: SliderTheme.of(context).copyWith(
                              activeTrackColor: AppTheme.primaryBlue,
                              thumbColor: AppTheme.primaryBlue,
                              trackHeight: 3,
                              thumbShape: const RoundSliderThumbShape(enabledThumbRadius: 6),
                            ),
                            child: Slider(
                              value: pointSize.clamp(0.5, 8.0),
                              min: 0.5,
                              max: 8.0,
                              onChanged: (v) => widget.service.setPointSize(v),
                            ),
                          ),
                          const SizedBox(height: 8),

                          // 形状
                          const Text('形状', style: TextStyle(fontSize: 12.5, color: AppTheme.textBody)),
                          const SizedBox(height: 4),
                          _buildDropdown(['方形', '圆形'], _shape, (v) => setState(() => _shape = v!)),
                          const SizedBox(height: 10),

                          // 高度来源
                          const Text('高度来源', style: TextStyle(fontSize: 12.5, color: AppTheme.textBody)),
                          const SizedBox(height: 4),
                          _buildDropdown(['elevation', 'Z'], _heightSource, (v) => setState(() => _heightSource = v!)),
                          const SizedBox(height: 10),

                          // 高度缩放
                          const Text('高度缩放', style: TextStyle(fontSize: 12.5, color: AppTheme.textBody)),
                          const SizedBox(height: 4),
                          Container(
                            height: 32,
                            padding: const EdgeInsets.symmetric(horizontal: 10),
                            decoration: BoxDecoration(
                              color: AppTheme.surface,
                              borderRadius: BorderRadius.circular(AppTheme.controlRadius),
                              border: Border.all(color: AppTheme.border),
                            ),
                            alignment: Alignment.center,
                            child: Text('${_heightScale.toStringAsFixed(2)}x',
                                style: const TextStyle(fontSize: 12.5, fontWeight: FontWeight.w500)),
                          ),
                          const SizedBox(height: 10),

                          // 着色字段
                          const Text('着色字段', style: TextStyle(fontSize: 12.5, color: AppTheme.textBody)),
                          const SizedBox(height: 4),
                          _buildDropdown(attrs, attrs.contains(_colorSource) ? _colorSource : attrs.first,
                              (v) => setState(() => _colorSource = v!)),
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
                          const Text('色标方案', style: TextStyle(fontSize: 12.5, color: AppTheme.textBody)),
                          const SizedBox(height: 4),
                          _buildDropdown(
                            ['Viridis', 'Plasma', 'Turbo', 'Jet', 'Coolwarm'],
                            _normalizeColormap(colormap),
                            (v) {
                              if (v != null) widget.service.setColormap(v);
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

                          // 数据范围
                          Row(
                            mainAxisAlignment: MainAxisAlignment.spaceBetween,
                            children: [
                              Text(
                                '标量区间: ${scalarMin.toStringAsFixed(1)} - ${scalarMax.toStringAsFixed(1)}',
                                style: const TextStyle(fontSize: 12, color: AppTheme.textDim, fontFamily: 'Consolas'),
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

  String _normalizeColormap(String name) {
    const valid = ['Viridis', 'Plasma', 'Turbo', 'Jet', 'Coolwarm'];
    for (final v in valid) {
      if (v.toLowerCase() == name.toLowerCase()) return v;
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
      case 'viridis':
      default:
        return const [Color(0xFF440154), Color(0xFF414487), Color(0xFF2A788E), Color(0xFF22A884), Color(0xFF7AD151), Color(0xFFFDE725)];
    }
  }

  Widget _buildDropdown(List<String> items, String value, ValueChanged<String?> onChanged) {
    return Container(
      height: 32,
      padding: const EdgeInsets.symmetric(horizontal: 10),
      decoration: BoxDecoration(
        color: AppTheme.surface,
        borderRadius: BorderRadius.circular(AppTheme.controlRadius),
        border: Border.all(color: AppTheme.border),
      ),
      child: DropdownButtonHideUnderline(
        child: DropdownButton<String>(
          isExpanded: true,
          value: items.contains(value) ? value : items.first,
          icon: const Icon(Icons.keyboard_arrow_down, size: 16, color: AppTheme.textDim),
          style: const TextStyle(fontSize: 12.5, color: AppTheme.textTitle),
          items: items.map((e) => DropdownMenuItem(value: e, child: Text(e))).toList(),
          onChanged: onChanged,
        ),
      ),
    );
  }
}
