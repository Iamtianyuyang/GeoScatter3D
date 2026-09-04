import 'package:flutter/material.dart';
import '../ffi/geoscatter3d_service.dart';
import '../theme/app_theme.dart';
import 'gpu_selection_dialog.dart';

class WelcomeFooter extends StatelessWidget {
  final GeoScatter3dService service;

  const WelcomeFooter({
    super.key,
    required this.service,
  });

  void _showGpuDialog(BuildContext context) {
    showDialog(
      context: context,
      barrierDismissible: true,
      builder: (_) => GpuSelectionDialog(service: service),
    );
  }

  void _showAboutDialog(BuildContext context) {
    showDialog(
      context: context,
      builder: (ctx) => AlertDialog(
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
        backgroundColor: AppTheme.surface,
        title: const Row(
          children: [
            Icon(Icons.scatter_plot_rounded, color: AppTheme.primaryBlue, size: 24),
            SizedBox(width: 10),
            Text('关于 GeoScatter3D', style: TextStyle(fontSize: 16, fontWeight: FontWeight.w700)),
          ],
        ),
        content: const Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              'GeoScatter3D 三维散点数据流式可视化系统',
              style: TextStyle(fontSize: 14, fontWeight: FontWeight.w600, color: AppTheme.textTitle),
            ),
            SizedBox(height: 8),
            Text(
              '• 跨平台 Flutter + Vulkan 1.3 现代桌面/移动架构\n'
              '• GS3D v2 显式小端标准与八叉树 LOD 金字塔\n'
              '• C-ABI 高性能 FFI 桥接，亿级点云零开销流式加载\n'
              '• 全异步多线程解耦与 JSON-RPC 控制面',
              style: TextStyle(fontSize: 13, color: AppTheme.textBody, height: 1.6),
            ),
            SizedBox(height: 12),
            Text('Version: 2.0.0-rc1 (Build 2026.09)', style: TextStyle(fontSize: 12, color: AppTheme.textDim)),
          ],
        ),
        actions: [
          ElevatedButton(
            onPressed: () => Navigator.of(ctx).pop(),
            style: ElevatedButton.styleFrom(
              backgroundColor: AppTheme.primaryBlue,
              foregroundColor: Colors.white,
              elevation: 0,
            ),
            child: const Text('我知道了'),
          ),
        ],
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: service,
      builder: (context, _) {
        final gpus = service.gpus;
        final activeIdx = service.activeGpuIndex;
        final activeGpuName = (activeIdx >= 0 && activeIdx < gpus.length)
            ? gpus[activeIdx].name
            : '默认图形适配器';

        return Container(
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
          decoration: const BoxDecoration(
            color: AppTheme.surface,
            border: Border(top: BorderSide(color: AppTheme.border)),
          ),
          child: Row(
            children: [
              // GPU 选择 Chip (自适应最大宽度，防止溢出)
              InkWell(
                onTap: () => _showGpuDialog(context),
                borderRadius: BorderRadius.circular(6),
                child: Container(
                  constraints: const BoxConstraints(maxWidth: 320),
                  padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
                  decoration: BoxDecoration(
                    color: AppTheme.surfaceMuted,
                    borderRadius: BorderRadius.circular(6),
                    border: Border.all(color: AppTheme.border),
                  ),
                  child: Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      const Icon(Icons.memory_rounded, size: 16, color: AppTheme.primaryBlue),
                      const SizedBox(width: 6),
                      Flexible(
                        child: Text(
                          '设备: $activeGpuName',
                          style: const TextStyle(
                            fontSize: 12,
                            fontWeight: FontWeight.w500,
                            color: AppTheme.textBody,
                          ),
                          overflow: TextOverflow.ellipsis,
                          maxLines: 1,
                        ),
                      ),
                      const SizedBox(width: 4),
                      const Icon(Icons.arrow_drop_down, size: 16, color: AppTheme.textDim),
                    ],
                  ),
                ),
              ),
              const SizedBox(width: 12),

              // 运行就绪状态
              Container(
                width: 7,
                height: 7,
                decoration: const BoxDecoration(
                  color: Color(0xFF10B981), // Emerald-500
                  shape: BoxShape.circle,
                ),
              ),
              const SizedBox(width: 6),
              const Expanded(
                child: Text(
                  'Vulkan 1.3 / GS3D v2 核心引擎就绪',
                  style: TextStyle(
                    fontSize: 12,
                    color: AppTheme.textDim,
                  ),
                  overflow: TextOverflow.ellipsis,
                  maxLines: 1,
                ),
              ),

              const SizedBox(width: 8),

              // 文档与关于
              TextButton.icon(
                onPressed: () => _showAboutDialog(context),
                icon: const Icon(Icons.info_outline_rounded, size: 15),
                label: const Text('关于系统', style: TextStyle(fontSize: 12)),
                style: TextButton.styleFrom(
                  foregroundColor: AppTheme.textDim,
                  padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
                  minimumSize: Size.zero,
                  tapTargetSize: MaterialTapTargetSize.shrinkWrap,
                ),
              ),
            ],
          ),
        );
      },
    );
  }
}
