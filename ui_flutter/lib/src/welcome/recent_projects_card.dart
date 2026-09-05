import 'dart:io';
import 'package:flutter/material.dart';
import '../ffi/geoscatter3d_service.dart';
import '../models/recent_project_model.dart';
import '../theme/app_theme.dart';
import 'welcome_ui_keys.dart';

class RecentProjectsCard extends StatelessWidget {
  final GeoScatter3dService service;

  const RecentProjectsCard({
    super.key,
    required this.service,
  });

  void _onOpenItem(BuildContext context, RecentProjectItem item) {
    final res = service.executeAction('welcome.recent.open', {'path': item.path});
    if (res['success'] != true) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(res['message'] as String? ?? '无法打开项目: ${item.path}，文件可能已被移动或损坏'),
          backgroundColor: const Color(0xFFDC2626),
        ),
      );
    }
  }

  void _onClearConfirm(BuildContext context) {
    showDialog(
      context: context,
      builder: (ctx) => AlertDialog(
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
        backgroundColor: AppTheme.surface,
        title: const Row(
          children: [
            Icon(Icons.delete_sweep_rounded, color: Color(0xFFDC2626), size: 22),
            SizedBox(width: 8),
            Text('清空最近记录', style: TextStyle(fontSize: 16, fontWeight: FontWeight.w700)),
          ],
        ),
        content: const Text(
          '确定要清空所有最近打开的项目记录吗？此操作不会删除磁盘上的任何实际数据文件。',
          style: TextStyle(fontSize: 13, color: AppTheme.textDim),
        ),
        actions: [
          TextButton(
            key: WelcomeUiKeys.recentCancelClearButton,
            onPressed: () => Navigator.of(ctx).pop(),
            child: const Text('取消', style: TextStyle(color: AppTheme.textDim)),
          ),
          ElevatedButton(
            key: WelcomeUiKeys.recentConfirmClearButton,
            onPressed: () {
              Navigator.of(ctx).pop();
              service.executeAction('welcome.recent.clear');
            },
            style: ElevatedButton.styleFrom(
              backgroundColor: const Color(0xFFDC2626),
              foregroundColor: Colors.white,
              elevation: 0,
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

        return Container(
          width: double.infinity,
          decoration: BoxDecoration(
            color: AppTheme.surface,
            borderRadius: BorderRadius.circular(10),
            border: Border.all(color: AppTheme.border),
            boxShadow: AppTheme.cardShadow,
          ),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              // 头部栏
              Padding(
                padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 14),
                child: Row(
                  children: [
                    Container(
                      width: 4,
                      height: 18,
                      decoration: BoxDecoration(
                        color: AppTheme.primaryBlue,
                        borderRadius: BorderRadius.circular(2),
                      ),
                    ),
                    const SizedBox(width: 10),
                    const Text(
                      '最近打开的项目',
                      style: TextStyle(
                        fontSize: 15,
                        fontWeight: FontWeight.w700,
                        color: AppTheme.textTitle,
                      ),
                    ),
                    const SizedBox(width: 8),
                    Container(
                      padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 1.5),
                      decoration: BoxDecoration(
                        color: AppTheme.surfaceMuted,
                        borderRadius: BorderRadius.circular(10),
                      ),
                      child: Text(
                        '${recentList.length}',
                        style: const TextStyle(
                          fontSize: 11,
                          fontWeight: FontWeight.w600,
                          color: AppTheme.textDim,
                        ),
                      ),
                    ),
                    const Spacer(),
                    if (recentList.isNotEmpty)
                      TextButton.icon(
                        key: WelcomeUiKeys.recentClearButton,
                        onPressed: () => _onClearConfirm(context),
                        icon: const Icon(Icons.delete_outline_rounded, size: 15),
                        label: const Text('清空记录', style: TextStyle(fontSize: 12)),
                        style: TextButton.styleFrom(
                          foregroundColor: AppTheme.textDim,
                          padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
                          minimumSize: Size.zero,
                          tapTargetSize: MaterialTapTargetSize.shrinkWrap,
                        ),
                      ),
                  ],
                ),
              ),

              const Divider(height: 1, color: AppTheme.border),

              // 列表主体
              if (recentList.isEmpty)
                Container(
                  width: double.infinity,
                  padding: const EdgeInsets.symmetric(vertical: 40),
                  alignment: Alignment.center,
                  child: Column(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Container(
                        padding: const EdgeInsets.all(12),
                        decoration: const BoxDecoration(
                          color: AppTheme.surfaceMuted,
                          shape: BoxShape.circle,
                        ),
                        child: const Icon(
                          Icons.folder_open_rounded,
                          size: 32,
                          color: AppTheme.textDim,
                        ),
                      ),
                      const SizedBox(height: 12),
                      const Text(
                        '暂无最近打开的项目记录',
                        style: TextStyle(
                          fontSize: 14,
                          fontWeight: FontWeight.w600,
                          color: AppTheme.textBody,
                        ),
                      ),
                      const SizedBox(height: 4),
                      const Text(
                        '可通过上方卡片新建工区或打开已有 .gs3d 数据包',
                        style: TextStyle(fontSize: 12, color: AppTheme.textDim),
                      ),
                      const SizedBox(height: 14),
                      OutlinedButton.icon(
                        key: WelcomeUiKeys.recentEmptyDemoButton,
                        onPressed: () => service.executeAction('welcome.quick_demo'),
                        icon: const Icon(Icons.play_circle_outline_rounded, size: 16),
                        label: const Text('一键载入内置示例工区体验', style: TextStyle(fontSize: 12)),
                        style: OutlinedButton.styleFrom(
                          foregroundColor: AppTheme.primaryBlue,
                          side: const BorderSide(color: AppTheme.primaryBlue),
                          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(6)),
                          padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 8),
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
                  separatorBuilder: (context, index) => const Divider(height: 1, color: AppTheme.border),
                  itemBuilder: (context, index) {
                    final item = recentList[index];
                    return _RecentProjectTile(
                      key: WelcomeUiKeys.recentItem(index),
                      index: index,
                      item: item,
                      onTap: () => _onOpenItem(context, item),
                      onRemove: () => service.removeRecentProject(item.path),
                    );
                  },
                ),
            ],
          ),
        );
      },
    );
  }
}

class _RecentProjectTile extends StatefulWidget {
  final int index;
  final RecentProjectItem item;
  final VoidCallback onTap;
  final VoidCallback onRemove;

  const _RecentProjectTile({
    super.key,
    required this.index,
    required this.item,
    required this.onTap,
    required this.onRemove,
  });

  @override
  State<_RecentProjectTile> createState() => _RecentProjectTileState();
}

class _RecentProjectTileState extends State<_RecentProjectTile> {
  bool _isHovered = false;

  bool get _exists {
    try {
      return File(widget.item.path).existsSync() || Directory(widget.item.path).existsSync();
    } catch (_) {
      return false;
    }
  }

  @override
  Widget build(BuildContext context) {
    final fileExists = _exists;

    return MouseRegion(
      onEnter: (_) => setState(() => _isHovered = true),
      onExit: (_) => setState(() => _isHovered = false),
      cursor: SystemMouseCursors.click,
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 120),
        color: _isHovered ? const Color(0xFFF1F5F9) : Colors.transparent,
        child: InkWell(
          onTap: widget.onTap,
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 12),
            child: Row(
              children: [
                // 文件类型标识
                Container(
                  width: 36,
                  height: 36,
                  decoration: BoxDecoration(
                    color: !fileExists
                        ? const Color(0xFFFEF2F2)
                        : (_isHovered ? AppTheme.primaryBlueBg : AppTheme.surfaceMuted),
                    borderRadius: BorderRadius.circular(6),
                  ),
                  child: Icon(
                    !fileExists ? Icons.warning_amber_rounded : Icons.dataset_outlined,
                    size: 20,
                    color: !fileExists
                        ? const Color(0xFFDC2626)
                        : (_isHovered ? AppTheme.primaryBlue : AppTheme.textDim),
                  ),
                ),
                const SizedBox(width: 14),

                // 项目名与绝对路径
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Row(
                        children: [
                          Flexible(
                            child: Text(
                              widget.item.displayName,
                              style: TextStyle(
                                fontSize: 13.5,
                                fontWeight: FontWeight.w600,
                                color: _isHovered ? AppTheme.primaryBlue : AppTheme.textTitle,
                              ),
                              maxLines: 1,
                              overflow: TextOverflow.ellipsis,
                            ),
                          ),
                          if (!fileExists) ...[
                            const SizedBox(width: 8),
                            Container(
                              padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 1),
                              decoration: BoxDecoration(
                                color: const Color(0xFFFEF2F2),
                                borderRadius: BorderRadius.circular(4),
                                border: Border.all(color: const Color(0xFFFCA5A5)),
                              ),
                              child: const Text('文件已失效', style: TextStyle(fontSize: 10, color: Color(0xFFDC2626))),
                            ),
                          ],
                        ],
                      ),
                      const SizedBox(height: 2),
                      Text(
                        widget.item.path,
                        style: const TextStyle(
                          fontSize: 11.5,
                          fontFamily: 'Consolas',
                          color: AppTheme.textDim,
                        ),
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                      ),
                    ],
                  ),
                ),
                const SizedBox(width: 16),

                // 相对时间
                Text(
                  widget.item.relativeTimeStr,
                  style: const TextStyle(
                    fontSize: 12,
                    color: AppTheme.textDim,
                  ),
                ),
                const SizedBox(width: 8),

                // 移除单项按钮
                IconButton(
                  key: WelcomeUiKeys.recentItemRemove(widget.index),
                  icon: const Icon(Icons.close_rounded, size: 16),
                  tooltip: '从最近列表中移除',
                  color: AppTheme.textDim,
                  splashRadius: 14,
                  padding: EdgeInsets.zero,
                  constraints: const BoxConstraints(minWidth: 28, minHeight: 28),
                  onPressed: widget.onRemove,
                ),
                const SizedBox(width: 4),

                // 载入箭头
                Icon(
                  Icons.chevron_right_rounded,
                  size: 18,
                  color: _isHovered ? AppTheme.primaryBlue : AppTheme.borderSubtle,
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
