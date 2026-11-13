import 'package:flutter/material.dart';

import '../ffi/geoscatter3d_service.dart';
import '../theme/app_tokens.dart';
import '../ui/format.dart';
import '../ui/ui_kit.dart';

/// 成果面板：导出点云 / 当前帧。
///
/// 导出是同步调用，超过上限会长时间占住渲染线程，所以面板直接把限制
/// 说清楚并禁用按钮，而不是让人点了等半天。
class ResultPanel extends StatelessWidget {
  const ResultPanel({super.key, required this.service});

  final GeoScatter3dService service;

  String _limitText() {
    final limit = GeoScatter3dService.maxSyncExportPoints;
    return limit >= 10000 ? '${limit ~/ 10000} 万' : '$limit';
  }

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    return ListenableBuilder(
      listenable: service,
      builder: (context, _) {
        // 每次通知都重算：放在 builder 外会读到旧值（曾经踩过）。
        final tooLarge =
            service.sourcePointCount > GeoScatter3dService.maxSyncExportPoints;
        final busy = service.exporting;
        final canExport = service.canExport;
        final path = service.lastExportPath;
        final error = service.lastExportError;
        final status = busy
            ? '正在写文件…'
            : tooLarge
                ? '超过 ${_limitText()}点的导出会长时间卡住渲染，暂不支持。'
                : '导出当前数据集或当前帧，文件写在应用状态目录的 exports/ 下。';
        return ListView(
          padding: const EdgeInsets.only(bottom: 24),
          children: [
            PanelSection(
              title: '导出',
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  Text(
                    status,
                    key: const ValueKey('result_status'),
                    style: tokens.text.meta.copyWith(
                      color: tooLarge
                          ? tokens.colors.warn
                          : tokens.colors.textMid,
                    ),
                  ),
                  const SizedBox(height: 12),
                  Row(
                    children: [
                      Expanded(
                        child: AppButton(
                          label: '导出 PLY',
                          buttonKey: const ValueKey('export_ply_button'),
                          onPressed: busy || !canExport
                              ? null
                              : () => service.exportPoints(format: 'ply'),
                        ),
                      ),
                      const SizedBox(width: 8),
                      Expanded(
                        child: AppButton(
                          label: '导出 CSV',
                          buttonKey: const ValueKey('export_csv_button'),
                          onPressed: busy || !canExport
                              ? null
                              : () => service.exportPoints(format: 'csv'),
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 8),
                  AppButton(
                    label: '保存当前帧',
                    icon: Icons.photo_camera_outlined,
                    buttonKey: const ValueKey('save_frame_button'),
                    onPressed: busy || !service.ready
                        ? null
                        : service.saveScreenshot,
                  ),
                  if (busy) ...[
                    const SizedBox(height: 12),
                    const ThinProgress(value: 0),
                  ],
                ],
              ),
            ),
            if (path != null) ...[
              const Hairline(),
              PanelSection(
                title: '最近一次',
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    KeyValueRow(
                      label: '文件',
                      value: fileName(path),
                      valueColor: tokens.colors.signal,
                    ),
                    SelectableText(
                      path,
                      key: const ValueKey('result_last_path'),
                      maxLines: 3,
                      style: tokens.text.meta.copyWith(
                        color: tokens.colors.textLow,
                        fontSize: 10.5,
                      ),
                    ),
                  ],
                ),
              ),
            ],
            if (error != null) ...[
              const Hairline(),
              PanelSection(
                child: NoticeBanner(
                  text: error,
                  key: const ValueKey('result_error'),
                  tone: ButtonTone.danger,
                  icon: Icons.error_outline,
                ),
              ),
            ],
          ],
        );
      },
    );
  }
}
