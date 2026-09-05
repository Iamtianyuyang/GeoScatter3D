import 'package:flutter/foundation.dart';

/// 欢迎页所有可交互控件的稳定唯一语义 Key 定义
/// 供 AI 自动化调用、MCP 控件定位以及自动化 Widget 单元测试使用
class WelcomeUiKeys {
  // 1. 操作三联卡片
  static const newProjectAction = ValueKey('welcome.action.new_project');
  static const openProjectAction = ValueKey('welcome.action.open_project');
  static const quickDemoAction = ValueKey('welcome.action.quick_demo');

  // 2. 新建工程对话框
  static const newProjectDialogPathInput = ValueKey('welcome.dialog.new_project.path_input');
  static const newProjectDialogSampleChip = ValueKey('welcome.dialog.new_project.sample_chip');
  static const newProjectDialogNameInput = ValueKey('welcome.dialog.new_project.name_input');
  static const newProjectDialogThreadsSlider = ValueKey('welcome.dialog.new_project.threads_slider');
  static const newProjectDialogBrowseButton = ValueKey('welcome.dialog.new_project.browse_button');
  static const newProjectDialogSubmitButton = ValueKey('welcome.dialog.new_project.submit_button');
  static const newProjectDialogCancelButton = ValueKey('welcome.dialog.new_project.cancel_button');
  static const newProjectDialogCloseButton = ValueKey('welcome.dialog.new_project.close_button');
  static const newProjectDialogCancelBuildButton = ValueKey('welcome.dialog.new_project.cancel_build_button');
  static const newProjectDialogProgressIndicator = ValueKey('welcome.dialog.new_project.progress_indicator');
  static const newProjectDialogAdvancedToggle = ValueKey('welcome.dialog.new_project.advanced_toggle');
  static const newProjectDialogVoxelModeDropdown = ValueKey('welcome.dialog.new_project.voxel_mode_dropdown');
  static const newProjectDialogPointsPerTileSlider = ValueKey('welcome.dialog.new_project.points_per_tile_slider');

  // 3. 打开工程对话框
  static const openProjectDialogPathInput = ValueKey('welcome.dialog.open_project.path_input');
  static const openProjectDialogBrowseFileButton = ValueKey('welcome.dialog.open_project.browse_file_button');
  static const openProjectDialogBrowseFolderButton = ValueKey('welcome.dialog.open_project.browse_folder_button');
  static const openProjectDialogSampleChip = ValueKey('welcome.dialog.open_project.sample_chip');
  static const openProjectDialogSubmitButton = ValueKey('welcome.dialog.open_project.submit_button');
  static const openProjectDialogCancelButton = ValueKey('welcome.dialog.open_project.cancel_button');
  static const openProjectDialogCloseButton = ValueKey('welcome.dialog.open_project.close_button');

  // 4. 最近打开记录卡片
  static const recentClearButton = ValueKey('welcome.recent.clear_button');
  static const recentConfirmClearButton = ValueKey('welcome.recent.confirm_clear_button');
  static const recentCancelClearButton = ValueKey('welcome.recent.cancel_clear_button');
  static const recentEmptyDemoButton = ValueKey('welcome.recent.empty_demo_button');
  static ValueKey recentItem(int index) => ValueKey('welcome.recent.item_$index');
  static ValueKey recentItemRemove(int index) => ValueKey('welcome.recent.item_remove_$index');

  // 5. 底部栏按钮
  static const footerGpuButton = ValueKey('welcome.footer.gpu_button');
  static const footerDocsButton = ValueKey('welcome.footer.docs_button');
  static const footerAboutButton = ValueKey('welcome.footer.about_button');

  // 6. GPU 选择对话框
  static ValueKey gpuItem(int index) => ValueKey('welcome.dialog.gpu.item_$index');
  static const gpuDialogApplyButton = ValueKey('welcome.dialog.gpu.apply_button');
  static const gpuDialogCancelButton = ValueKey('welcome.dialog.gpu.cancel_button');
  static const gpuDialogCloseButton = ValueKey('welcome.dialog.gpu.close_button');

  // 7. 关于与文档对话框
  static const aboutDialogCloseButton = ValueKey('welcome.dialog.about.close_button');
  static const docsDialogCloseButton = ValueKey('welcome.dialog.docs.close_button');

  // 8. 品牌徽标与拖拽目标
  static const heroLogo = ValueKey('welcome.hero.logo');
  static const welcomeDragDropTarget = ValueKey('welcome.drag_drop_target');
}

/// 主工作台 (Workbench) 所有可交互控件的稳定唯一语义 Key 定义
class WorkbenchUiKeys {
  WorkbenchUiKeys._();

  // 顶部菜单栏
  static const topMenuBar = ValueKey('workbench.top_menu_bar');
  static const homeButton = ValueKey('workbench.menu.home_button');
  static const menuFile = ValueKey('workbench.menu.file');
  static const menuView = ValueKey('workbench.menu.view');
  static const menuWindow = ValueKey('workbench.menu.window');
  static const menuHelp = ValueKey('workbench.menu.help');
  static const layoutSwitcher = ValueKey('workbench.menu.layout_switcher');
  static const quickLoadDemoButton = ValueKey('workbench.menu.quick_load_demo');

  // 文件菜单项
  static const menuItemNewProject = ValueKey('workbench.menu_item.new_project');
  static const menuItemOpenProject = ValueKey('workbench.menu_item.open_project');
  static const menuItemQuickDemo = ValueKey('workbench.menu_item.quick_demo');
  static const menuItemScreenshot = ValueKey('workbench.menu_item.screenshot');
  static const menuItemWelcome = ValueKey('workbench.menu_item.welcome');

  // 视图菜单项
  static const menuItemResetCamera = ValueKey('workbench.menu_item.reset_camera');
  static const menuItemTopView = ValueKey('workbench.menu_item.top_view');
  static const menuItemFrontView = ValueKey('workbench.menu_item.front_view');
  static const menuItemSideView = ValueKey('workbench.menu_item.side_view');
  static const menuItemIsoView = ValueKey('workbench.menu_item.iso_view');
  static const menuItemToggleMapAxis = ValueKey('workbench.menu_item.toggle_map_axis');
  static const menuItemToggleWorldAxis = ValueKey('workbench.menu_item.toggle_world_axis');
  static const menuItemToggleCrosshair = ValueKey('workbench.menu_item.toggle_crosshair');

  // 窗口菜单项
  static const menuItemToggleLeftDock = ValueKey('workbench.menu_item.toggle_left_dock');
  static const menuItemToggleRightDock = ValueKey('workbench.menu_item.toggle_right_dock');
  static const menuItemLayoutStandard = ValueKey('workbench.menu_item.layout_standard');
  static const menuItemLayoutFloating = ValueKey('workbench.menu_item.layout_floating');
  static const menuItemLayoutAnalysis = ValueKey('workbench.menu_item.layout_analysis');

  // 帮助菜单项
  static const menuItemDocs = ValueKey('workbench.menu_item.docs');
  static const menuItemGpu = ValueKey('workbench.menu_item.gpu');
  static const menuItemAbout = ValueKey('workbench.menu_item.about');

  // 中央视口与浮动条
  static const viewportCanvas = ValueKey('workbench.viewport.canvas');
  static const viewportResetButton = ValueKey('workbench.viewport.reset_button');
  static const viewportPanToggle = ValueKey('workbench.viewport.pan_toggle');
  static const viewportMapAxisToggle = ValueKey('workbench.viewport.map_axis_toggle');
  static const viewportWorldAxisToggle = ValueKey('workbench.viewport.world_axis_toggle');
  static const viewportCrosshairToggle = ValueKey('workbench.viewport.crosshair_toggle');
  static const viewportZoomInButton = ValueKey('workbench.viewport.zoom_in_button');
  static const viewportZoomOutButton = ValueKey('workbench.viewport.zoom_out_button');
  static const viewportPresetViewDropdown = ValueKey('workbench.viewport.preset_view_dropdown');
  static const viewportScreenshotButton = ValueKey('workbench.viewport.screenshot_button');
  static const viewportHudCard = ValueKey('workbench.viewport.hud_card');
  static const viewportColorLegend = ValueKey('workbench.viewport.color_legend');
  static const viewportGizmo = ValueKey('workbench.viewport.gizmo');
  static const viewportEmptyGuideLoadButton = ValueKey('workbench.viewport.empty_guide_load_button');

  // 左侧面板
  static const leftDockPanel = ValueKey('workbench.left_dock.panel');
  static const leftDockTabProject = ValueKey('workbench.left_dock.tab_project');
  static const leftDockTabMeasure = ValueKey('workbench.left_dock.tab_measure');
  static const leftDockTabStats = ValueKey('workbench.left_dock.tab_stats');
  static const leftDockMeasureDistButton = ValueKey('workbench.left_dock.measure_dist_button');
  static const leftDockMeasureAreaButton = ValueKey('workbench.left_dock.measure_area_button');
  static const leftDockMeasureStrikeDipButton = ValueKey('workbench.left_dock.measure_strike_dip_button');
  static const leftDockStatsRecalculateButton = ValueKey('workbench.left_dock.stats_recalculate_button');

  // 右侧面板
  static const rightDockPanel = ValueKey('workbench.right_dock.panel');
  static const rightDockCloseButton = ValueKey('workbench.right_dock.close_button');
  static const rightDockPointSizeSlider = ValueKey('workbench.right_dock.point_size_slider');
  static const rightDockShapeDropdown = ValueKey('workbench.right_dock.shape_dropdown');
  static const rightDockHeightSourceDropdown = ValueKey('workbench.right_dock.height_source_dropdown');
  static const rightDockHeightScaleSlider = ValueKey('workbench.right_dock.height_scale_slider');
  static const rightDockColorAttrDropdown = ValueKey('workbench.right_dock.color_attr_dropdown');
  static const rightDockColormapDropdown = ValueKey('workbench.right_dock.colormap_dropdown');
  static const rightDockScalarMinSlider = ValueKey('workbench.right_dock.scalar_min_slider');
  static const rightDockScalarMaxSlider = ValueKey('workbench.right_dock.scalar_max_slider');
  static const rightDockResetScalarRangeButton = ValueKey('workbench.right_dock.reset_scalar_range_button');
  static const rightDockBgColorDropdown = ValueKey('workbench.right_dock.bg_color_dropdown');

  // 底部状态栏
  static const bottomStatusBar = ValueKey('workbench.bottom_status_bar');
}

