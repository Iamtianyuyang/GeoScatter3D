import 'package:flutter/material.dart';
import '../theme/app_theme.dart';
import '../widgets/modern_card.dart';

class RightDockPanel extends StatefulWidget {
  const RightDockPanel({super.key});

  @override
  State<RightDockPanel> createState() => _RightDockPanelState();
}

class _RightDockPanelState extends State<RightDockPanel> {
  double _pointSize = 1.5;
  String _shape = '方形';
  String _heightSource = 'elevation';
  final double _heightScale = 1.0;
  String _colorSource = 'field_statics';
  String _colorMap = 'Rainbow256';

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
                  '属性',
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
            child: ListView(
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
                          Text(_pointSize.toStringAsFixed(1),
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
                          value: _pointSize,
                          min: 0.5,
                          max: 5.0,
                          onChanged: (v) => setState(() => _pointSize = v),
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
                      const Text('着色', style: TextStyle(fontSize: 12.5, color: AppTheme.textBody)),
                      const SizedBox(height: 4),
                      _buildDropdown(['field_statics', 'elevation'], _colorSource, (v) => setState(() => _colorSource = v!)),
                    ],
                  ),
                ),
                const SizedBox(height: 10),

                CollapsibleCard(
                  title: '色调映射',
                  initialOpen: true,
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      const Text('色标', style: TextStyle(fontSize: 12.5, color: AppTheme.textBody)),
                      const SizedBox(height: 4),
                      _buildDropdown(['Rainbow256', 'Viridis', 'Turbo', 'Jet'], _colorMap, (v) => setState(() => _colorMap = v!)),
                      const SizedBox(height: 10),

                      // 渐变预览条
                      Container(
                        height: 14,
                        decoration: BoxDecoration(
                          borderRadius: BorderRadius.circular(2),
                          gradient: const LinearGradient(
                            colors: [
                              Colors.blue,
                              Colors.cyan,
                              Colors.green,
                              Colors.yellow,
                              Colors.red,
                              Colors.purple,
                            ],
                          ),
                        ),
                      ),
                      const SizedBox(height: 8),

                      // 数据范围
                      Row(
                        mainAxisAlignment: MainAxisAlignment.spaceBetween,
                        children: const [
                          Text('数据范围: 7 - 25', style: TextStyle(fontSize: 12, color: AppTheme.textDim)),
                        ],
                      ),
                    ],
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
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
          value: value,
          icon: const Icon(Icons.keyboard_arrow_down, size: 16, color: AppTheme.textDim),
          style: const TextStyle(fontSize: 12.5, color: AppTheme.textTitle),
          items: items.map((e) => DropdownMenuItem(value: e, child: Text(e))).toList(),
          onChanged: onChanged,
        ),
      ),
    );
  }
}
