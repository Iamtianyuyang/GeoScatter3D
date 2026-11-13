import 'package:flutter/material.dart';

import '../ffi/geoscatter3d_service.dart';
import '../theme/app_tokens.dart';

/// 测量面板：列出拾取到的点、两点距离和高差。
class MeasurementPanel extends StatelessWidget {
  const MeasurementPanel({super.key, required this.service});

  final GeoScatter3dService service;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    final title = tokens.typography.sectionTitle.copyWith(
      color: tokens.colors.chromeTextPrimary,
    );
    final body = tokens.typography.meta.copyWith(
      color: tokens.colors.chromeTextSecondary,
    );

    return ListenableBuilder(
      listenable: service,
      builder: (context, child) {
        final points = service.pickedPoints;
        final distance = service.measurementDistance;
        final heightDelta = service.measurementHeightDelta;
        return ListView(
          children: [
            Text('测量', style: title),
            SizedBox(height: tokens.spacing.sm),
            Text(
              points.isEmpty ? '在视口里单击点云，依次拾取起点和终点。' : '拾取点',
              style: body,
            ),
            for (var i = 0; i < points.length; i++) ...[
              SizedBox(height: tokens.spacing.sm),
              Text(
                '${i == 0 ? '起点' : '终点'}  '
                'X ${points[i].x.toStringAsFixed(3)}  '
                'Y ${points[i].y.toStringAsFixed(3)}  '
                'Z ${points[i].z.toStringAsFixed(3)}',
                key: ValueKey('measurement_point_$i'),
                style: body,
              ),
            ],
            if (distance != null) ...[
              SizedBox(height: tokens.spacing.lg),
              Text(
                '距离 ${distance.toStringAsFixed(3)}',
                key: const ValueKey('measurement_distance'),
                style: title,
              ),
              SizedBox(height: tokens.spacing.xs),
              Text(
                '高差 ${heightDelta!.toStringAsFixed(3)}',
                key: const ValueKey('measurement_height_delta'),
                style: body,
              ),
            ],
            SizedBox(height: tokens.spacing.lg),
            Align(
              alignment: Alignment.centerLeft,
              child: TextButton(
                key: const ValueKey('measurement_clear'),
                onPressed: points.isEmpty ? null : service.clearMeasurement,
                child: const Text('清除测量'),
              ),
            ),
          ],
        );
      },
    );
  }
}
