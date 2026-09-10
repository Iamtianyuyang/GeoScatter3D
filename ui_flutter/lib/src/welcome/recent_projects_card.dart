import 'package:flutter/material.dart';

import '../ffi/geoscatter3d_service.dart';
import '../models/recent_project_model.dart';
import '../theme/gs3d_tokens.dart';
import '../widgets/fluid.dart';
import 'welcome_ui_keys.dart';

/// 最近打开项目列表（表头 + 表格行 + 空态）。
class RecentProjectsCard extends StatelessWidget {
  const RecentProjectsCard({super.key, required this.service});

  final GeoScatter3dService service;

  Future<void> _onOpenItem(BuildContext context, RecentProjectItem item) async {
    final res = await service.executeActionAsync('welcome.recent.open', {
      'path': item.path,
    });
    if (!context.mounted) return;
    if (res['success'] != true) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(
            res['message'] as String? ?? '无法打开项目: ${item.path}，文件可能已被移动或损坏',
          ),
          backgroundColor: Gs3dTokens.danger,
        ),
      );
    }
  }

  void _onClearConfirm(BuildContext context) {
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
              Icons.delete_sweep_rounded,
              color: Gs3dTokens.danger,
              size: 22,
            ),
            SizedBox(width: Gs3dTokens.space8),
            Text('清空最近记录', style: Gs3dType.title2),
          ],
        ),
        content: const Text(
          '确定要清空所有最近打开的项目记录吗？此操作不会删除磁盘上的任何实际数据文件。',
          style: Gs3dType.body,
        ),
        actions: [
          TextButton(
            key: WelcomeUiKeys.recentCancelClearButton,
            onPressed: () => Navigator.of(ctx).pop(),
            child: const Text('取消'),
          ),
          FilledButton(
            key: WelcomeUiKeys.recentConfirmClearButton,
            onPressed: () {
              Navigator.of(ctx).pop();
              service.executeAction('welcome.recent.clear');
            },
            style: FilledButton.styleFrom(
              backgroundColor: Gs3dTokens.danger,
              foregroundColor: Gs3dTokens.textOnAccent,
            ),
            child: const Text('确认清空'),
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
        final recentList = service.recentProjects;

        return Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                const Text('最近打开的项目', style: Gs3dType.headline),
                const Spacer(),
                if (recentList.isNotEmpty)
                  Gs3dPressable(
                    onTap: () => _onClearConfirm(context),
                    child: Padding(
                      key: WelcomeUiKeys.recentClearButton,
                      padding: const EdgeInsets.symmetric(
                        horizontal: Gs3dTokens.space8,
                        vertical: Gs3dTokens.space2,
                      ),
                      child: Text(
                        '清除',
                        style: Gs3dType.caption.copyWith(
                          color: Gs3dTokens.textTertiary,
                        ),
                      ),
                    ),
                  ),
              ],
            ),
            const SizedBox(height: Gs3dTokens.space12),

            // 表头列标题。
            const Padding(
              padding: EdgeInsets.symmetric(vertical: 6),
              child: Row(
                children: [
                  SizedBox(width: 220, child: Gs3dSectionLabel('项目名称')),
                  Expanded(child: Gs3dSectionLabel('路径')),
                  SizedBox(width: 140, child: Gs3dSectionLabel('最后打开时间')),
                  SizedBox(width: 48),
                ],
              ),
            ),
            const Gs3dDivider(height: 1),

            if (recentList.isEmpty)
              Container(
                width: double.infinity,
                padding: const EdgeInsets.symmetric(vertical: 36),
                alignment: Alignment.center,
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    const Icon(
                      Icons.folder_open_rounded,
                      size: 36,
                      color: Gs3dTokens.borderStrong,
                    ),
                    const SizedBox(height: Gs3dTokens.space8 + 2),
                    const Text(
                      '暂无最近打开的项目记录',
                      style: TextStyle(
                        fontSize: 13.5,
                        fontWeight: FontWeight.w600,
                        color: Gs3dTokens.textPrimary,
                      ),
                    ),
                    const SizedBox(height: Gs3dTokens.space4),
                    Text(
                      '可使用上方卡片新建工区或打开已有 .gs3d 数据包',
                      style: Gs3dType.caption,
                    ),
                    const SizedBox(height: Gs3dTokens.space12 + 2),
                    OutlinedButton.icon(
                      key: WelcomeUiKeys.recentEmptyDemoButton,
                      onPressed: () =>
                          service.executeActionAsync('welcome.quick_demo'),
                      icon: const Icon(Icons.play_arrow_rounded, size: 16),
                      label: const Text('载入内置示例工区体验'),
                      style: OutlinedButton.styleFrom(
                        foregroundColor: Gs3dTokens.accent,
                        side: const BorderSide(color: Gs3dTokens.accentBorder),
                        shape: RoundedRectangleBorder(
                          borderRadius: BorderRadius.circular(
                            Gs3dTokens.radiusMd,
                          ),
                        ),
                      ),
                    ),
                  ],
                ),
              )
            else
              ListView.separated(
                shrinkWrap: true,
                physics: const NeverScrollableScrollPhysics(),
                itemCount: recentList.length,
                separatorBuilder: (context, index) =>
                    const Gs3dDivider(height: 1),
                itemBuilder: (context, index) {
                  final item = recentList[index];
                  return _RecentProjectRow(
                    key: WelcomeUiKeys.recentItem(index),
                    index: index,
                    item: item,
                    onTap: () => _onOpenItem(context, item),
                    onRemove: () => service.removeRecentProject(item.path),
                  );
                },
              ),
          ],
        );
      },
    );
  }
}

class _RecentProjectRow extends StatelessWidget {
  const _RecentProjectRow({
    super.key,
    required this.index,
    required this.item,
    required this.onTap,
    required this.onRemove,
  });

  final int index;
  final RecentProjectItem item;
  final VoidCallback onTap;
  final VoidCallback onRemove;

  @override
  Widget build(BuildContext context) {
    return Gs3dPressable(
      onTap: onTap,
      builder: (context, state) {
        final hovered = state != Gs3dInteractionState.idle;
        return AnimatedContainer(
          duration: Gs3dMotion.micro,
          curve: Gs3dMotion.standardCurve,
          color: hovered ? Gs3dTokens.surfaceHover : Colors.transparent,
          padding: const EdgeInsets.symmetric(vertical: 9),
          child: Row(
            children: [
              SizedBox(
                width: 220,
                child: Row(
                  children: [
                    Container(
                      width: 30,
                      height: 30,
                      decoration: BoxDecoration(
                        color: Gs3dTokens.surfaceSunken,
                        borderRadius: BorderRadius.circular(Gs3dTokens.radiusSm),
                      ),
                      child: const CustomPaint(
                        painter: _WaveBadgeIconPainter(),
                      ),
                    ),
                    const SizedBox(width: Gs3dTokens.space8 + 2),
                    Expanded(
                      child: Text(
                        item.displayName,
                        style: Gs3dType.body.copyWith(
                          fontSize: 13,
                          fontWeight: FontWeight.w500,
                          color: Gs3dTokens.textPrimary,
                        ),
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                      ),
                    ),
                  ],
                ),
              ),
              Expanded(
                child: Padding(
                  padding: const EdgeInsets.only(right: Gs3dTokens.space16),
                  child: Text(
                    item.path,
                    style: Gs3dType.mono.copyWith(
                      fontWeight: FontWeight.w400,
                      color: Gs3dTokens.textBody,
                    ),
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                  ),
                ),
              ),
              SizedBox(
                width: 140,
                child: Text(item.relativeTimeStr, style: Gs3dType.caption),
              ),
              SizedBox(
                width: 48,
                child: Row(
                  mainAxisAlignment: MainAxisAlignment.end,
                  children: [
                    IconButton(
                      key: WelcomeUiKeys.recentItemRemove(index),
                      icon: Icon(
                        hovered
                            ? Icons.close_rounded
                            : Icons.more_horiz_rounded,
                        size: 15,
                        color: hovered
                            ? Gs3dTokens.danger
                            : Gs3dTokens.textTertiary,
                      ),
                      tooltip: '移除记录',
                      padding: EdgeInsets.zero,
                      constraints: const BoxConstraints(
                        minWidth: 26,
                        minHeight: 26,
                      ),
                      onPressed: onRemove,
                    ),
                  ],
                ),
              ),
            ],
          ),
        );
      },
    );
  }
}

class _WaveBadgeIconPainter extends CustomPainter {
  const _WaveBadgeIconPainter();

  @override
  void paint(Canvas canvas, Size size) {
    final paint = Paint()
      ..color = Gs3dPalette.slate400
      ..strokeWidth = 1.4
      ..style = PaintingStyle.stroke;

    final path = Path();
    path.moveTo(6, size.height - 8);
    path.quadraticBezierTo(
      size.width * 0.4,
      size.height * 0.35,
      size.width * 0.6,
      size.height * 0.55,
    );
    path.quadraticBezierTo(
      size.width * 0.8,
      size.height * 0.75,
      size.width - 6,
      size.height * 0.45,
    );
    canvas.drawPath(path, paint);
  }

  @override
  bool shouldRepaint(covariant CustomPainter oldDelegate) => false;
}
