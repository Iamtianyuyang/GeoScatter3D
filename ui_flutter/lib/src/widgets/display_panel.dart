import 'dart:async';

import 'package:flutter/material.dart';

import '../ffi/geoscatter3d_service.dart';
import '../ffi/render_settings.dart';
import '../theme/app_tokens.dart';

/// 显示设置：配色与点大小。改动先本地生效，拖动结束后才入队并落盘。
class DisplayPanel extends StatefulWidget {
  const DisplayPanel({super.key, required this.service});

  final GeoScatter3dService service;

  @override
  State<DisplayPanel> createState() => _DisplayPanelState();
}

/// 索引与 `assets/shaders/point.frag` 的 `colormap_dispatch` 一致。
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

class _DisplayPanelState extends State<DisplayPanel> {
  late int _colormap = widget.service.renderSettings.colormapIndex;
  late double _pointSize = widget.service.renderSettings.pointSize;

  Future<void> _save() async {
    try {
      await widget.service.saveRenderSettings();
    } catch (error) {
      if (!mounted) return;
      ScaffoldMessenger.maybeOf(
        context,
      )?.showSnackBar(SnackBar(content: Text('无法保存显示设置：$error')));
    }
  }

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;

    return ListView(
      children: [
        Text(
          '配色',
          style: tokens.typography.sectionTitle.copyWith(
            color: tokens.colors.chromeTextPrimary,
          ),
        ),
        SizedBox(height: tokens.spacing.sm),
        DropdownButton<int>(
          key: const ValueKey('colormap_dropdown'),
          value: _colormap,
          isExpanded: true,
          dropdownColor: tokens.colors.chromeSurface,
          items: [
            for (final (index, name) in kColormaps)
              DropdownMenuItem(value: index, child: Text(name)),
          ],
          onChanged: (value) {
            if (value == null) return;
            setState(() => _colormap = value);
            widget.service.setColormapIndex(value);
            unawaited(_save());
          },
        ),
        SizedBox(height: tokens.spacing.xl),
        Text(
          '点大小 ${_pointSize.toStringAsFixed(1)} px',
          style: tokens.typography.sectionTitle.copyWith(
            color: tokens.colors.chromeTextPrimary,
          ),
        ),
        Slider(
          key: const ValueKey('point_size_slider'),
          value: _pointSize,
          min: RenderSettings.minPointSize,
          max: RenderSettings.maxPointSize,
          divisions: 15,
          label: _pointSize.toStringAsFixed(1),
          onChanged: (value) => setState(() => _pointSize = value),
          onChangeEnd: (value) {
            widget.service.setPointSize(value);
            unawaited(_save());
          },
        ),
        SizedBox(height: tokens.spacing.sm),
        Text(
          '只影响本机显示；工程包里的坐标和属性不变。',
          style: tokens.typography.meta.copyWith(
            color: tokens.colors.chromeTextSecondary,
          ),
        ),
      ],
    );
  }
}
