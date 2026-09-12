import 'package:flutter/material.dart';
import '../ffi/geoscatter3d_service.dart';
import '../theme/gs3d_form_factor.dart';
import '../theme/gs3d_tokens.dart';
import '../welcome/gpu_selection_dialog.dart';
import '../welcome/new_project_dialog.dart';
import '../welcome/open_project_dialog.dart';
import '../welcome/welcome_ui_keys.dart';
import '../widgets/fluid.dart';
import 'export_dialog.dart';

class TopMenuBar extends StatelessWidget {
  final GeoScatter3dService service;

  const TopMenuBar({super.key, required this.service});

  // 对话框弹出统一收敛到 Gs3dResponsiveDialog：expanded 档保持居中模态
  // （桌面行为零变化），compact/tablet 档自动落底部 sheet（拇指可达）。
  void _showExportDialog(BuildContext context) {
    Gs3dResponsiveDialog.show(
      context,
      title: '导出点云数据',
      builder: (_) => ExportPointCloudDialog(service: service),
    );
  }

  void _showNewProjectDialog(BuildContext context) {
    Gs3dResponsiveDialog.show(
      context,
      title: '新建散点工程',
      builder: (_) => NewProjectDialog(service: service),
    );
  }

  void _showOpenProjectDialog(BuildContext context) {
    Gs3dResponsiveDialog.show(
      context,
      title: '打开工程数据包',
      builder: (_) => OpenProjectDialog(service: service),
    );
  }

  void _showGpuDialog(BuildContext context) {
    Gs3dResponsiveDialog.show(
      context,
      title: '选择首选渲染 GPU',
      builder: (_) => GpuSelectionDialog(service: service),
    );
  }

  Future<void> _loadBuiltInSample(BuildContext context) async {
    final result = await service.executeActionAsync('welcome.quick_demo');
    if (!context.mounted || result['success'] == true) return;
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(
        content: Text(result['message'] as String? ?? '内置示例加载失败'),
        backgroundColor: Gs3dTokens.danger,
      ),
    );
  }

  void _showDocsDialog(BuildContext context) {
    showDialog(
      context: context,
      builder: (ctx) => AlertDialog(
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
        backgroundColor: Gs3dTokens.surface,
        title: const Row(
          children: [
            Icon(Icons.menu_book_rounded, color: Gs3dTokens.accent, size: 22),
            SizedBox(width: 10),
            Text(
              '操作指南与技术标准',
              style: TextStyle(fontSize: 16, fontWeight: FontWeight.w700),
            ),
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
                style: TextStyle(
                  fontSize: 13.5,
                  fontWeight: FontWeight.w700,
                  color: Gs3dTokens.textPrimary,
                ),
              ),
              SizedBox(height: 4),
              Text(
                '• 强制采用 GS3D v2 显式小端标准，支持单体二进制 (.gs3d) 与分层工区包 (.gs3d.bundle)。\n'
                '• 数据块严格按 point_stride 校验内存对齐区间，杜绝格式损坏与静默容忍。',
                style: TextStyle(
                  fontSize: 12.5,
                  color: Gs3dTokens.textBody,
                  height: 1.5,
                ),
              ),
              SizedBox(height: 12),
              Text(
                '2. 空间索引与海量点云渲染',
                style: TextStyle(
                  fontSize: 13.5,
                  fontWeight: FontWeight.w700,
                  color: Gs3dTokens.textPrimary,
                ),
              ),
              SizedBox(height: 4),
              Text(
                '• 采用动态多级八叉树 LOD 金字塔，按需流式加载与视锥裁剪，毫秒级响应上亿散点。',
                style: TextStyle(
                  fontSize: 12.5,
                  color: Gs3dTokens.textBody,
                  height: 1.5,
                ),
              ),
              SizedBox(height: 12),
              Text(
                '3. 视口交互控制快捷指南',
                style: TextStyle(
                  fontSize: 13.5,
                  fontWeight: FontWeight.w700,
                  color: Gs3dTokens.textPrimary,
                ),
              ),
              SizedBox(height: 4),
              Text(
                '• 旋转视角：鼠标左键按住拖拽\n'
                '• 平移视口：鼠标右键/中键按住拖拽，或开启平移模式后左键拖拽\n'
                '• 缩放视野：滚动鼠标滚轮 或 点击放大/缩小按钮\n'
                '• 复位视角：快捷工具栏「复位视角 R」',
                style: TextStyle(
                  fontSize: 12.5,
                  color: Gs3dTokens.textBody,
                  height: 1.5,
                ),
              ),
            ],
          ),
        ),
        actions: [
          ElevatedButton(
            key: WorkbenchUiKeys.menuItemDocs,
            onPressed: () => Navigator.of(ctx).pop(),
            style: ElevatedButton.styleFrom(
              backgroundColor: Gs3dTokens.accent,
              foregroundColor: Gs3dTokens.textOnAccent,
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
        backgroundColor: Gs3dTokens.surface,
        title: const Row(
          children: [
            Icon(
              Icons.scatter_plot_rounded,
              color: Gs3dTokens.accent,
              size: 24,
            ),
            SizedBox(width: 10),
            Text(
              '关于 GeoScatter3D',
              style: TextStyle(fontSize: 16, fontWeight: FontWeight.w700),
            ),
          ],
        ),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text(
              'GeoScatter3D 三维散点数据流式可视化系统',
              style: TextStyle(
                fontSize: 14,
                fontWeight: FontWeight.w600,
                color: Gs3dTokens.textPrimary,
              ),
            ),
            const SizedBox(height: 8),
            const Text(
              '• 跨平台 Flutter + Vulkan 1.3 现代桌面/移动架构\n'
              '• GS3D v2 显式小端标准与八叉树 LOD 金字塔\n'
              '• C-ABI 高性能 FFI 桥接，亿级点云零开销流式加载\n'
              '• 全异步多线程解耦与 AI JSON-RPC 控制面',
              style: TextStyle(
                fontSize: 13,
                color: Gs3dTokens.textBody,
                height: 1.6,
              ),
            ),
            const SizedBox(height: 12),
            Text(
              '引擎内核版本: ${service.engineVersion}',
              style: const TextStyle(
                fontSize: 12,
                color: Gs3dTokens.textSecondary,
              ),
            ),
          ],
        ),
        actions: [
          ElevatedButton(
            key: WorkbenchUiKeys.menuItemAbout,
            onPressed: () => Navigator.of(ctx).pop(),
            style: ElevatedButton.styleFrom(
              backgroundColor: Gs3dTokens.accent,
              foregroundColor: Gs3dTokens.textOnAccent,
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
    // 触控档（compact/tablet）：菜单收敛为触控工具条（高 56 + 命令按钮 +
    // 布局切换）；expanded 档保持既有桌面菜单条，像素零变化。
    if (context.isTouchPreferred) {
      return _buildTouchToolbar(context);
    }
    return Container(
      key: WorkbenchUiKeys.topMenuBar,
      height: 38,
      padding: const EdgeInsets.symmetric(horizontal: 14),
      decoration: const BoxDecoration(
        color: Gs3dTokens.surfaceHeader,
        border: Border(bottom: BorderSide(color: Gs3dTokens.hairline)),
      ),
      child: Row(
        children: [
          // Logo & 返回欢迎页
          Gs3dPressable(
            onTap: () => service.closeWorkbench(),
            hoverScale: 0,
            key: WorkbenchUiKeys.homeButton,
            builder: (context, state) => AnimatedContainer(
              duration: const Duration(milliseconds: 140),
              curve: Gs3dMotion.standardCurve,
              padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 4),
              decoration: BoxDecoration(
                color: state == Gs3dInteractionState.idle
                    ? Colors.transparent
                    : Gs3dTokens.surfaceHover,
                borderRadius: BorderRadius.circular(Gs3dTokens.radiusSm),
              ),
              child: Row(
                children: [
                  const Icon(
                    Icons.home_outlined,
                    size: 17,
                    color: Gs3dTokens.accent,
                  ),
                  const SizedBox(width: 6),
                  Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      const Text('GeoScatter', style: Gs3dType.brand),
                      const Text(' 3D', style: Gs3dType.brandAccent),
                    ],
                  ),
                ],
              ),
            ),
          ),
          const SizedBox(width: 14),

          // 菜单项
          _buildFileMenu(context),
          _buildViewMenu(context),
          _buildHelpMenu(context),

          const Spacer(),

          // 居中数据集名称
          ListenableBuilder(
            listenable: service,
            builder: (context, _) {
              return Text(
                service.summary.name.isEmpty ? '未载入工程' : service.summary.name,
                style: const TextStyle(
                  fontSize: 13,
                  fontWeight: FontWeight.w500,
                  color: Gs3dTokens.textSecondary,
                ),
              );
            },
          ),

          const Spacer(),

          // 渲染与外观悬浮设置按钮
          ListenableBuilder(
            listenable: service,
            builder: (context, _) => TextButton.icon(
              key: const ValueKey('workbench.menu.btn_render_settings'),
              style: TextButton.styleFrom(
                foregroundColor: service.isRenderSettingsOpen
                    ? Gs3dTokens.accent
                    : Gs3dTokens.textPrimary,
                backgroundColor: service.isRenderSettingsOpen
                    ? Gs3dTokens.surfaceHover
                    : Colors.transparent,
                padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
              ),
              icon: const Icon(Icons.tune_rounded, size: 16),
              label: const Text(
                '渲染外观',
                style: TextStyle(fontSize: 12.5, fontWeight: FontWeight.w600),
              ),
              onPressed: () => service.toggleRenderSettings(),
            ),
          ),
          const SizedBox(width: 8),

          // 快速打开数据按钮
          TextButton.icon(
            key: WorkbenchUiKeys.quickLoadDemoButton,
            style: TextButton.styleFrom(
              foregroundColor: Gs3dTokens.accent,
              padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
            ),
            icon: const Icon(Icons.folder_open_rounded, size: 16),
            label: const Text(
              '载入示例',
              style: TextStyle(fontSize: 12.5, fontWeight: FontWeight.w600),
            ),
            onPressed: () {
              _loadBuiltInSample(context);
            },
          ),
          const SizedBox(width: 6),

          // 返回欢迎页按钮
          TextButton.icon(
            key: const ValueKey('workbench.menu.btn_welcome'),
            style: TextButton.styleFrom(
              foregroundColor: Gs3dTokens.textSecondary,
              padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
            ),
            icon: const Icon(Icons.arrow_back_rounded, size: 15),
            label: const Text('欢迎页', style: TextStyle(fontSize: 12.5)),
            onPressed: () => service.closeWorkbench(),
          ),
        ],
      ),
    );
  }

  Widget _buildFileMenu(BuildContext context) {
    return PopupMenuButton<String>(
      key: WorkbenchUiKeys.menuFile,
      tooltip: '文件',
      offset: const Offset(0, 30),
      onSelected: (value) {
        switch (value) {
          case 'new_project':
            _showNewProjectDialog(context);
            break;
          case 'open_project':
            _showOpenProjectDialog(context);
            break;
          case 'load_sample':
            _loadBuiltInSample(context);
            break;
          case 'screenshot':
            service.requestScreenshot();
            break;
          case 'export':
            _showExportDialog(context);
            break;
          case 'close':
            service.closeWorkbench();
            break;
        }
      },
      itemBuilder: (context) => [
        const PopupMenuItem(
          key: WorkbenchUiKeys.menuItemNewProject,
          value: 'new_project',
          child: Row(
            children: [
              Icon(Icons.add_box_outlined, size: 16, color: Gs3dTokens.accent),
              SizedBox(width: 8),
              Text('新建工程...'),
            ],
          ),
        ),
        const PopupMenuItem(
          key: WorkbenchUiKeys.menuItemOpenProject,
          value: 'open_project',
          child: Row(
            children: [
              Icon(
                Icons.folder_open_rounded,
                size: 16,
                color: Gs3dTokens.accent,
              ),
              SizedBox(width: 8),
              Text('打开工程...'),
            ],
          ),
        ),
        const PopupMenuItem(
          key: WorkbenchUiKeys.menuItemQuickDemo,
          value: 'load_sample',
          child: Row(
            children: [
              Icon(
                Icons.scatter_plot_rounded,
                size: 16,
                color: Gs3dTokens.success,
              ),
              SizedBox(width: 8),
              Text('载入示例数据'),
            ],
          ),
        ),
        const PopupMenuDivider(),
        const PopupMenuItem(
          key: WorkbenchUiKeys.menuItemScreenshot,
          value: 'screenshot',
          child: Row(
            children: [
              Icon(
                Icons.camera_alt_outlined,
                size: 16,
                color: Gs3dTokens.accent,
              ),
              SizedBox(width: 8),
              Text('视口截图'),
            ],
          ),
        ),
        const PopupMenuItem(
          key: ValueKey('workbench.menu.item_export'),
          value: 'export',
          child: Row(
            children: [
              Icon(
                Icons.file_download_outlined,
                size: 16,
                color: Gs3dTokens.accent,
              ),
              SizedBox(width: 8),
              Text('导出点云 (PLY/CSV)...'),
            ],
          ),
        ),
        const PopupMenuDivider(),
        const PopupMenuItem(
          key: WorkbenchUiKeys.menuItemWelcome,
          value: 'close',
          child: Row(
            children: [
              Icon(
                Icons.home_outlined,
                size: 16,
                color: Gs3dTokens.textSecondary,
              ),
              SizedBox(width: 8),
              Text('返回欢迎页'),
            ],
          ),
        ),
      ],
      child: _buildMenuLabel('文件'),
    );
  }

  Widget _buildViewMenu(BuildContext context) {
    return PopupMenuButton<String>(
      key: WorkbenchUiKeys.menuView,
      tooltip: '视图',
      offset: const Offset(0, 30),
      onSelected: (value) {
        switch (value) {
          case 'reset':
            service.resetCamera();
            break;
          case 'top':
            service.setCameraView(preset: 'top');
            break;
          case 'front':
            service.setCameraView(preset: 'front');
            break;
          case 'side':
            service.setCameraView(preset: 'side');
            break;
          case 'iso':
            service.setCameraView(preset: 'iso');
            break;
          case 'render_settings':
            service.toggleRenderSettings();
            break;
          case 'theme_next':
            service.cycleTheme();
            break;
        }
      },
      itemBuilder: (context) => [
        const PopupMenuItem(
          key: WorkbenchUiKeys.menuItemResetCamera,
          value: 'reset',
          child: Row(
            children: [
              Icon(Icons.refresh_rounded, size: 16, color: Gs3dTokens.accent),
              SizedBox(width: 8),
              Text('复位视角 (Home / R)'),
            ],
          ),
        ),
        PopupMenuItem(
          key: const ValueKey('workbench.menu.item_render_settings'),
          value: 'render_settings',
          child: Row(
            children: [
              const Icon(Icons.tune_rounded, size: 16, color: Gs3dTokens.accent),
              const SizedBox(width: 8),
              Text(service.isRenderSettingsOpen ? '关闭渲染外观' : '渲染外观面板...'),
            ],
          ),
        ),
        const PopupMenuDivider(),
        PopupMenuItem(
          key: const ValueKey('workbench.menu.item_theme_next'),
          value: 'theme_next',
          child: Row(
            children: [
              const Icon(
                Icons.palette_outlined,
                size: 16,
                color: Gs3dTokens.accent,
              ),
              const SizedBox(width: 8),
              Text('切换主题 (${service.themeName})'),
            ],
          ),
        ),
        const PopupMenuDivider(),
        const PopupMenuItem(
          key: WorkbenchUiKeys.menuItemTopView,
          value: 'top',
          child: Row(
            children: [
              Icon(
                Icons.border_top_rounded,
                size: 16,
                color: Gs3dTokens.textBody,
              ),
              SizedBox(width: 8),
              Text('顶视图 (Top)'),
            ],
          ),
        ),
        const PopupMenuItem(
          key: WorkbenchUiKeys.menuItemFrontView,
          value: 'front',
          child: Row(
            children: [
              Icon(
                Icons.stay_current_portrait_rounded,
                size: 16,
                color: Gs3dTokens.textBody,
              ),
              SizedBox(width: 8),
              Text('前视图 (Front)'),
            ],
          ),
        ),
        const PopupMenuItem(
          key: WorkbenchUiKeys.menuItemSideView,
          value: 'side',
          child: Row(
            children: [
              Icon(
                Icons.stay_current_landscape_rounded,
                size: 16,
                color: Gs3dTokens.textBody,
              ),
              SizedBox(width: 8),
              Text('侧视图 (Side)'),
            ],
          ),
        ),
        const PopupMenuItem(
          key: WorkbenchUiKeys.menuItemIsoView,
          value: 'iso',
          child: Row(
            children: [
              Icon(
                Icons.view_in_ar_rounded,
                size: 16,
                color: Gs3dTokens.textBody,
              ),
              SizedBox(width: 8),
              Text('等轴测 (Isometric)'),
            ],
          ),
        ),
      ],
      child: _buildMenuLabel('视图'),
    );
  }

  Widget _buildHelpMenu(BuildContext context) {
    return PopupMenuButton<String>(
      key: WorkbenchUiKeys.menuHelp,
      tooltip: '帮助',
      offset: const Offset(0, 30),
      onSelected: (value) {
        switch (value) {
          case 'shortcuts':
            service.toggleShortcutOverlay(true);
            break;
          case 'gpu':
            _showGpuDialog(context);
            break;
          case 'docs':
            _showDocsDialog(context);
            break;
          case 'about':
            _showAboutDialog(context);
            break;
        }
      },
      itemBuilder: (context) => [
        const PopupMenuItem(
          key: ValueKey('workbench.menu.item_shortcuts'),
          value: 'shortcuts',
          child: Row(
            children: [
              Icon(Icons.keyboard_outlined, size: 16, color: Gs3dTokens.accent),
              SizedBox(width: 8),
              Text('快捷操作速查 (F1)...'),
            ],
          ),
        ),
        const PopupMenuItem(
          key: WorkbenchUiKeys.menuItemGpu,
          value: 'gpu',
          child: Row(
            children: [
              Icon(Icons.memory_rounded, size: 16, color: Gs3dTokens.accent),
              SizedBox(width: 8),
              Text('GPU 设备与运行状态...'),
            ],
          ),
        ),
        const PopupMenuItem(
          key: WorkbenchUiKeys.menuItemDocs,
          value: 'docs',
          child: Row(
            children: [
              Icon(Icons.menu_book_rounded, size: 16, color: Gs3dTokens.accent),
              SizedBox(width: 8),
              Text('技术标准与操作指南...'),
            ],
          ),
        ),
        const PopupMenuDivider(),
        const PopupMenuItem(
          key: WorkbenchUiKeys.menuItemAbout,
          value: 'about',
          child: Row(
            children: [
              Icon(
                Icons.info_outline_rounded,
                size: 16,
                color: Gs3dTokens.textSecondary,
              ),
              SizedBox(width: 8),
              Text('关于 GeoScatter 3D...'),
            ],
          ),
        ),
      ],
      child: _buildMenuLabel('帮助'),
    );
  }

  Widget _buildMenuLabel(String title) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 6),
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
        decoration: BoxDecoration(borderRadius: BorderRadius.circular(4)),
        child: Text(
          title,
          style: const TextStyle(
            fontSize: 13,
            fontWeight: FontWeight.w500,
            color: Gs3dTokens.textPrimary,
          ),
        ),
      ),
    );
  }

  /// 触控工具条（tablet/compact 档，高 56，命中区 ≥48）。
  Widget _buildTouchToolbar(BuildContext context) {
    if (context.gs3dFormFactor == Gs3dFormFactorSlot.compact) {
      return _buildCompactTouchToolbar(context);
    }

    return Container(
      key: WorkbenchUiKeys.topMenuBar,
      height: Gs3dTokens.topBarTouchHeight,
      padding: const EdgeInsets.symmetric(horizontal: Gs3dTokens.space12),
      decoration: const BoxDecoration(
        color: Gs3dTokens.surfaceHeader,
        border: Border(bottom: BorderSide(color: Gs3dTokens.hairline)),
      ),
      child: Row(
        children: [
          // 品牌与返回欢迎页
          Gs3dPressable(
            onTap: () => service.closeWorkbench(),
            hoverScale: 0,
            minTouchSize: Gs3dTokens.hitTargetTouch,
            key: WorkbenchUiKeys.homeButton,
            builder: (context, state) => AnimatedContainer(
              duration: const Duration(milliseconds: 140),
              curve: Gs3dMotion.standardCurve,
              padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 4),
              decoration: BoxDecoration(
                color: state == Gs3dInteractionState.idle
                    ? Colors.transparent
                    : Gs3dTokens.surfaceHover,
                borderRadius: BorderRadius.circular(Gs3dTokens.radiusSm),
              ),
              child: Row(
                children: [
                  const Icon(
                    Icons.home_outlined,
                    size: 20,
                    color: Gs3dTokens.accent,
                  ),
                  const SizedBox(width: 6),
                  Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      const Text('GeoScatter', style: Gs3dType.brand),
                      const Text(' 3D', style: Gs3dType.brandAccent),
                    ],
                  ),
                ],
              ),
            ),
          ),
          const SizedBox(width: Gs3dTokens.space12),

          // 渲染外观面板触发按钮
          ListenableBuilder(
            listenable: service,
            builder: (context, _) => Gs3dPillButton(
              key: const ValueKey('workbench.menu.btn_render_settings'),
              label: '渲染外观',
              icon: Icons.tune_rounded,
              active: service.isRenderSettingsOpen,
              touchTarget: true,
              onPressed: () => service.toggleRenderSettings(),
            ),
          ),
          const Spacer(),

          // 居中数据集名称。
          ListenableBuilder(
            listenable: service,
            builder: (context, _) {
              return Flexible(
                child: Text(
                  service.summary.name.isEmpty ? '未载入工程' : service.summary.name,
                  overflow: TextOverflow.ellipsis,
                  style: const TextStyle(
                    fontSize: 13,
                    fontWeight: FontWeight.w500,
                    color: Gs3dTokens.textSecondary,
                    fontFamily: Gs3dType.fontFamily,
                  ),
                ),
              );
            },
          ),

          const Spacer(),

          // 常驻「命令」按钮
          Gs3dPillButton(
            key: const ValueKey('workbench.menu.command'),
            label: '命令',
            icon: Icons.terminal,
            touchTarget: true,
            onPressed: () => service.executeAction(
              'workbench.overlay.command_palette',
              {'open': true},
            ),
          ),
          const SizedBox(width: Gs3dTokens.space8),

          // 载入示例。
          Gs3dPillButton(
            key: WorkbenchUiKeys.quickLoadDemoButton,
            label: '载入示例',
            icon: Icons.folder_open_rounded,
            touchTarget: true,
            onPressed: () => _loadBuiltInSample(context),
          ),
        ],
      ),
    );
  }

  /// 紧凑手机工具条：只保留图标入口，把工程名交给中间的弹性区域。
  Widget _buildCompactTouchToolbar(BuildContext context) {
    return Container(
      key: WorkbenchUiKeys.topMenuBar,
      height: Gs3dTokens.topBarTouchHeight,
      padding: const EdgeInsets.symmetric(horizontal: Gs3dTokens.space4),
      decoration: const BoxDecoration(
        color: Gs3dTokens.surfaceHeader,
        border: Border(bottom: BorderSide(color: Gs3dTokens.hairline)),
      ),
      child: Row(
        children: [
          _buildCompactIconAction(
            key: WorkbenchUiKeys.homeButton,
            icon: Icons.home_outlined,
            tooltip: '返回欢迎页',
            onPressed: service.closeWorkbench,
          ),
          const SizedBox(width: Gs3dTokens.space4),
          ListenableBuilder(
            listenable: service,
            builder: (context, _) => _buildCompactIconAction(
              key: const ValueKey('workbench.menu.btn_render_settings'),
              icon: Icons.tune_rounded,
              tooltip: '渲染外观',
              onPressed: () => service.toggleRenderSettings(),
            ),
          ),
          const SizedBox(width: Gs3dTokens.space4),
          Expanded(
            child: ListenableBuilder(
              listenable: service,
              builder: (context, _) => Text(
                service.summary.name.isEmpty ? '未载入工程' : service.summary.name,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                textAlign: TextAlign.center,
                style: Gs3dType.micro.copyWith(
                  color: Gs3dTokens.textSecondary,
                  fontWeight: FontWeight.w600,
                ),
              ),
            ),
          ),
          const SizedBox(width: Gs3dTokens.space4),
          _buildCompactIconAction(
            key: const ValueKey('workbench.menu.command'),
            icon: Icons.terminal,
            tooltip: '命令面板',
            onPressed: () => service.executeAction(
              'workbench.overlay.command_palette',
              {'open': true},
            ),
          ),
          _buildCompactIconAction(
            key: WorkbenchUiKeys.quickLoadDemoButton,
            icon: Icons.folder_open_rounded,
            tooltip: '载入示例',
            onPressed: () => _loadBuiltInSample(context),
          ),
        ],
      ),
    );
  }

  Widget _buildCompactIconAction({
    required Key key,
    required IconData icon,
    required String tooltip,
    required VoidCallback onPressed,
    VoidCallback? onLongPress,
  }) {
    return Semantics(
      button: true,
      label: tooltip,
      child: Tooltip(
        message: tooltip,
        child: Gs3dPressable(
          key: key,
          onTap: onPressed,
          onLongPress: onLongPress,
          hoverScale: 0,
          minTouchSize: Gs3dTokens.hitTargetTouch,
          builder: (context, state) => AnimatedContainer(
            duration: Gs3dMotion.micro,
            curve: Gs3dMotion.standardCurve,
            width: Gs3dTokens.hitTargetTouch,
            height: Gs3dTokens.hitTargetTouch,
            alignment: Alignment.center,
            decoration: BoxDecoration(
              color: state == Gs3dInteractionState.idle
                  ? Colors.transparent
                  : Gs3dTokens.surfaceHover,
              borderRadius: BorderRadius.circular(Gs3dTokens.radiusMd),
            ),
            child: Icon(
              icon,
              size: Gs3dTokens.iconSizeMd,
              color: Gs3dTokens.textBody,
            ),
          ),
        ),
      ),
    );
  }
}
