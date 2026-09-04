// ignore_for_file: deprecated_member_use

import 'package:flutter/material.dart';
import '../ffi/geoscatter3d_service.dart';
import '../theme/app_theme.dart';
import 'welcome_ui_keys.dart';

class GpuSelectionDialog extends StatefulWidget {
  final GeoScatter3dService service;

  const GpuSelectionDialog({
    super.key,
    required this.service,
  });

  @override
  State<GpuSelectionDialog> createState() => _GpuSelectionDialogState();
}

class _GpuSelectionDialogState extends State<GpuSelectionDialog> {
  late int _selectedGpu;

  @override
  void initState() {
    super.initState();
    _selectedGpu = widget.service.activeGpuIndex;
  }

  @override
  Widget build(BuildContext context) {
    final gpus = widget.service.gpus;

    return Dialog(
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
      backgroundColor: AppTheme.surface,
      surfaceTintColor: Colors.transparent,
      child: Container(
        width: 500,
        padding: const EdgeInsets.all(24),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // 标题 (使用 Expanded 避免窄屏溢出)
            Row(
              children: [
                Container(
                  padding: const EdgeInsets.all(8),
                  decoration: BoxDecoration(
                    color: AppTheme.primaryBlueBg,
                    borderRadius: BorderRadius.circular(8),
                  ),
                  child: const Icon(Icons.memory_rounded, color: AppTheme.primaryBlue, size: 20),
                ),
                const SizedBox(width: 12),
                const Expanded(
                  child: Text(
                    '选择图形渲染硬件 (Vulkan GPU)',
                    style: TextStyle(
                      fontSize: 16,
                      fontWeight: FontWeight.w700,
                      color: AppTheme.textTitle,
                    ),
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                  ),
                ),
                const SizedBox(width: 8),
                IconButton(
                  key: WelcomeUiKeys.gpuDialogCloseButton,
                  icon: const Icon(Icons.close, size: 18, color: AppTheme.textDim),
                  onPressed: () => Navigator.of(context).pop(),
                ),
              ],
            ),
            const SizedBox(height: 16),
            const Text(
              '当前设备检测到的可用 Vulkan 物理显卡列表，推荐选用高性能独立显卡以获得最佳帧率：',
              style: TextStyle(fontSize: 13, color: AppTheme.textDim),
            ),
            const SizedBox(height: 16),

            // 显卡列表 (纯 Material + InkWell + Radio，无 ListTile 闪烁与层级冲突)
            ...gpus.map((gpu) {
              final isSelected = (_selectedGpu == gpu.index);
              return Container(
                key: WelcomeUiKeys.gpuItem(gpu.index),
                margin: const EdgeInsets.only(bottom: 10),
                decoration: BoxDecoration(
                  borderRadius: BorderRadius.circular(8),
                  border: Border.all(
                    color: isSelected ? AppTheme.primaryBlue : AppTheme.border,
                    width: isSelected ? 1.5 : 1.0,
                  ),
                ),
                child: Material(
                  color: isSelected ? AppTheme.primaryBlueBg : AppTheme.surface,
                  borderRadius: BorderRadius.circular(8),
                  child: InkWell(
                    borderRadius: BorderRadius.circular(8),
                    onTap: () => setState(() => _selectedGpu = gpu.index),
                    child: Padding(
                      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 12),
                      child: Row(
                        children: [
                          Radio<int>(
                            value: gpu.index,
                            groupValue: _selectedGpu,
                            activeColor: AppTheme.primaryBlue,
                            onChanged: (val) {
                              if (val != null) setState(() => _selectedGpu = val);
                            },
                          ),
                          const SizedBox(width: 10),
                          Expanded(
                            child: Column(
                              crossAxisAlignment: CrossAxisAlignment.start,
                              children: [
                                Text(
                                  gpu.name,
                                  style: TextStyle(
                                    fontSize: 13.5,
                                    fontWeight: isSelected ? FontWeight.w700 : FontWeight.w500,
                                    color: AppTheme.textTitle,
                                  ),
                                ),
                                const SizedBox(height: 2),
                                Text(
                                  gpu.typeDescription,
                                  style: TextStyle(
                                    fontSize: 12,
                                    color: isSelected ? AppTheme.primaryBlue : AppTheme.textDim,
                                  ),
                                ),
                              ],
                            ),
                          ),
                        ],
                      ),
                    ),
                  ),
                ),
              );
            }),
            const SizedBox(height: 16),

            // 底部按钮
            Row(
              mainAxisAlignment: MainAxisAlignment.end,
              children: [
                TextButton(
                  key: WelcomeUiKeys.gpuDialogCancelButton,
                  onPressed: () => Navigator.of(context).pop(),
                  child: const Text('取消', style: TextStyle(color: AppTheme.textDim)),
                ),
                const SizedBox(width: 10),
                ElevatedButton(
                  key: WelcomeUiKeys.gpuDialogApplyButton,
                  style: ElevatedButton.styleFrom(
                    backgroundColor: AppTheme.primaryBlue,
                    foregroundColor: Colors.white,
                    shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(6)),
                    elevation: 0,
                  ),
                  onPressed: () {
                    widget.service.executeAction('welcome.gpu.set_preferred', {'index': _selectedGpu});
                    Navigator.of(context).pop();
                  },
                  child: const Text('应用并保存设置'),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}
