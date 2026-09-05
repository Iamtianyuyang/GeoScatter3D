import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import '../ffi/geoscatter3d_service.dart';
import '../theme/app_theme.dart';
import 'new_project_dialog.dart';
import 'open_project_dialog.dart';
import 'recent_projects_card.dart';
import 'welcome_action_cards.dart';
import 'welcome_footer.dart';
import 'welcome_hero.dart';
import 'welcome_ui_keys.dart';

class WelcomePage extends StatefulWidget {
  final GeoScatter3dService service;

  const WelcomePage({
    super.key,
    required this.service,
  });

  @override
  State<WelcomePage> createState() => _WelcomePageState();
}

class _WelcomePageState extends State<WelcomePage> {
  @override
  void initState() {
    super.initState();
    widget.service.onFileDropped = _handleFileDropped;
  }

  @override
  void dispose() {
    if (widget.service.onFileDropped == _handleFileDropped) {
      widget.service.onFileDropped = null;
    }
    super.dispose();
  }

  void _handleFileDropped(String path) {
    if (!mounted) return;
    final lower = path.toLowerCase();
    if (lower.endsWith('.gs3d') || lower.endsWith('.gs3d.bundle')) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text('已载入拖放工程: $path'),
          backgroundColor: const Color(0xFF10B981),
        ),
      );
    } else {
      showDialog(
        context: context,
        barrierDismissible: true,
        builder: (_) => NewProjectDialog(service: widget.service, initialPath: path),
      );
    }
  }

  void _openNewProjectDialog() {
    showDialog(
      context: context,
      barrierDismissible: true,
      builder: (_) => NewProjectDialog(service: widget.service),
    );
  }

  void _openProjectDialog() {
    showDialog(
      context: context,
      barrierDismissible: true,
      builder: (_) => OpenProjectDialog(service: widget.service),
    );
  }

  @override
  Widget build(BuildContext context) {
    return CallbackShortcuts(
      bindings: {
        const SingleActivator(LogicalKeyboardKey.keyN, control: true): _openNewProjectDialog,
        const SingleActivator(LogicalKeyboardKey.keyO, control: true): _openProjectDialog,
      },
      child: Focus(
        autofocus: true,
        child: Scaffold(
          key: WelcomeUiKeys.welcomeDragDropTarget,
          backgroundColor: AppTheme.background,
          body: Column(
            children: [
              // 顶部细微留白 / 窗口顶部空间
              Container(
                height: 28,
                color: AppTheme.surface,
                padding: const EdgeInsets.symmetric(horizontal: 16),
                alignment: Alignment.centerLeft,
                child: const Row(
                  children: [
                    Icon(Icons.scatter_plot_rounded, size: 14, color: AppTheme.primaryBlue),
                    SizedBox(width: 8),
                    Text(
                      'GeoScatter3D v2.0 - 现代化三维散点系统 (Flutter Desktop)',
                      style: TextStyle(
                        fontSize: 11,
                        color: AppTheme.textDim,
                        letterSpacing: 0.2,
                      ),
                    ),
                  ],
                ),
              ),
              const Divider(height: 1, color: AppTheme.border),

              // 主体滚动内容区（水平居中，自适应最大宽度）
              Expanded(
                child: SingleChildScrollView(
                  padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 24),
                  child: Center(
                    child: ConstrainedBox(
                      constraints: const BoxConstraints(maxWidth: 960),
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.stretch,
                        children: [
                          // 1. Hero 区域（动态粒子流背景 + 品牌徽标与标语）
                          const WelcomeHero(),

                          // 2. 核心操作三联卡片（新建工程、打开工程、快速体验）
                          WelcomeActionCards(service: widget.service),
                          const SizedBox(height: 24),

                          // 3. 最近打开项目卡片与列表
                          RecentProjectsCard(service: widget.service),
                          const SizedBox(height: 32),
                        ],
                      ),
                    ),
                  ),
                ),
              ),

              // 4. 底部状态与 GPU 选择栏
              WelcomeFooter(service: widget.service),
            ],
          ),
        ),
      ),
    );
  }
}
