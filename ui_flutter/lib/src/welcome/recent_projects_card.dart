import 'package:flutter/material.dart';
import '../ffi/geoscatter3d_service.dart';
import '../models/recent_project_model.dart';
import '../theme/macos_tokens.dart';
import 'welcome_ui_keys.dart';

class RecentProjectsCard extends StatelessWidget {
  final GeoScatter3dService service;

  const RecentProjectsCard({super.key, required this.service});

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
        backgroundColor: Colors.white,
        title: const Row(
          children: [
            Icon(
              Icons.delete_sweep_rounded,
              color: Color(0xFFDC2626),
              size: 22,
            ),
            SizedBox(width: 8),
            Text(
              '清空最近记录',
              style: TextStyle(fontSize: 16, fontWeight: FontWeight.w700),
            ),
          ],
        ),
        content: const Text(
          '确定要清空所有最近打开的项目记录吗？此操作不会删除磁盘上的任何实际数据文件。',
          style: TextStyle(fontSize: 13, color: Color(0xFF4B5563)),
        ),
        actions: [
          TextButton(
            key: WelcomeUiKeys.recentCancelClearButton,
            onPressed: () => Navigator.of(ctx).pop(),
            child: const Text('取消', style: TextStyle(color: Color(0xFF6B7280))),
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
              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(6)),
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
            // 表头标题与右侧「清除」
            Row(
              children: [
                const Text(
                  '最近打开的项目',
                  style: TextStyle(
                    fontSize: 14.5,
                    fontWeight: FontWeight.w700,
                    color: Color(0xFF111827),
                  ),
                ),
                const Spacer(),
                if (recentList.isNotEmpty)
                  InkWell(
                    key: WelcomeUiKeys.recentClearButton,
                    onTap: () => _onClearConfirm(context),
                    child: const Text(
                      '清除',
                      style: TextStyle(
                        fontSize: 12,
                        color: Color(0xFF9CA3AF),
                      ),
                    ),
                  ),
              ],
            ),
            const SizedBox(height: 12),

            // 表头列标题
            const Padding(
              padding: EdgeInsets.symmetric(vertical: 6),
              child: Row(
                children: [
                  SizedBox(
                    width: 220,
                    child: Text(
                      '项目名称',
                      style: TextStyle(
                        fontSize: 11,
                        fontWeight: FontWeight.w500,
                        color: Color(0xFF9CA3AF),
                      ),
                    ),
                  ),
                  Expanded(
                    child: Text(
                      '路径',
                      style: TextStyle(
                        fontSize: 11,
                        fontWeight: FontWeight.w500,
                        color: Color(0xFF9CA3AF),
                      ),
                    ),
                  ),
                  SizedBox(
                    width: 140,
                    child: Text(
                      '最后打开时间',
                      style: TextStyle(
                        fontSize: 11,
                        fontWeight: FontWeight.w500,
                        color: Color(0xFF9CA3AF),
                      ),
                    ),
                  ),
                  SizedBox(width: 48),
                ],
              ),
            ),
            const Divider(height: 1, color: Color(0xFFF3F4F6)),

            // 列表主体内容
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
                      color: Color(0xFFCBD5E1),
                    ),
                    const SizedBox(height: 10),
                    const Text(
                      '暂无最近打开的项目记录',
                      style: TextStyle(
                        fontSize: 13.5,
                        fontWeight: FontWeight.w600,
                        color: Color(0xFF111827),
                      ),
                    ),
                    const SizedBox(height: 4),
                    const Text(
                      '可使用上方卡片新建工区或打开已有 .gs3d 数据包',
                      style: TextStyle(fontSize: 11.5, color: Color(0xFF9CA3AF)),
                    ),
                    const SizedBox(height: 14),
                    OutlinedButton.icon(
                      key: WelcomeUiKeys.recentEmptyDemoButton,
                      onPressed: () =>
                          service.executeActionAsync('welcome.quick_demo'),
                      icon: const Icon(Icons.play_arrow_rounded, size: 16),
                      label: const Text(
                        '载入内置示例工区体验',
                        style: TextStyle(fontSize: 12),
                      ),
                      style: OutlinedButton.styleFrom(
                        foregroundColor: MacOsTokens.systemBlue,
                        side: const BorderSide(color: MacOsTokens.systemBlue),
                        shape: RoundedRectangleBorder(
                          borderRadius: BorderRadius.circular(6),
                        ),
                        padding: const EdgeInsets.symmetric(
                          horizontal: 14,
                          vertical: 8,
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
                    const Divider(height: 1, color: Color(0xFFF3F4F6)),
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

class _RecentProjectRow extends StatefulWidget {
  final int index;
  final RecentProjectItem item;
  final VoidCallback onTap;
  final VoidCallback onRemove;

  const _RecentProjectRow({
    super.key,
    required this.index,
    required this.item,
    required this.onTap,
    required this.onRemove,
  });

  @override
  State<_RecentProjectRow> createState() => _RecentProjectRowState();
}

class _RecentProjectRowState extends State<_RecentProjectRow> {
  bool _isHovered = false;

  @override
  Widget build(BuildContext context) {
    return MouseRegion(
      onEnter: (_) => setState(() => _isHovered = true),
      onExit: (_) => setState(() => _isHovered = false),
      cursor: SystemMouseCursors.click,
      child: InkWell(
        onTap: widget.onTap,
        hoverColor: const Color(0xFFF9FAFB),
        child: Padding(
          padding: const EdgeInsets.symmetric(vertical: 9),
          child: Row(
            children: [
              // 项目名称与徽标 (220px)
              SizedBox(
                width: 220,
                child: Row(
                  children: [
                    // 波形微图标徽标 (30x30, #F3F4F6)
                    Container(
                      width: 30,
                      height: 30,
                      decoration: BoxDecoration(
                        color: const Color(0xFFF3F4F6),
                        borderRadius: BorderRadius.circular(6),
                      ),
                      child: CustomPaint(
                        painter: const _WaveBadgeIconPainter(),
                      ),
                    ),
                    const SizedBox(width: 10),
                    Expanded(
                      child: Text(
                        widget.item.displayName,
                        style: const TextStyle(
                          fontSize: 13,
                          fontWeight: FontWeight.w500,
                          color: Color(0xFF111827),
                        ),
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                      ),
                    ),
                  ],
                ),
              ),

              // 路径
              Expanded(
                child: Padding(
                  padding: const EdgeInsets.only(right: 16),
                  child: Text(
                    widget.item.path,
                    style: const TextStyle(
                      fontSize: 12,
                      fontFamily: 'monospace',
                      color: Color(0xFF4B5563),
                    ),
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                  ),
                ),
              ),

              // 最后打开时间
              SizedBox(
                width: 140,
                child: Text(
                  widget.item.relativeTimeStr,
                  style: const TextStyle(
                    fontSize: 11.5,
                    color: Color(0xFF9CA3AF),
                  ),
                ),
              ),

              // 更多操作 ··· 与移除按钮
              SizedBox(
                width: 48,
                child: Row(
                  mainAxisAlignment: MainAxisAlignment.end,
                  children: [
                    IconButton(
                      key: WelcomeUiKeys.recentItemRemove(widget.index),
                      icon: Icon(
                        _isHovered ? Icons.close_rounded : Icons.more_horiz_rounded,
                        size: 15,
                        color: _isHovered ? const Color(0xFFEF4444) : const Color(0xFF9CA3AF),
                      ),
                      tooltip: '移除记录',
                      padding: EdgeInsets.zero,
                      constraints: const BoxConstraints(
                        minWidth: 26,
                        minHeight: 26,
                      ),
                      onPressed: widget.onRemove,
                    ),
                  ],
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _WaveBadgeIconPainter extends CustomPainter {
  const _WaveBadgeIconPainter();

  @override
  void paint(Canvas canvas, Size size) {
    final paint = Paint()
      ..color = const Color(0xFF94A3B8)
      ..strokeWidth = 1.4
      ..style = PaintingStyle.stroke;

    final path = Path();
    path.moveTo(6, size.height - 8);
    path.quadraticBezierTo(size.width * 0.4, size.height * 0.35, size.width * 0.6, size.height * 0.55);
    path.quadraticBezierTo(size.width * 0.8, size.height * 0.75, size.width - 6, size.height * 0.45);
    canvas.drawPath(path, paint);
  }

  @override
  bool shouldRepaint(covariant CustomPainter oldDelegate) => false;
}
