import 'package:flutter/material.dart';
import '../ffi/geoscatter3d_service.dart';
import '../theme/app_theme.dart';
import 'gpu_selection_dialog.dart';
import 'welcome_ui_keys.dart';

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

  void _showDocsDialog(BuildContext context) {
    showDialog(
      context: context,
      builder: (ctx) => AlertDialog(
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
        backgroundColor: AppTheme.surface,
        title: const Row(
          children: [
            Icon(Icons.menu_book_rounded, color: AppTheme.primaryBlue, size: 22),
            SizedBox(width: 10),
            Text('操作指南与技术标准', style: TextStyle(fontSize: 16, fontWeight: FontWeight.w700)),
          ],
        ),
        content: const SizedBox(
          width: 520,
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                '1. 数据格式规范 (GS3D v2)',
                style: TextStyle(fontSize: 13.5, fontWeight: FontWeight.w700, color: AppTheme.textTitle),
              ),
              SizedBox(height: 4),
              Text(
                '• 强制采用 GS3D v2 显式小端标准，支持单体二进制 (.gs3d) 与分层工区包 (.gs3d.bundle)。\n'
                '• 数据块严格按 point_stride 校验内存对齐区间，杜绝格式损坏与静默容忍。',
                style: TextStyle(fontSize: 12.5, color: AppTheme.textBody, height: 1.5),
              ),
              SizedBox(height: 12),
              Text(
                '2. 空间索引与海量点云渲染',
                style: TextStyle(fontSize: 13.5, fontWeight: FontWeight.w700, color: AppTheme.textTitle),
              ),
              SizedBox(height: 4),
              Text(
                '• 采用动态多级八叉树 LOD 金字塔，按需流式加载与视锥裁剪，毫秒级响应上亿散点。',
                style: TextStyle(fontSize: 12.5, color: AppTheme.textBody, height: 1.5),
              ),
              SizedBox(height: 12),
              Text(
                '3. 视口交互控制快捷指南',
                style: TextStyle(fontSize: 13.5, fontWeight: FontWeight.w700, color: AppTheme.textTitle),
              ),
              SizedBox(height: 4),
              Text(
                '• 旋转视角：鼠标左键按住拖拽\n'
                '• 平移视口：鼠标中键按住拖拽 或 Shift + 鼠标左键\n'
                '• 缩放视野：滚动鼠标滚轮\n'
                '• 复位视角：工作台顶部工具栏「视角复位」按钮',
                style: TextStyle(fontSize: 12.5, color: AppTheme.textBody, height: 1.5),
              ),
            ],
          ),
        ),
        actions: [
          ElevatedButton(
            key: WelcomeUiKeys.docsDialogCloseButton,
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
        content: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text(
              'GeoScatter3D 三维散点数据流式可视化系统',
              style: TextStyle(fontSize: 14, fontWeight: FontWeight.w600, color: AppTheme.textTitle),
            ),
            const SizedBox(height: 8),
            const Text(
              '• 跨平台 Flutter + Vulkan 1.3 现代桌面/移动架构\n'
              '• GS3D v2 显式小端标准与八叉树 LOD 金字塔\n'
              '• C-ABI 高性能 FFI 桥接，亿级点云零开销流式加载\n'
              '• 全异步多线程解耦与 JSON-RPC 控制面',
              style: TextStyle(fontSize: 13, color: AppTheme.textBody, height: 1.6),
            ),
            const SizedBox(height: 12),
            Text('引擎内核版本: ${service.engineVersion}', style: const TextStyle(fontSize: 12, color: AppTheme.textDim)),
          ],
        ),
        actions: [
          ElevatedButton(
            key: WelcomeUiKeys.aboutDialogCloseButton,
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
                key: WelcomeUiKeys.footerGpuButton,
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

              // 操作技术指南
              TextButton.icon(
                key: WelcomeUiKeys.footerDocsButton,
                onPressed: () => _showDocsDialog(context),
                icon: const Icon(Icons.menu_book_rounded, size: 15),
                label: const Text('技术指南', style: TextStyle(fontSize: 12)),
                style: TextButton.styleFrom(
                  foregroundColor: AppTheme.textDim,
                  padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
                  minimumSize: Size.zero,
                  tapTargetSize: MaterialTapTargetSize.shrinkWrap,
                ),
              ),
              const SizedBox(width: 4),

              // 关于
              TextButton.icon(
                key: WelcomeUiKeys.footerAboutButton,
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
