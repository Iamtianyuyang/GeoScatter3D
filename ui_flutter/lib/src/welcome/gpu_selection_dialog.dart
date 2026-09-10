import 'package:flutter/material.dart';

import '../ffi/geoscatter3d_service.dart';
import '../theme/gs3d_tokens.dart';
import '../widgets/fluid.dart';
import 'welcome_ui_keys.dart';

/// Vulkan 物理显卡选择对话框。
class GpuSelectionDialog extends StatefulWidget {
  const GpuSelectionDialog({super.key, required this.service});

  final GeoScatter3dService service;

  @override
  State<GpuSelectionDialog> createState() => _GpuSelectionDialogState();
}

class _GpuSelectionDialogState extends State<GpuSelectionDialog> {
  late int _selectedGpu;
  String? _error;

  @override
  void initState() {
    super.initState();
    _selectedGpu = widget.service.activeGpuIndex;
  }

  @override
  Widget build(BuildContext context) {
    final gpus = widget.service.gpus;

    return Dialog(
      shape: RoundedRectangleBorder(
        borderRadius: BorderRadius.circular(Gs3dTokens.radiusXl),
      ),
      backgroundColor: Gs3dTokens.surface,
      surfaceTintColor: Colors.transparent,
      child: Container(
        width: 500,
        padding: const EdgeInsets.all(Gs3dTokens.space24),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            if (gpus.isEmpty)
              const Text(
                '未探测到可用 Vulkan 设备，请检查驱动。',
                style: Gs3dType.body,
              ),
            if (_error != null)
              Text(
                _error!,
                style: Gs3dType.caption.copyWith(color: Gs3dTokens.danger),
              ),

            // 标题（Expanded 避免窄屏溢出）。
            Row(
              children: [
                Container(
                  padding: const EdgeInsets.all(Gs3dTokens.space8),
                  decoration: BoxDecoration(
                    color: Gs3dTokens.accentSubtle,
                    borderRadius: BorderRadius.circular(Gs3dTokens.radiusMd),
                  ),
                  child: const Icon(
                    Icons.memory_rounded,
                    color: Gs3dTokens.accent,
                    size: 20,
                  ),
                ),
                const SizedBox(width: Gs3dTokens.space12),
                const Expanded(
                  child: Text(
                    '选择图形渲染硬件 (Vulkan GPU)',
                    style: Gs3dType.title2,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                  ),
                ),
                const SizedBox(width: Gs3dTokens.space8),
                IconButton(
                  key: WelcomeUiKeys.gpuDialogCloseButton,
                  icon: const Icon(
                    Icons.close,
                    size: 18,
                    color: Gs3dTokens.textSecondary,
                  ),
                  onPressed: () => Navigator.of(context).pop(),
                ),
              ],
            ),
            const SizedBox(height: Gs3dTokens.space16),
            Text(
              '当前设备检测到的可用 Vulkan 物理显卡列表，推荐选用高性能独立显卡以获得最佳帧率：',
              style: Gs3dType.body.copyWith(color: Gs3dTokens.textSecondary),
            ),
            const SizedBox(height: Gs3dTokens.space16),

            ...gpus.map((gpu) {
              final isSelected = _selectedGpu == gpu.index;
              return Padding(
                padding: const EdgeInsets.only(bottom: Gs3dTokens.space8 + 2),
                child: Gs3dPressable(
                  onTap: () => setState(() => _selectedGpu = gpu.index),
                  child: AnimatedContainer(
                    key: WelcomeUiKeys.gpuItem(gpu.index),
                    duration: Gs3dMotion.micro,
                    curve: Gs3dMotion.standardCurve,
                    padding: const EdgeInsets.symmetric(
                      horizontal: Gs3dTokens.space12 + 2,
                      vertical: Gs3dTokens.space12,
                    ),
                    decoration: BoxDecoration(
                      color: isSelected
                          ? Gs3dTokens.accentSubtle
                          : Gs3dTokens.surface,
                      borderRadius: BorderRadius.circular(Gs3dTokens.radiusLg),
                      border: Border.all(
                        color: isSelected
                            ? Gs3dTokens.accent
                            : Gs3dTokens.border,
                        width: isSelected ? 1.5 : 1,
                      ),
                    ),
                    child: Row(
                      children: [
                        Icon(
                          isSelected
                              ? Icons.check_circle_rounded
                              : Icons.circle_outlined,
                          size: 18,
                          color: isSelected
                              ? Gs3dTokens.accent
                              : Gs3dTokens.textDisabled,
                        ),
                        const SizedBox(width: Gs3dTokens.space12),
                        Expanded(
                          child: Column(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            children: [
                              Text(
                                gpu.name,
                                style: Gs3dType.body.copyWith(
                                  fontSize: 13.5,
                                  fontWeight: isSelected
                                      ? FontWeight.w700
                                      : FontWeight.w500,
                                  color: Gs3dTokens.textPrimary,
                                ),
                              ),
                              const SizedBox(height: Gs3dTokens.space2),
                              Text(
                                gpu.typeDescription,
                                style: Gs3dType.caption.copyWith(
                                  color: isSelected
                                      ? Gs3dTokens.accent
                                      : Gs3dTokens.textSecondary,
                                ),
                              ),
                            ],
                          ),
                        ),
                      ],
                    ),
                  ),
                ),
              );
            }),
            const SizedBox(height: Gs3dTokens.space8),

            Row(
              mainAxisAlignment: MainAxisAlignment.end,
              children: [
                TextButton(
                  key: WelcomeUiKeys.gpuDialogCancelButton,
                  onPressed: () => Navigator.of(context).pop(),
                  child: const Text('取消'),
                ),
                const SizedBox(width: Gs3dTokens.space8 + 2),
                FilledButton(
                  key: WelcomeUiKeys.gpuDialogApplyButton,
                  onPressed: gpus.isEmpty
                      ? null
                      : () {
                          final result = widget.service.executeAction(
                            'welcome.gpu.set_preferred',
                            {'index': _selectedGpu},
                          );
                          if (result['success'] == true) {
                            Navigator.of(context).pop();
                          } else {
                            setState(
                              () => _error = result['message'] as String?,
                            );
                          }
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
