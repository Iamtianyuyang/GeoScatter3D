import 'package:flutter/material.dart';
import '../ffi/geoscatter3d_service.dart';
import '../theme/app_theme.dart';

/// 点云数据导出对话框 (Export Point Cloud Dialog)
class ExportPointCloudDialog extends StatefulWidget {
  final GeoScatter3dService service;

  const ExportPointCloudDialog({super.key, required this.service});

  @override
  State<ExportPointCloudDialog> createState() => _ExportPointCloudDialogState();
}

class _ExportPointCloudDialogState extends State<ExportPointCloudDialog> {
  final TextEditingController _pathController = TextEditingController(
    text: 'data/export_points.ply',
  );
  String _format = 'ply';
  bool _isExporting = false;

  @override
  void dispose() {
    _pathController.dispose();
    super.dispose();
  }

  void _onFormatChanged(String fmt) {
    setState(() {
      _format = fmt;
      var current = _pathController.text;
      if (fmt == 'ply' && current.endsWith('.csv')) {
        _pathController.text = current.replaceAll(RegExp(r'\.csv$'), '.ply');
      } else if (fmt == 'csv' && current.endsWith('.ply')) {
        _pathController.text = current.replaceAll(RegExp(r'\.ply$'), '.csv');
      }
    });
  }

  Future<void> _doExport() async {
    final path = _pathController.text.trim();
    if (path.isEmpty) return;

    setState(() {
      _isExporting = true;
    });

    final ok = await widget.service.exportPointCloudThroughNativeViewer(
      path,
      format: _format,
    );

    if (!mounted) return;

    setState(() {
      _isExporting = false;
    });

    final messenger = ScaffoldMessenger.of(context);
    Navigator.of(context).pop();

    messenger.showSnackBar(
      SnackBar(
        content: Text(ok ? '点云已成功导出至: $path' : '导出点云失败，请检查文件写入权限'),
        duration: const Duration(seconds: 3),
        behavior: SnackBarBehavior.floating,
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      key: const ValueKey('export_point_cloud_dialog'),
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
      backgroundColor: AppTheme.surface,
      title: const Row(
        children: [
          Icon(
            Icons.file_download_outlined,
            color: AppTheme.primaryBlue,
            size: 22,
          ),
          SizedBox(width: 10),
          Text(
            '导出点云数据',
            style: TextStyle(fontSize: 16, fontWeight: FontWeight.w700),
          ),
        ],
      ),
      content: SizedBox(
        width: 480,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text(
              '选择目标文件导出格式与存储路径：',
              style: TextStyle(fontSize: 12.5, color: AppTheme.textBody),
            ),
            const SizedBox(height: 14),

            // 格式单选
            Row(
              children: [
                _buildFormatRadio(
                  'ply',
                  'PLY 格式 (*.ply)',
                  '兼容 MeshLab / CloudCompare',
                ),
                const SizedBox(width: 12),
                _buildFormatRadio('csv', 'CSV 文本 (*.csv)', '纯文本逗号分隔'),
              ],
            ),
            const SizedBox(height: 14),

            // 导出路径输入
            const Text(
              '输出路径 (Path):',
              style: TextStyle(
                fontSize: 12,
                fontWeight: FontWeight.w600,
                color: AppTheme.textTitle,
              ),
            ),
            const SizedBox(height: 6),
            TextField(
              controller: _pathController,
              style: const TextStyle(fontSize: 13, fontFamily: 'monospace'),
              decoration: InputDecoration(
                isDense: true,
                border: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(6),
                  borderSide: const BorderSide(color: AppTheme.border),
                ),
                contentPadding: const EdgeInsets.symmetric(
                  horizontal: 10,
                  vertical: 8,
                ),
              ),
            ),
            const SizedBox(height: 10),
            Text(
              '当前待导出有效点数: ${widget.service.points.isNotEmpty ? widget.service.points.length : widget.service.summary.pointCount} 个点',
              style: const TextStyle(fontSize: 11.5, color: AppTheme.textDim),
            ),
          ],
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.of(context).pop(),
          child: const Text('取消'),
        ),
        ElevatedButton(
          key: const ValueKey('btn_confirm_export'),
          onPressed: _isExporting
              ? null
              : () {
                  _doExport();
                },
          style: ElevatedButton.styleFrom(
            backgroundColor: AppTheme.primaryBlue,
            foregroundColor: Colors.white,
            elevation: 0,
          ),
          child: Text(_isExporting ? '导出中...' : '开始导出'),
        ),
      ],
    );
  }

  Widget _buildFormatRadio(String fmt, String title, String subtitle) {
    final active = _format == fmt;
    return Expanded(
      child: InkWell(
        onTap: () => _onFormatChanged(fmt),
        borderRadius: BorderRadius.circular(6),
        child: Container(
          padding: const EdgeInsets.all(10),
          decoration: BoxDecoration(
            color: active
                ? AppTheme.primaryBlue.withAlpha(25)
                : AppTheme.surfaceMuted,
            borderRadius: BorderRadius.circular(6),
            border: Border.all(
              color: active ? AppTheme.primaryBlue : AppTheme.border,
            ),
          ),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                title,
                style: TextStyle(
                  fontSize: 12,
                  fontWeight: FontWeight.bold,
                  color: active ? AppTheme.primaryBlue : AppTheme.textTitle,
                ),
              ),
              const SizedBox(height: 2),
              Text(
                subtitle,
                style: const TextStyle(fontSize: 10, color: AppTheme.textDim),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
