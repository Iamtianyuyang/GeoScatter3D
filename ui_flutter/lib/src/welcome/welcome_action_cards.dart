import 'package:flutter/material.dart';
import '../ffi/geoscatter3d_service.dart';
import '../theme/app_theme.dart';
import 'new_project_dialog.dart';
import 'open_project_dialog.dart';
import 'welcome_ui_keys.dart';

class WelcomeActionCards extends StatelessWidget {
  final GeoScatter3dService service;

  const WelcomeActionCards({
    super.key,
    required this.service,
  });

  void _onNewProject(BuildContext context) {
    showDialog(
      context: context,
      barrierDismissible: true,
      builder: (_) => NewProjectDialog(service: service),
    );
  }

  void _onOpenProject(BuildContext context) {
    showDialog(
      context: context,
      barrierDismissible: true,
      builder: (_) => OpenProjectDialog(service: service),
    );
  }

  void _onQuickDemo(BuildContext context) {
    final result = service.executeAction('welcome.quick_demo');
    if (result['success'] != true) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(result['message'] as String? ?? '未在当前环境找到 sample-points.gs3d.bundle 示例数据包'),
          backgroundColor: const Color(0xFFDC2626),
        ),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, constraints) {
        final isWide = constraints.maxWidth >= 760;

        final cards = [
          _ActionCardItem(
            key: WelcomeUiKeys.newProjectAction,
            icon: Icons.add_rounded,
            iconColor: AppTheme.primaryBlue,
            iconBg: AppTheme.primaryBlueBg,
            title: '新建工程',
            subtitle: '从原始 CSV / DAT 点云导入并构建 LOD 金字塔',
            tag: '向导模式',
            highlighted: false,
            onTap: () => _onNewProject(context),
          ),
          _ActionCardItem(
            key: WelcomeUiKeys.openProjectAction,
            icon: Icons.folder_open_rounded,
            iconColor: const Color(0xFF0284C7), // Sky-600
            iconBg: const Color(0xFFF0F9FF), // Sky-50
            title: '打开工程',
            subtitle: '载入已就绪的 .gs3d 文件或 .gs3d.bundle 空间包',
            tag: '快捷载入',
            highlighted: false,
            onTap: () => _onOpenProject(context),
          ),
          _ActionCardItem(
            key: WelcomeUiKeys.quickDemoAction,
            icon: Icons.rocket_launch_rounded,
            iconColor: const Color(0xFF7C3AED), // Purple-600
            iconBg: const Color(0xFFF5F3FF), // Purple-50
            title: '快速体验示例',
            subtitle: '一键载入内置 25 测点 5 级八叉树散点测试工区',
            tag: '即开即看',
            highlighted: true,
            onTap: () => _onQuickDemo(context),
          ),
        ];

        if (isWide) {
          return Row(
            children: [
              for (int i = 0; i < cards.length; i++) ...[
                if (i > 0) const SizedBox(width: 14),
                Expanded(child: cards[i]),
              ],
            ],
          );
        } else {
          return Column(
            children: [
              for (int i = 0; i < cards.length; i++) ...[
                if (i > 0) const SizedBox(height: 12),
                cards[i],
              ],
            ],
          );
        }
      },
    );
  }
}

class _ActionCardItem extends StatefulWidget {
  final IconData icon;
  final Color iconColor;
  final Color iconBg;
  final String title;
  final String subtitle;
  final String tag;
  final bool highlighted;
  final VoidCallback onTap;

  const _ActionCardItem({
    super.key,
    required this.icon,
    required this.iconColor,
    required this.iconBg,
    required this.title,
    required this.subtitle,
    required this.tag,
    required this.highlighted,
    required this.onTap,
  });

  @override
  State<_ActionCardItem> createState() => _ActionCardItemState();
}

class _ActionCardItemState extends State<_ActionCardItem> {
  bool _isHovered = false;

  @override
  Widget build(BuildContext context) {
    final borderColor = _isHovered
        ? AppTheme.primaryBlue
        : (widget.highlighted
            ? AppTheme.primaryBlue.withValues(alpha: 0.35)
            : AppTheme.border);

    return MouseRegion(
      onEnter: (_) => setState(() => _isHovered = true),
      onExit: (_) => setState(() => _isHovered = false),
      cursor: SystemMouseCursors.click,
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 140),
        decoration: BoxDecoration(
          color: _isHovered ? const Color(0xFFFAFCFF) : AppTheme.surface,
          borderRadius: BorderRadius.circular(10),
          border: Border.all(color: borderColor, width: _isHovered ? 1.5 : 1.0),
          boxShadow: _isHovered
              ? [
                  BoxShadow(
                    color: AppTheme.primaryBlue.withValues(alpha: 0.1),
                    blurRadius: 10,
                    offset: const Offset(0, 4),
                  ),
                ]
              : AppTheme.cardShadow,
        ),
        child: Material(
          color: Colors.transparent,
          child: InkWell(
            borderRadius: BorderRadius.circular(10),
            onTap: widget.onTap,
            child: Padding(
              padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 16),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                mainAxisSize: MainAxisSize.min,
                children: [
                  Row(
                    children: [
                      Container(
                        width: 38,
                        height: 38,
                        decoration: BoxDecoration(
                          color: widget.iconBg,
                          borderRadius: BorderRadius.circular(8),
                        ),
                        alignment: Alignment.center,
                        child: Icon(widget.icon, color: widget.iconColor, size: 22),
                      ),
                      const Spacer(),
                      Container(
                        padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
                        decoration: BoxDecoration(
                          color: widget.highlighted
                              ? AppTheme.primaryBlueBg
                              : AppTheme.surfaceMuted,
                          borderRadius: BorderRadius.circular(4),
                        ),
                        child: Text(
                          widget.tag,
                          style: TextStyle(
                            fontSize: 11,
                            fontWeight: FontWeight.w600,
                            color: widget.highlighted
                                ? AppTheme.primaryBlue
                                : AppTheme.textDim,
                          ),
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 14),
                  Row(
                    children: [
                      Text(
                        widget.title,
                        style: const TextStyle(
                          fontSize: 16,
                          fontWeight: FontWeight.w700,
                          color: AppTheme.textTitle,
                        ),
                      ),
                      const SizedBox(width: 4),
                      AnimatedSlide(
                        offset: _isHovered ? const Offset(0.2, 0) : Offset.zero,
                        duration: const Duration(milliseconds: 140),
                        child: const Icon(
                          Icons.arrow_forward_rounded,
                          size: 16,
                          color: AppTheme.primaryBlue,
                        ),
                      ),
                    ],
                  ),
                  const SizedBox(height: 6),
                  Text(
                    widget.subtitle,
                    style: const TextStyle(
                      fontSize: 12.5,
                      color: AppTheme.textDim,
                      height: 1.4,
                    ),
                    maxLines: 2,
                    overflow: TextOverflow.ellipsis,
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }
}
