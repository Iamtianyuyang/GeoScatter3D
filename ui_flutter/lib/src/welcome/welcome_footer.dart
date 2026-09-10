import 'package:flutter/material.dart';

import '../ffi/geoscatter3d_service.dart';
import '../theme/gs3d_tokens.dart';
import '../widgets/fluid.dart';
import 'gpu_selection_dialog.dart';
import 'welcome_ui_keys.dart';

/// 欢迎页底部工具条（独立形态）。
///
/// 展示当前 GPU、后端状态，并提供 GPU 管理 / 技术指南 / 关于三个入口。
class WelcomeFooter extends StatelessWidget {
  const WelcomeFooter({super.key, required this.service});

  final GeoScatter3dService service;

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
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(Gs3dTokens.radiusXl),
        ),
        backgroundColor: Gs3dTokens.surface,
        title: const Row(
          children: [
            Icon(Icons.menu_book_rounded, color: Gs3dTokens.accent, size: 22),
            SizedBox(width: Gs3dTokens.space8 + 2),
            Text('操作指南与技术标准', style: Gs3dType.title2),
          ],
        ),
        content: const SizedBox(
          width: 520,
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              _DocSection(
                title: '1. 数据格式规范 (GS3D v2)',
                body:
                    '• 强制采用 GS3D v2 显式小端标准，支持单体二进制 (.gs3d) 与分层工区包 (.gs3d.bundle)。\n'
                    '• 数据块严格按 point_stride 校验内存对齐区间，杜绝格式损坏与静默容忍。',
              ),
              _DocSection(
                title: '2. 空间索引与海量点云渲染',
                body: '• 采用动态多级八叉树 LOD 金字塔，按需流式加载与视锥裁剪，毫秒级响应上亿散点。',
              ),
              _DocSection(
                title: '3. 视口交互控制快捷指南',
                body:
                    '• 旋转视角：鼠标左键按住拖拽\n'
                    '• 平移视口：鼠标右键或中键按住拖拽（也可 Shift+左键）\n'
                    '• 缩放视野：滚动鼠标滚轮\n'
                    '• 复位视角：工作台顶部工具栏「视角复位」按钮',
              ),
            ],
          ),
        ),
        actions: [
          FilledButton(
            key: WelcomeUiKeys.docsDialogCloseButton,
            onPressed: () => Navigator.of(ctx).pop(),
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
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(Gs3dTokens.radiusXl),
        ),
        backgroundColor: Gs3dTokens.surface,
        title: const Row(
          children: [
            Icon(
              Icons.scatter_plot_rounded,
              color: Gs3dTokens.accent,
              size: 24,
            ),
            SizedBox(width: Gs3dTokens.space8 + 2),
            Text('关于 GeoScatter3D', style: Gs3dType.title2),
          ],
        ),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text(
              'GeoScatter3D 三维散点数据流式可视化系统',
              style: Gs3dType.bodyStrong,
            ),
            const SizedBox(height: Gs3dTokens.space8),
            const Text(
              '• 跨平台 Flutter + Vulkan 1.3 现代桌面架构\n'
              '• GS3D v2 显式小端标准与八叉树 LOD 金字塔\n'
              '• C-ABI 高性能 FFI 桥接，亿级点云零开销流式加载\n'
              '• 全异步多线程解耦与 JSON-RPC 控制面',
              style: TextStyle(
                fontSize: 13,
                color: Gs3dTokens.textSecondary,
                height: 1.6,
              ),
            ),
            const SizedBox(height: Gs3dTokens.space12),
            Text('引擎内核版本: ${service.engineVersion}', style: Gs3dType.caption),
          ],
        ),
        actions: [
          FilledButton(
            key: WelcomeUiKeys.aboutDialogCloseButton,
            onPressed: () => Navigator.of(ctx).pop(),
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
          padding: const EdgeInsets.symmetric(
            horizontal: Gs3dTokens.space20,
            vertical: Gs3dTokens.space8,
          ),
          decoration: const BoxDecoration(
            color: Gs3dTokens.surfaceHeader,
            border: Border(
              top: BorderSide(color: Gs3dTokens.hairline),
            ),
          ),
          child: Row(
            children: [
              // GPU 选择胶囊。
              Gs3dPressable(
                onTap: () => _showGpuDialog(context),
                child: Container(
                  key: WelcomeUiKeys.footerGpuButton,
                  constraints: const BoxConstraints(maxWidth: 320),
                  padding: const EdgeInsets.symmetric(
                    horizontal: Gs3dTokens.space8 + 2,
                    vertical: 5,
                  ),
                  decoration: BoxDecoration(
                    color: Gs3dTokens.surfaceSunken,
                    borderRadius: BorderRadius.circular(Gs3dTokens.radiusMd),
                    border: Border.all(color: Gs3dTokens.border),
                  ),
                  child: Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      const Icon(
                        Icons.memory_rounded,
                        size: 15,
                        color: Gs3dTokens.accent,
                      ),
                      const SizedBox(width: 6),
                      Flexible(
                        child: Text(
                          '设备: $activeGpuName',
                          style: Gs3dType.bodyStrong.copyWith(fontSize: 11.5),
                          overflow: TextOverflow.ellipsis,
                          maxLines: 1,
                        ),
                      ),
                      const SizedBox(width: Gs3dTokens.space4),
                      const Icon(
                        Icons.arrow_drop_down,
                        size: 16,
                        color: Gs3dTokens.textTertiary,
                      ),
                    ],
                  ),
                ),
              ),
              const SizedBox(width: Gs3dTokens.space12),

              // 运行就绪指示灯。
              Gs3dStatusDot(
                color: service.isInitialized
                    ? Gs3dTokens.success
                    : Gs3dTokens.warning,
                size: 7,
              ),
              const SizedBox(width: 7),
              Expanded(
                child: Text(
                  service.backendStatus,
                  style: Gs3dType.caption,
                  overflow: TextOverflow.ellipsis,
                  maxLines: 1,
                ),
              ),

              const SizedBox(width: Gs3dTokens.space8),

              _FooterAction(
                key: WelcomeUiKeys.footerDocsButton,
                icon: Icons.menu_book_rounded,
                label: '技术指南',
                onTap: () => _showDocsDialog(context),
              ),
              const SizedBox(width: Gs3dTokens.space4),
              _FooterAction(
                key: WelcomeUiKeys.footerAboutButton,
                icon: Icons.info_outline_rounded,
                label: '关于系统',
                onTap: () => _showAboutDialog(context),
              ),
            ],
          ),
        );
      },
    );
  }
}

class _FooterAction extends StatelessWidget {
  const _FooterAction({
    super.key,
    required this.icon,
    required this.label,
    required this.onTap,
  });

  final IconData icon;
  final String label;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return Gs3dPillButton(label: label, icon: icon, onPressed: onTap, dense: true);
  }
}

class _DocSection extends StatelessWidget {
  const _DocSection({required this.title, required this.body});

  final String title;
  final String body;

  @override
  Widget build(BuildContext context) {
    return Column(
      mainAxisSize: MainAxisSize.min,
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(title, style: Gs3dType.headline),
        const SizedBox(height: Gs3dTokens.space4),
        Text(
          body,
          style: const TextStyle(
            fontSize: 12.5,
            color: Gs3dTokens.textSecondary,
            height: 1.5,
          ),
        ),
        const SizedBox(height: Gs3dTokens.space12),
      ],
    );
  }
}
