import 'package:flutter/material.dart';

import '../ffi/geoscatter3d_service.dart';
import '../theme/app_tokens.dart';
import '../ui/format.dart';
import '../ui/ui_kit.dart';

/// 测量面板：两个拾取点的坐标、空间距离与高差。
///
/// 距离和高差是这块面板唯一需要「一眼读出」的值，所以放大字号、
/// 等宽数字、带正负号；坐标退到第二层，三位小数即可。
class MeasurementPanel extends StatelessWidget {
  const MeasurementPanel({super.key, required this.service});

  final GeoScatter3dService service;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    return ListenableBuilder(
      listenable: service,
      builder: (context, _) {
        final points = service.pickedPoints;
        final distance = service.measurementDistance;
        final heightDelta = service.measurementHeightDelta;
        return ListView(
          padding: const EdgeInsets.only(bottom: 24),
          children: [
            PanelSection(
              title: '读数',
              child: points.isEmpty
                  ? const EmptyHint(
                      text: '还没有拾取点',
                      hint: '在视口中单击点云，依次选择起点与终点。第三次点击开始新的一组。',
                      icon: Icons.straighten,
                    )
                  : points.length == 1
                      ? NoticeBanner(
                          text: '已拾取起点，再点一个点得到距离与高差。',
                          icon: Icons.adjust,
                        )
                      : Column(
                          crossAxisAlignment: CrossAxisAlignment.stretch,
                          children: [
                            Text(
                              '距离 ${distance!.toStringAsFixed(3)}',
                              key: const ValueKey('measurement_distance'),
                              style: tokens.type.display.copyWith(
                                color: tokens.colors.signal,
                                fontFeatures: const [
                                  FontFeature.tabularFigures(),
                                ],
                              ),
                            ),
                            const SizedBox(height: 2),
                            Text(
                              '高差 ${heightDelta! >= 0 ? '+' : ''}'
                              '${heightDelta.toStringAsFixed(3)}',
                              key: const ValueKey('measurement_height_delta'),
                              style: tokens.type.numeric.copyWith(
                                fontSize: 14,
                                color: tokens.colors.textHigh,
                              ),
                            ),
                            const SizedBox(height: 6),
                            Text(
                              '距离为两点三维直线距离；高差 = 终点 − 起点。',
                              style: tokens.type.meta.copyWith(
                                color: tokens.colors.textLow,
                                fontSize: 10.5,
                              ),
                            ),
                          ],
                        ),
            ),
            if (points.isNotEmpty) ...[
              const Hairline(),
              PanelSection(
                title: '拾取点',
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    for (var i = 0; i < points.length; i++)
                      _PointRow(
                        label: i == 0 ? '起点' : '终点',
                        index: i,
                        x: points[i].x,
                        y: points[i].y,
                        z: points[i].z,
                        value: points[i].value,
                      ),
                  ],
                ),
              ),
              PanelSection(
                padding: const EdgeInsets.fromLTRB(16, 0, 16, 12),
                child: AppButton(
                  label: '清除测量',
                  icon: Icons.backspace_outlined,
                  buttonKey: const ValueKey('measurement_clear'),
                  onPressed: service.clearMeasurement,
                ),
              ),
            ],
          ],
        );
      },
    );
  }
}

class _PointRow extends StatelessWidget {
  const _PointRow({
    required this.label,
    required this.index,
    required this.x,
    required this.y,
    required this.z,
    required this.value,
  });

  final String label;
  final int index;
  final double x;
  final double y;
  final double z;
  final double value;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 6),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Container(
                width: 16,
                height: 16,
                alignment: Alignment.center,
                decoration: BoxDecoration(
                  color: tokens.colors.signalSoft,
                  borderRadius: BorderRadius.circular(3),
                ),
                child: Text(
                  '${index + 1}',
                  style: tokens.type.meta.copyWith(
                    fontSize: 10,
                    color: tokens.colors.signal,
                    fontWeight: FontWeight.w700,
                  ),
                ),
              ),
              const SizedBox(width: 6),
              Text(
                label,
                style: tokens.type.meta.copyWith(
                  color: tokens.colors.textMid,
                  fontWeight: FontWeight.w700,
                ),
              ),
              const Spacer(),
              Text(
                '值 ${formatNumber(value, digits: 3)}',
                style: tokens.type.meta.copyWith(
                  color: tokens.colors.textLow,
                  fontSize: 10.5,
                ),
              ),
            ],
          ),
          const SizedBox(height: 4),
          Text(
            'X ${x.toStringAsFixed(3)}\n'
            'Y ${y.toStringAsFixed(3)}\n'
            'Z ${z.toStringAsFixed(3)}',
            key: ValueKey('measurement_point_$index'),
            style: tokens.type.numeric.copyWith(
              fontSize: 12,
              height: 1.45,
              color: tokens.colors.textHigh,
            ),
          ),
        ],
      ),
    );
  }
}
