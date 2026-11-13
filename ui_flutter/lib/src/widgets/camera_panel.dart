import 'package:flutter/material.dart';

import '../ffi/geoscatter3d_service.dart';
import '../theme/app_tokens.dart';
import '../ui/ui_kit.dart';

/// 相机面板：正交视角、缩放与操作说明。
class CameraPanel extends StatelessWidget {
  const CameraPanel({super.key, required this.service});

  final GeoScatter3dService service;

  static const _views = [
    (0, '−X 西'),
    (1, '+X 东'),
    (2, '−Y 南'),
    (3, '+Y 北'),
    (4, '俯视'),
    (5, '仰视'),
  ];

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    return ListenableBuilder(
      listenable: service,
      builder: (context, _) {
        final enabled = service.ready;
        return ListView(
          padding: const EdgeInsets.only(bottom: 24),
          children: [
            PanelSection(
              title: '正交视角',
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  Wrap(
                    spacing: 8,
                    runSpacing: 8,
                    children: [
                      for (final (index, label) in _views)
                        SizedBox(
                          width: 88,
                          child: AppButton(
                            label: label,
                            dense: true,
                            buttonKey: ValueKey('camera_view_$index'),
                            onPressed: enabled
                                ? () => service.gizmoAxis(index)
                                : null,
                          ),
                        ),
                    ],
                  ),
                  const SizedBox(height: 12),
                  Row(
                    children: [
                      Expanded(
                        child: AppButton(
                          label: '放大',
                          icon: Icons.add,
                          onPressed: enabled
                              ? () => service.executeAction(
                                    'camera.input',
                                    {'scrollY': 1.0},
                                  )
                              : null,
                        ),
                      ),
                      const SizedBox(width: 8),
                      Expanded(
                        child: AppButton(
                          label: '缩小',
                          icon: Icons.remove,
                          onPressed: enabled
                              ? () => service.executeAction(
                                    'camera.input',
                                    {'scrollY': -1.0},
                                  )
                              : null,
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 8),
                  AppButton(
                    label: '复位视图',
                    icon: Icons.restart_alt,
                    buttonKey: const ValueKey('camera_reset'),
                    onPressed: enabled
                        ? () => service.executeAction('camera.reset')
                        : null,
                  ),
                ],
              ),
            ),
            const Hairline(),
            PanelSection(
              title: '操作',
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  _Gesture(tokens, Icons.swipe, '单指拖动', '旋转视角'),
                  _Gesture(tokens, Icons.pinch, '双指拖动 / 捏合', '平移与缩放'),
                  _Gesture(tokens, Icons.ads_click, '单击', '拾取点（测量用）'),
                  _Gesture(tokens, Icons.mouse, '滚轮', '推拉相机'),
                ],
              ),
            ),
          ],
        );
      },
    );
  }
}

class _Gesture extends StatelessWidget {
  const _Gesture(this.tokens, this.icon, this.gesture, this.effect);

  final AppTokens tokens;
  final IconData icon;
  final String gesture;
  final String effect;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 5),
      child: Row(
        children: [
          Icon(icon, size: 15, color: tokens.colors.textLow),
          const SizedBox(width: 10),
          Expanded(
            child: Text(
              gesture,
              style: tokens.text.body.copyWith(color: tokens.colors.textMid),
            ),
          ),
          Text(
            effect,
            style: tokens.text.meta.copyWith(color: tokens.colors.textLow),
          ),
        ],
      ),
    );
  }
}
