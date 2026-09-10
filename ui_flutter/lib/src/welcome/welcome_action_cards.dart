import 'package:flutter/material.dart';
import '../ffi/geoscatter3d_service.dart';
import '../theme/macos_tokens.dart';
import 'new_project_dialog.dart';
import 'open_project_dialog.dart';
import 'welcome_ui_keys.dart';

class WelcomeActionCards extends StatelessWidget {
  final GeoScatter3dService service;

  const WelcomeActionCards({super.key, required this.service});

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

  Future<void> _onQuickDemo(BuildContext context) async {
    final result = await service.executeActionAsync('welcome.quick_demo');
    if (!context.mounted) return;
    if (result['success'] != true) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(
            result['message'] as String? ??
                '未在当前环境找到 sample-points.gs3d.bundle 示例数据包',
          ),
          backgroundColor: const Color(0xFFDC2626),
        ),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const Text(
          '新建或打开项目',
          style: TextStyle(
            fontSize: 14.5,
            fontWeight: FontWeight.w700,
            color: Color(0xFF111827),
          ),
        ),
        const SizedBox(height: 14),

        // 三联极简现代化卡片 (纯黑白灰高级质感，无花哨色彩与过多描述)
        LayoutBuilder(
          builder: (context, constraints) {
            final isWide = constraints.maxWidth >= 720;

            final card1 = _CleanActionCard(
              key: WelcomeUiKeys.newProjectAction,
              icon: Icons.add,
              title: '新建工程',
              subtitle: '从 CSV / DAT 点云导入并构建 LOD 金字塔',
              onTap: () => _onNewProject(context),
            );

            final card2 = _CleanActionCard(
              key: WelcomeUiKeys.openProjectAction,
              icon: Icons.folder_open_rounded,
              title: '打开工程',
              subtitle: '载入已有的 .gs3d 文件或 .gs3d.bundle 空间包',
              onTap: () => _onOpenProject(context),
            );

            final card3 = _CleanActionCard(
              key: WelcomeUiKeys.quickDemoAction,
              icon: Icons.play_arrow_outlined,
              title: '快速体验示例',
              subtitle: '构建并载入内置散点示例，直接进入三维视口',
              onTap: () => _onQuickDemo(context),
            );

            if (isWide) {
              return IntrinsicHeight(
                child: Row(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    Expanded(child: card1),
                    const SizedBox(width: 14),
                    Expanded(child: card2),
                    const SizedBox(width: 14),
                    Expanded(child: card3),
                  ],
                ),
              );
            } else {
              return Column(
                children: [
                  card1,
                  const SizedBox(height: 12),
                  card2,
                  const SizedBox(height: 12),
                  card3,
                ],
              );
            }
          },
        ),

        const SizedBox(height: 18),

        // 经典 Apple 蓝「打开文件…」主功能按钮
        ElevatedButton(
          onPressed: () => _onOpenProject(context),
          style: ElevatedButton.styleFrom(
            backgroundColor: MacOsTokens.systemBlue,
            foregroundColor: Colors.white,
            elevation: 0,
            padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 10),
            shape: RoundedRectangleBorder(
              borderRadius: BorderRadius.circular(6),
            ),
          ),
          child: const Text(
            '打开文件…',
            style: TextStyle(
              fontSize: 13,
              fontWeight: FontWeight.w600,
              letterSpacing: 0.1,
            ),
          ),
        ),
      ],
    );
  }
}

class _CleanActionCard extends StatefulWidget {
  final IconData icon;
  final String title;
  final String subtitle;
  final VoidCallback onTap;

  const _CleanActionCard({
    super.key,
    required this.icon,
    required this.title,
    required this.subtitle,
    required this.onTap,
  });

  @override
  State<_CleanActionCard> createState() => _CleanActionCardState();
}

class _CleanActionCardState extends State<_CleanActionCard> {
  bool _isHovered = false;

  @override
  Widget build(BuildContext context) {
    return MouseRegion(
      onEnter: (_) => setState(() => _isHovered = true),
      onExit: (_) => setState(() => _isHovered = false),
      cursor: SystemMouseCursors.click,
      child: GestureDetector(
        onTap: widget.onTap,
        child: AnimatedContainer(
          duration: const Duration(milliseconds: 140),
          curve: Curves.easeOutCubic,
          padding: const EdgeInsets.all(18),
          decoration: BoxDecoration(
            color: Colors.white,
            borderRadius: BorderRadius.circular(10),
            border: Border.all(
              color: _isHovered ? const Color(0xFFCBD5E1) : const Color(0xFFE5E7EB),
              width: 1,
            ),
            boxShadow: _isHovered
                ? const [
                    BoxShadow(
                      color: Color(0x0C000000),
                      blurRadius: 10,
                      offset: Offset(0, 3),
                    ),
                  ]
                : const [
                    BoxShadow(
                      color: Color(0x04000000),
                      blurRadius: 4,
                      offset: Offset(0, 1),
                    ),
                  ],
          ),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Row(
                children: [
                  Icon(
                    widget.icon,
                    size: 20,
                    color: const Color(0xFF111827),
                  ),
                  const SizedBox(width: 10),
                  Text(
                    widget.title,
                    style: const TextStyle(
                      fontSize: 15,
                      fontWeight: FontWeight.w700,
                      color: Color(0xFF111827),
                      letterSpacing: -0.2,
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 12),
              Text(
                widget.subtitle,
                style: const TextStyle(
                  fontSize: 12,
                  color: Color(0xFF6B7280),
                  height: 1.45,
                ),
                maxLines: 2,
                overflow: TextOverflow.ellipsis,
              ),
            ],
          ),
        ),
      ),
    );
  }
}
