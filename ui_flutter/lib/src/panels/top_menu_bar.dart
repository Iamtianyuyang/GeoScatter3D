import 'package:flutter/material.dart';
import '../ffi/geoscatter3d_service.dart';
import '../theme/app_theme.dart';
import '../welcome/gpu_selection_dialog.dart';
import '../welcome/new_project_dialog.dart';
import '../welcome/open_project_dialog.dart';
import '../welcome/welcome_ui_keys.dart';
import 'export_dialog.dart';

class TopMenuBar extends StatelessWidget {
  final GeoScatter3dService service;

  const TopMenuBar({
    super.key,
    required this.service,
  });

  void _showExportDialog(BuildContext context) {
    showDialog(
      context: context,
      barrierDismissible: true,
      builder: (_) => ExportPointCloudDialog(service: service),
    );
  }

  void _showNewProjectDialog(BuildContext context) {
    showDialog(
      context: context,
      barrierDismissible: true,
      builder: (_) => NewProjectDialog(service: service),
    );
  }

  void _showOpenProjectDialog(BuildContext context) {
    showDialog(
      context: context,
      barrierDismissible: true,
      builder: (_) => OpenProjectDialog(service: service),
    );
  }

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
                '• 平移视口：开启平移模式后左键拖拽\n'
                '• 缩放视野：滚动鼠标滚轮 或 点击放大/缩小按钮\n'
                '• 复位视角：快捷工具栏「复位视角 R」',
                style: TextStyle(fontSize: 12.5, color: AppTheme.textBody, height: 1.5),
              ),
            ],
          ),
        ),
        actions: [
          ElevatedButton(
            key: WorkbenchUiKeys.menuItemDocs,
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
              '• 全异步多线程解耦与 AI JSON-RPC 控制面',
              style: TextStyle(fontSize: 13, color: AppTheme.textBody, height: 1.6),
            ),
            const SizedBox(height: 12),
            Text('引擎内核版本: ${service.engineVersion}', style: const TextStyle(fontSize: 12, color: AppTheme.textDim)),
          ],
        ),
        actions: [
          ElevatedButton(
            key: WorkbenchUiKeys.menuItemAbout,
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
    return Container(
      key: WorkbenchUiKeys.topMenuBar,
      height: 38,
      padding: const EdgeInsets.symmetric(horizontal: 14),
      decoration: const BoxDecoration(
        color: AppTheme.surface,
        border: Border(
          bottom: BorderSide(color: AppTheme.border, width: 1),
        ),
      ),
      child: Row(
        children: [
          // Logo & 返回欢迎页
          InkWell(
            key: WorkbenchUiKeys.homeButton,
            borderRadius: BorderRadius.circular(6),
            onTap: () => service.closeWorkbench(),
            child: Padding(
              padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 4),
              child: Row(
                children: [
                  const Icon(Icons.home_outlined, size: 17, color: AppTheme.primaryBlue),
                  const SizedBox(width: 6),
                  RichText(
                    text: const TextSpan(
                      children: [
                        TextSpan(
                          text: 'GeoScatter ',
                          style: TextStyle(
                            fontSize: 14,
                            fontWeight: FontWeight.w800,
                            color: AppTheme.textTitle,
                          ),
                        ),
                        TextSpan(
                          text: '3D',
                          style: TextStyle(
                            fontSize: 14,
                            fontWeight: FontWeight.w800,
                            color: AppTheme.primaryBlue,
                          ),
                        ),
                      ],
                    ),
                  ),
                ],
              ),
            ),
          ),
          const SizedBox(width: 14),

          // 菜单项
          _buildFileMenu(context),
          _buildViewMenu(context),
          _buildToolsMenu(context),
          _buildWindowMenu(context),
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
                  color: AppTheme.textDim,
                ),
              );
            },
          ),

          const Spacer(),

          // 返回欢迎页按钮
          TextButton.icon(
            key: const ValueKey('workbench.menu.btn_welcome'),
            style: TextButton.styleFrom(
              foregroundColor: AppTheme.textDim,
              padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
            ),
            icon: const Icon(Icons.arrow_back_rounded, size: 15),
            label: const Text('欢迎页', style: TextStyle(fontSize: 12.5)),
            onPressed: () => service.closeWorkbench(),
          ),
          const SizedBox(width: 6),

          // 布局切换选择器
          PopupMenuButton<WorkbenchLayoutMode>(
            key: WorkbenchUiKeys.layoutSwitcher,
            tooltip: '切换工作区布局模式',
            initialValue: service.layoutMode,
            onSelected: (mode) => service.setLayoutMode(mode),
            itemBuilder: (context) => [
              const PopupMenuItem(
                key: WorkbenchUiKeys.menuItemLayoutStandard,
                value: WorkbenchLayoutMode.standard,
                child: Row(
                  children: [
                    Icon(Icons.dashboard_customize_outlined, size: 16, color: AppTheme.primaryBlue),
                    SizedBox(width: 8),
                    Text('标准工作台 (Workbench)'),
                  ],
                ),
              ),
              const PopupMenuItem(
                key: WorkbenchUiKeys.menuItemLayoutFloating,
                value: WorkbenchLayoutMode.floatingDock,
                child: Row(
                  children: [
                    Icon(Icons.layers_outlined, size: 16, color: AppTheme.accentBlue),
                    SizedBox(width: 8),
                    Text('悬浮胶囊 Dock (Floating Dock)'),
                  ],
                ),
              ),
              const PopupMenuItem(
                key: WorkbenchUiKeys.menuItemLayoutAnalysis,
                value: WorkbenchLayoutMode.analysisRail,
                child: Row(
                  children: [
                    Icon(Icons.view_sidebar_outlined, size: 16, color: AppTheme.statusGreen),
                    SizedBox(width: 8),
                    Text('暗色分析舱 (Analysis Rail)'),
                  ],
                ),
              ),
            ],
            child: Container(
              padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
              decoration: BoxDecoration(
                border: Border.all(color: AppTheme.border),
                borderRadius: BorderRadius.circular(6),
                color: Colors.white,
              ),
              child: Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  const Icon(Icons.view_quilt_outlined, size: 14, color: AppTheme.primaryBlue),
                  const SizedBox(width: 5),
                  Text(
                    service.layoutMode.displayName,
                    style: const TextStyle(fontSize: 12, fontWeight: FontWeight.w600, color: AppTheme.textTitle),
                  ),
                  const SizedBox(width: 4),
                  const Icon(Icons.arrow_drop_down_rounded, size: 16, color: AppTheme.textDim),
                ],
              ),
            ),
          ),
          const SizedBox(width: 10),

          // 快速打开数据按钮
          TextButton.icon(
            key: WorkbenchUiKeys.quickLoadDemoButton,
            style: TextButton.styleFrom(
              foregroundColor: AppTheme.primaryBlue,
              padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
            ),
            icon: const Icon(Icons.folder_open_rounded, size: 16),
            label: const Text('载入示例', style: TextStyle(fontSize: 12.5, fontWeight: FontWeight.w600)),
            onPressed: () {
              service.loadDataset('data/sample-points.gs3d.bundle');
            },
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
            service.loadDataset('data/sample-points.gs3d.bundle');
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
              Icon(Icons.add_box_outlined, size: 16, color: AppTheme.primaryBlue),
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
              Icon(Icons.folder_open_rounded, size: 16, color: AppTheme.primaryBlue),
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
              Icon(Icons.scatter_plot_rounded, size: 16, color: AppTheme.statusGreen),
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
              Icon(Icons.camera_alt_outlined, size: 16, color: AppTheme.accentBlue),
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
              Icon(Icons.file_download_outlined, size: 16, color: AppTheme.accentBlue),
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
              Icon(Icons.home_outlined, size: 16, color: AppTheme.textDim),
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
          case 'map_axis':
            service.setViewportOverlays(mapAxis: !service.showMapAxis);
            break;
          case 'world_axis':
            service.setViewportOverlays(worldAxis: !service.showWorldAxis);
            break;
          case 'crosshair':
            service.setViewportOverlays(crosshair: !service.showCrosshair);
            break;
        }
      },
      itemBuilder: (context) => [
        const PopupMenuItem(
          key: WorkbenchUiKeys.menuItemResetCamera,
          value: 'reset',
          child: Row(
            children: [
              Icon(Icons.refresh_rounded, size: 16, color: AppTheme.primaryBlue),
              SizedBox(width: 8),
              Text('复位视角 (Home / R)'),
            ],
          ),
        ),
        const PopupMenuDivider(),
        const PopupMenuItem(
          key: WorkbenchUiKeys.menuItemTopView,
          value: 'top',
          child: Row(
            children: [
              Icon(Icons.border_top_rounded, size: 16, color: AppTheme.textBody),
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
              Icon(Icons.stay_current_portrait_rounded, size: 16, color: AppTheme.textBody),
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
              Icon(Icons.stay_current_landscape_rounded, size: 16, color: AppTheme.textBody),
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
              Icon(Icons.view_in_ar_rounded, size: 16, color: AppTheme.textBody),
              SizedBox(width: 8),
              Text('等轴测 (Isometric)'),
            ],
          ),
        ),
        const PopupMenuDivider(),
        PopupMenuItem(
          key: WorkbenchUiKeys.menuItemToggleMapAxis,
          value: 'map_axis',
          child: Row(
            children: [
              Icon(
                service.showMapAxis ? Icons.check_box_rounded : Icons.check_box_outline_blank_rounded,
                size: 16,
                color: service.showMapAxis ? AppTheme.primaryBlue : AppTheme.textDim,
              ),
              const SizedBox(width: 8),
              const Text('显示地图标尺轴'),
            ],
          ),
        ),
        PopupMenuItem(
          key: WorkbenchUiKeys.menuItemToggleWorldAxis,
          value: 'world_axis',
          child: Row(
            children: [
              Icon(
                service.showWorldAxis ? Icons.check_box_rounded : Icons.check_box_outline_blank_rounded,
                size: 16,
                color: service.showWorldAxis ? AppTheme.primaryBlue : AppTheme.textDim,
              ),
              const SizedBox(width: 8),
              const Text('显示世界参考轴'),
            ],
          ),
        ),
        PopupMenuItem(
          key: WorkbenchUiKeys.menuItemToggleCrosshair,
          value: 'crosshair',
          child: Row(
            children: [
              Icon(
                service.showCrosshair ? Icons.check_box_rounded : Icons.check_box_outline_blank_rounded,
                size: 16,
                color: service.showCrosshair ? AppTheme.primaryBlue : AppTheme.textDim,
              ),
              const SizedBox(width: 8),
              const Text('显示中心十字准星'),
            ],
          ),
        ),
      ],
      child: _buildMenuLabel('视图'),
    );
  }

  Widget _buildToolsMenu(BuildContext context) {
    return PopupMenuButton<String>(
      key: const ValueKey('workbench.menu.tools'),
      tooltip: '工具',
      offset: const Offset(0, 30),
      onSelected: (value) {
        switch (value) {
          case 'cmd_palette':
            service.toggleCommandPalette(true);
            break;
          case 'performance':
            service.togglePerformancePanel(true);
            break;
          case 'tile_inspector':
            service.toggleTileInspector(true);
            break;
        }
      },
      itemBuilder: (context) => [
        const PopupMenuItem(
          key: ValueKey('workbench.menu.item_cmd_palette'),
          value: 'cmd_palette',
          child: Row(
            children: [
              Icon(Icons.terminal, size: 16, color: AppTheme.primaryBlue),
              SizedBox(width: 8),
              Text('命令面板 (Ctrl+P)...'),
            ],
          ),
        ),
        const PopupMenuItem(
          key: ValueKey('workbench.menu.item_performance'),
          value: 'performance',
          child: Row(
            children: [
              Icon(Icons.speed_rounded, size: 16, color: AppTheme.statusGreen),
              SizedBox(width: 8),
              Text('性能与显存诊断...'),
            ],
          ),
        ),
        const PopupMenuItem(
          key: ValueKey('workbench.menu.item_tile_inspector'),
          value: 'tile_inspector',
          child: Row(
            children: [
              Icon(Icons.grid_view_rounded, size: 16, color: AppTheme.accentBlue),
              SizedBox(width: 8),
              Text('八叉树瓦片与 LOD 视察器...'),
            ],
          ),
        ),
      ],
      child: _buildMenuLabel('工具'),
    );
  }

  Widget _buildWindowMenu(BuildContext context) {
    return PopupMenuButton<String>(
      key: WorkbenchUiKeys.menuWindow,
      tooltip: '窗口',
      offset: const Offset(0, 30),
      onSelected: (value) {
        switch (value) {
          case 'layout_std':
            service.setLayoutMode(WorkbenchLayoutMode.standard);
            break;
          case 'layout_float':
            service.setLayoutMode(WorkbenchLayoutMode.floatingDock);
            break;
          case 'layout_rail':
            service.setLayoutMode(WorkbenchLayoutMode.analysisRail);
            break;
          case 'toggle_left':
            service.toggleLeftDock();
            break;
          case 'toggle_right':
            service.toggleRightDock();
            break;
        }
      },
      itemBuilder: (context) => [
        const PopupMenuItem(
          key: WorkbenchUiKeys.menuItemLayoutStandard,
          value: 'layout_std',
          child: Row(
            children: [
              Icon(Icons.dashboard_customize_outlined, size: 16, color: AppTheme.primaryBlue),
              SizedBox(width: 8),
              Text('标准工作台布局'),
            ],
          ),
        ),
        const PopupMenuItem(
          key: WorkbenchUiKeys.menuItemLayoutFloating,
          value: 'layout_float',
          child: Row(
            children: [
              Icon(Icons.layers_outlined, size: 16, color: AppTheme.accentBlue),
              SizedBox(width: 8),
              Text('悬浮胶囊 Dock 布局'),
            ],
          ),
        ),
        const PopupMenuItem(
          key: WorkbenchUiKeys.menuItemLayoutAnalysis,
          value: 'layout_rail',
          child: Row(
            children: [
              Icon(Icons.view_sidebar_outlined, size: 16, color: AppTheme.statusGreen),
              SizedBox(width: 8),
              Text('暗色分析舱布局'),
            ],
          ),
        ),
        const PopupMenuDivider(),
        PopupMenuItem(
          key: WorkbenchUiKeys.menuItemToggleLeftDock,
          value: 'toggle_left',
          child: Row(
            children: [
              Icon(
                service.leftDockVisible ? Icons.check_box_rounded : Icons.check_box_outline_blank_rounded,
                size: 16,
                color: service.leftDockVisible ? AppTheme.primaryBlue : AppTheme.textDim,
              ),
              const SizedBox(width: 8),
              const Text('左侧抽屉面板'),
            ],
          ),
        ),
        PopupMenuItem(
          key: WorkbenchUiKeys.menuItemToggleRightDock,
          value: 'toggle_right',
          child: Row(
            children: [
              Icon(
                service.rightDockVisible ? Icons.check_box_rounded : Icons.check_box_outline_blank_rounded,
                size: 16,
                color: service.rightDockVisible ? AppTheme.primaryBlue : AppTheme.textDim,
              ),
              const SizedBox(width: 8),
              const Text('右侧属性面板'),
            ],
          ),
        ),
      ],
      child: _buildMenuLabel('窗口'),
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
              Icon(Icons.keyboard_outlined, size: 16, color: AppTheme.primaryBlue),
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
              Icon(Icons.memory_rounded, size: 16, color: AppTheme.primaryBlue),
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
              Icon(Icons.menu_book_rounded, size: 16, color: AppTheme.accentBlue),
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
              Icon(Icons.info_outline_rounded, size: 16, color: AppTheme.textDim),
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
        decoration: BoxDecoration(
          borderRadius: BorderRadius.circular(4),
        ),
        child: Text(
          title,
          style: const TextStyle(
            fontSize: 13,
            fontWeight: FontWeight.w500,
            color: AppTheme.textTitle,
          ),
        ),
      ),
    );
  }
}

