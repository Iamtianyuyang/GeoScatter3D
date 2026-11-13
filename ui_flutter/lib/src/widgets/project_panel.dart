import 'package:flutter/material.dart';

import '../ffi/app_abi.dart';
import '../ffi/geoscatter3d_service.dart';
import '../theme/app_tokens.dart';
import '../ui/format.dart';
import '../ui/ui_kit.dart';

/// 工程面板：当前数据集的身份（来源、范围、属性、CRS）+ 最近项目。
///
/// 测绘里「这是哪份数据、覆盖到哪」比任何可视化设置都重要，
/// 所以数值全部用真实 manifest 摘要，缺字段就显示「—」。
class ProjectPanel extends StatelessWidget {
  const ProjectPanel({
    super.key,
    required this.service,
    required this.onOpenBundle,
    this.onOpenRecent,
  });

  final GeoScatter3dService service;
  final VoidCallback onOpenBundle;
  final ValueChanged<String>? onOpenRecent;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    return ListenableBuilder(
      listenable: service,
      builder: (context, _) {
        final info = service.datasetInfo;
        final path = service.datasetPath;
        final hasDataset = path != null && path.isNotEmpty;
        return ListView(
          padding: const EdgeInsets.only(bottom: 24),
          children: [
            PanelSection(
              title: '数据集',
              child: hasDataset
                  ? _DatasetSummary(service: service, info: info)
                  : EmptyHint(
                      text: '尚未打开数据集',
                      hint: '打开预处理生成的 .gs3d 工程包，或先用「预处理」新建。',
                      icon: Icons.dataset_outlined,
                      action: AppButton(
                        label: '打开工程包',
                        icon: Icons.inventory_2_outlined,
                        tone: ButtonTone.primary,
                        onPressed: onOpenBundle,
                      ),
                    ),
            ),
            if (hasDataset) ...[
              PanelSection(
                padding: const EdgeInsets.fromLTRB(16, 0, 16, 12),
                child: Row(
                  children: [
                    Expanded(
                      child: AppButton(
                        label: '换一个工程包',
                        icon: Icons.inventory_2_outlined,
                        onPressed: onOpenBundle,
                      ),
                    ),
                    const SizedBox(width: 8),
                    AppButton(
                      label: '卸载',
                      icon: Icons.eject_outlined,
                      tone: ButtonTone.danger,
                      expand: false,
                      onPressed: () => service.unloadDataset(),
                    ),
                  ],
                ),
              ),
            ],
            const Hairline(),
            PanelSection(
              title: '最近项目',
              trailing: service.recentProjects.isEmpty
                  ? null
                  : TextButton(
                      onPressed: service.clearRecentProjects,
                      style: TextButton.styleFrom(
                        padding: EdgeInsets.zero,
                        minimumSize: const Size(0, 24),
                        tapTargetSize: MaterialTapTargetSize.shrinkWrap,
                        foregroundColor: tokens.colors.textLow,
                      ),
                      child: Text(
                        '清除',
                        style: tokens.text.meta.copyWith(
                          color: tokens.colors.textLow,
                        ),
                      ),
                    ),
              child: service.recentProjects.isEmpty
                  ? EmptyHint(
                      text: '没有最近项目',
                      hint: '打开过的工程包会记在这里，重启后可一键回到上次位置。',
                      icon: Icons.history,
                    )
                  : Column(
                      children: [
                        for (final project in service.recentProjects)
                          _RecentTile(
                            project: project,
                            active: path == project.path,
                            onOpen: onOpenRecent == null
                                ? null
                                : () => onOpenRecent!(project.path),
                            onForget: () =>
                                service.forgetRecentProject(project.path),
                          ),
                      ],
                    ),
            ),
          ],
        );
      },
    );
  }
}

class _DatasetSummary extends StatelessWidget {
  const _DatasetSummary({required this.service, required this.info});

  final GeoScatter3dService service;
  final Map<String, dynamic> info;

  String _range(String key) {
    final list = info[key];
    if (list is! List || list.length < 3) return '—';
    final values = list.map((v) => v is num ? v.toDouble() : 0.0).toList();
    return '${formatNumber(values[0])}, ${formatNumber(values[1])}, '
        '${formatNumber(values[2])}';
  }

  String _size() {
    final min = info['bbox_min'];
    final max = info['bbox_max'];
    if (min is! List || max is! List || min.length < 3 || max.length < 3) {
      return '—';
    }
    double d(int axis) {
      final a = min[axis];
      final b = max[axis];
      if (a is! num || b is! num) return 0;
      return (b - a).abs().toDouble();
    }

    return '${formatNumber(d(0))} × ${formatNumber(d(1))} × ${formatNumber(d(2))}';
  }

  String _crs() {
    final crs = info['crs'];
    if (crs is! Map) return '未记录';
    final authority = crs['authority'];
    final code = crs['code'];
    final unit = crs['unit'];
    final vertical = crs['vertical_unit'];
    final parts = <String>[];
    if (authority is String && code != null) parts.add('$authority:$code');
    if (unit is String) parts.add(unit);
    if (vertical is String) parts.add('高程 $vertical');
    return parts.isEmpty ? '未记录' : parts.join(' · ');
  }

  String _attributes() {
    final attributes = info['attributes'];
    if (attributes is! List || attributes.isEmpty) return '仅坐标';
    return attributes
        .map((a) => a is Map ? (a['name'] ?? '?').toString() : '?')
        .join('、');
  }

  String get _valueRange {
    final min = info['value_min'];
    final max = info['value_max'];
    if (min is! num || max is! num) return '—';
    return '${formatNumber(min)} ~ ${formatNumber(max)}';
  }

  @override
  Widget build(BuildContext context) {
    final path = service.datasetPath ?? '';
    final info = this.info;
    final version = info['format_version'];
    final chunks = info['chunk_count'];
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        Padding(
          padding: const EdgeInsets.only(bottom: 10),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                fileName(path),
                key: const ValueKey('project_dataset_name'),
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: context.tokens.text.bodyStrong.copyWith(
                  color: context.tokens.colors.textHigh,
                  fontSize: 14,
                ),
              ),
              const SizedBox(height: 2),
              SelectableText(
                path,
                maxLines: 2,
                style: context.tokens.text.meta.copyWith(
                  color: context.tokens.colors.textLow,
                  fontSize: 11,
                ),
              ),
            ],
          ),
        ),
        KeyValueRow(
          label: '点数',
          numeric: true,
          value: formatCount(info['point_count'] as num? ??
              service.sourcePointCount),
        ),
        KeyValueRow(
          label: '绘制',
          numeric: true,
          value: formatCount(service.pointCount),
        ),
        KeyValueRow(label: '范围', numeric: true, value: _size()),
        KeyValueRow(label: '原点', numeric: true, value: _range('origin'), maxLines: 3),
        KeyValueRow(label: '值域', numeric: true, value: _valueRange),
        KeyValueRow(label: '属性', value: _attributes(), maxLines: 3),
        KeyValueRow(label: '坐标系', value: _crs(), maxLines: 2),
        KeyValueRow(
          label: '工程包',
          value: version == null
              ? '—'
              : 'v$version · ${chunks ?? '?'} 块'
                  '${info['lod_sidecar'] == true ? ' · 含 LOD' : ''}',
        ),
        KeyValueRow(label: 'LOD', value: service.lodSummary.isEmpty ? '—' : service.lodSummary),
        Padding(
          padding: const EdgeInsets.only(top: 6),
          child: Text(
            '数值来自工程包清单；「绘制」是当前 LOD 实际送进显卡的点数。',
            style: context.tokens.text.meta.copyWith(
              color: context.tokens.colors.textLow,
              fontSize: 10.5,
            ),
          ),
        ),
      ],
    );
  }
}

class _RecentTile extends StatelessWidget {
  const _RecentTile({
    required this.project,
    required this.active,
    required this.onOpen,
    required this.onForget,
  });

  final RecentProject project;
  final bool active;
  final VoidCallback? onOpen;
  final VoidCallback onForget;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    final opened = DateTime.fromMillisecondsSinceEpoch(
      project.lastOpened > 1000000000000
          ? project.lastOpened
          : project.lastOpened * 1000,
    );
    return InkWell(
      onTap: onOpen,
      child: Container(
        padding: const EdgeInsets.symmetric(vertical: 8, horizontal: 4),
        decoration: BoxDecoration(
          border: Border(
            bottom: BorderSide(color: tokens.colors.line.withValues(alpha: 0.6)),
          ),
        ),
        child: Row(
          children: [
            Icon(
              Icons.folder_outlined,
              size: 15,
              color: active ? tokens.colors.signal : tokens.colors.textLow,
            ),
            const SizedBox(width: 8),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                mainAxisSize: MainAxisSize.min,
                children: [
                  Text(
                    fileName(project.path),
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: tokens.text.body.copyWith(
                      color: active
                          ? tokens.colors.signal
                          : tokens.colors.textHigh,
                      fontSize: 12.5,
                    ),
                  ),
                  Text(
                    '${shortPath(project.path)} · ${formatRelativeTime(opened)}',
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: tokens.text.meta.copyWith(
                      color: tokens.colors.textLow,
                      fontSize: 10.5,
                    ),
                  ),
                ],
              ),
            ),
            IconButton(
              tooltip: '从最近项目移除',
              onPressed: onForget,
              visualDensity: VisualDensity.compact,
              iconSize: 14,
              color: tokens.colors.textLow,
              icon: const Icon(Icons.close),
            ),
          ],
        ),
      ),
    );
  }
}
