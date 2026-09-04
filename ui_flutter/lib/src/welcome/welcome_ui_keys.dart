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

  // 8. 品牌徽标
  static const heroLogo = ValueKey('welcome.hero.logo');
}
