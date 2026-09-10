import 'package:flutter/material.dart';

import '../ffi/geoscatter3d_service.dart';
import '../theme/gs3d_tokens.dart';
import '../widgets/fluid.dart';
import 'new_project_dialog.dart';
import 'open_project_dialog.dart';
import 'welcome_ui_keys.dart';

/// 欢迎页主操作三联卡片 + 主功能按钮。
///
/// 交互遵循 Apple 流体设计：悬停即抬升（z1→z2 + 强调描边 + 箭头滑入），
/// 按压即时收缩；全部时长/阴影取自 [Gs3dMotion] / [Gs3dTokens]。
class WelcomeActionCards extends StatelessWidget {
  const WelcomeActionCards({super.key, required this.service});

  final GeoScatter3dService service;

  Future<void> _onNewProject(BuildContext context) {
    return showDialog(
      context: context,
      barrierDismissible: true,
      builder: (_) => NewProjectDialog(service: service),
    );
  }

  Future<void> _onOpenProject(BuildContext context) {
    return showDialog(
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
          backgroundColor: Gs3dTokens.danger,
        ),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const Text('新建或打开项目', style: Gs3dType.headline),
        const SizedBox(height: Gs3dTokens.space12 + 2),

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
              icon: Icons.play_arrow_rounded,
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
                    const SizedBox(width: Gs3dTokens.space12 + 2),
                    Expanded(child: card2),
                    const SizedBox(width: Gs3dTokens.space12 + 2),
                    Expanded(child: card3),
                  ],
                ),
              );
            }
            return Column(
              children: [
                card1,
                const SizedBox(height: Gs3dTokens.space12),
                card2,
                const SizedBox(height: Gs3dTokens.space12),
                card3,
              ],
            );
          },
        ),

        const SizedBox(height: Gs3dTokens.space16 + 2),

        // 主功能按钮（强调色实心，按压即时收缩）。
        FilledButton.icon(
          onPressed: () => _onOpenProject(context),
          icon: const Icon(Icons.file_open_outlined, size: 16),
          label: const Text('打开文件…'),
        ),
      ],
    );
  }
}

class _CleanActionCard extends StatefulWidget {
  const _CleanActionCard({
    super.key,
    required this.icon,
    required this.title,
    required this.subtitle,
    required this.onTap,
  });

  final IconData icon;
  final String title;
  final String subtitle;
  final VoidCallback onTap;

  @override
  State<_CleanActionCard> createState() => _CleanActionCardState();
}

class _CleanActionCardState extends State<_CleanActionCard> {
  @override
  Widget build(BuildContext context) {
    final a11y = context.gs3dA11y;
    return Gs3dPressable(
      onTap: widget.onTap,
      hoverScale: 0.004,
      builder: (context, state) {
        final hovered = state != Gs3dInteractionState.idle;
        return AnimatedContainer(
          duration: a11y.resolve(Gs3dMotion.micro),
          curve: Gs3dMotion.enterCurve,
          padding: const EdgeInsets.all(Gs3dTokens.space16 + 2),
          decoration: BoxDecoration(
            color: Gs3dTokens.surface,
            borderRadius: BorderRadius.circular(Gs3dTokens.radiusLg),
            border: Border.all(
              color: hovered ? Gs3dTokens.accentBorder : Gs3dTokens.border,
            ),
            boxShadow: hovered
                ? Gs3dTokens.elevation2
                : Gs3dTokens.elevation1,
          ),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            mainAxisAlignment: MainAxisAlignment.spaceBetween,
            children: [
              Row(
                children: [
                  // 图标底衬（强调色弱化底 + 圆角）。
                  Container(
                    width: 34,
                    height: 34,
                    decoration: BoxDecoration(
                      color: hovered
                          ? Gs3dTokens.accentSubtle
                          : Gs3dTokens.surfaceSunken,
                      borderRadius: BorderRadius.circular(Gs3dTokens.radiusMd),
                    ),
                    alignment: Alignment.center,
                    child: Icon(
                      widget.icon,
                      size: 19,
                      color: hovered
                          ? Gs3dTokens.accent
                          : Gs3dTokens.textPrimary,
                    ),
                  ),
                  const SizedBox(width: Gs3dTokens.space12),
                  Expanded(
                    child: Text(
                      widget.title,
                      style: Gs3dType.headline.copyWith(fontSize: 15),
                      overflow: TextOverflow.ellipsis,
                    ),
                  ),
                  // 悬停时箭头滑入（弹簧驱动）。
                  Gs3dSpringValue(
                    value: hovered ? 1.0 : 0.0,
                    spring: Gs3dMotion.ui,
                    builder: (context, t, _) => Opacity(
                      opacity: t.clamp(0.0, 1.0),
                      child: Transform.translate(
                        offset: Offset(-6 * (1 - t), 0),
                        child: const Icon(
                          Icons.arrow_forward_rounded,
                          size: 15,
                          color: Gs3dTokens.accent,
                        ),
                      ),
                    ),
                  ),
                ],
              ),
              const SizedBox(height: Gs3dTokens.space12),
              Text(
                widget.subtitle,
                style: Gs3dType.caption,
                maxLines: 2,
                overflow: TextOverflow.ellipsis,
              ),
            ],
          ),
        );
      },
    );
  }
}
