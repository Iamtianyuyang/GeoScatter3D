import 'package:flutter/material.dart';

import '../ffi/geoscatter3d_service.dart';
import '../theme/app_tokens.dart';

/// 成果面板：导出点云与截图，只展示落盘路径，不假装有另存为对话框。
class ResultPanel extends StatelessWidget {
  const ResultPanel({super.key, required this.service});

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
        // 每次通知都重新算：静态字段在 ListenableBuilder 外层会读到旧值。
        final tooLarge =
            service.sourcePointCount > GeoScatter3dService.maxSyncExportPoints;
        final busy = service.exporting;
        final canExport = service.canExport;
        final path = service.lastExportPath;
        final error = service.lastExportError;
        return ListView(
          children: [
            Text('导出', style: title),
            SizedBox(height: tokens.spacing.sm),
            Text(
              busy
                  ? '正在写文件…'
                  : tooLarge
                  ? '超过 ${GeoScatter3dService.maxSyncExportPoints ~/ 10000} 万点的导出会长时间卡住渲染，暂不支持。'
                  : '导出当前数据集或当前帧，文件写在应用状态目录的 exports/ 下。',
              key: const ValueKey('result_status'),
              style: body,
            ),
            SizedBox(height: tokens.spacing.lg),
            Wrap(
              spacing: tokens.spacing.sm,
              runSpacing: tokens.spacing.sm,
              children: [
                OutlinedButton(
                  key: const ValueKey('export_ply_button'),
                  onPressed: busy || !canExport
                      ? null
                      : () => service.exportPoints(format: 'ply'),
                  child: const Text('导出 PLY'),
                ),
                OutlinedButton(
                  key: const ValueKey('export_csv_button'),
                  onPressed: busy || !canExport
                      ? null
                      : () => service.exportPoints(format: 'csv'),
                  child: const Text('导出 CSV'),
                ),
                OutlinedButton(
                  key: const ValueKey('save_frame_button'),
                  onPressed: busy || !service.ready
                      ? null
                      : () => service.saveScreenshot(),
                  child: const Text('保存当前帧'),
                ),
              ],
            ),
            if (path != null) ...[
              SizedBox(height: tokens.spacing.lg),
              Text(
                '已保存',
                style: title,
              ),
              SizedBox(height: tokens.spacing.xs),
              Text(
                path,
                key: const ValueKey('result_last_path'),
                style: body,
              ),
            ],
            if (error != null) ...[
              SizedBox(height: tokens.spacing.md),
              Text(
                error,
                key: const ValueKey('result_error'),
                style: body.copyWith(color: tokens.colors.chromeTextPrimary),
              ),
            ],
          ],
        );
      },
    );
  }
}
