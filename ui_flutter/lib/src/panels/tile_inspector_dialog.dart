import 'package:flutter/material.dart';
import '../ffi/geoscatter3d_service.dart';
import '../theme/app_theme.dart';

/// 八叉树瓦片与多级 LOD 流式加载视察器 (Octree Tile & LOD Streaming Inspector)
class TileInspectorDialog extends StatelessWidget {
  final GeoScatter3dService service;

  const TileInspectorDialog({
    super.key,
    required this.service,
  });

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: service,
      builder: (context, _) {
        final summary = service.summary;
        final bmin = summary.bboxMin;
        final bmax = summary.bboxMax;
        final loadedTiles = service.loadedTiles;
        final pendingTiles = service.pendingTiles;
        final hitRate = service.cacheHitRate;

        final spanX = (bmax[0] - bmin[0]).abs();
        final spanY = (bmax[1] - bmin[1]).abs();
        final spanZ = (bmax[2] - bmin[2]).abs();

        final lodList = summary.lodDetails.isNotEmpty
            ? summary.lodDetails
            : [
                'LOD 0 (完整分辨率) - 采样率 100% - 全景高精',
                'LOD 1 (中景半精采样) - 采样率 50% - 视距 > 100m',
                'LOD 2 (远景四分采样) - 采样率 25% - 视距 > 300m',
                'LOD 3 (全景八分骨架) - 采样率 12.5% - 视距 > 800m',
              ];

        return Stack(
          children: [
            // 1. 半透明遮罩
            Positioned.fill(
              child: GestureDetector(
                onTap: () => service.toggleTileInspector(false),
                child: Container(
                  color: Colors.black.withAlpha(120),
                ),
              ),
            ),

            // 2. 居中检查器卡片
            Center(
              child: Container(
                key: const ValueKey('tile_inspector_dialog'),
                width: 660,
                constraints: const BoxConstraints(maxHeight: 620),
                margin: const EdgeInsets.all(20),
                decoration: BoxDecoration(
                  color: AppTheme.surface,
                  borderRadius: BorderRadius.circular(12),
                  border: Border.all(color: AppTheme.border, width: 1),
                  boxShadow: const [
                    BoxShadow(
                      color: Colors.black45,
                      blurRadius: 24,
                      offset: Offset(0, 10),
                    ),
                  ],
                ),
                child: ClipRRect(
                  borderRadius: BorderRadius.circular(12),
                  child: Column(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      // 顶部标题栏
                      Container(
                        padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 14),
                        decoration: const BoxDecoration(
                          color: AppTheme.surfaceMuted,
                          border: Border(bottom: BorderSide(color: AppTheme.border, width: 1)),
                        ),
                        child: Row(
                          children: [
                            Container(
                              padding: const EdgeInsets.all(6),
                              decoration: BoxDecoration(
                                color: AppTheme.primaryBlue.withAlpha(30),
                                borderRadius: BorderRadius.circular(6),
                              ),
                              child: const Icon(Icons.grid_view_rounded, size: 20, color: AppTheme.primaryBlue),
                            ),
                            const SizedBox(width: 12),
                            Expanded(
                              child: Column(
                                crossAxisAlignment: CrossAxisAlignment.start,
                                children: const [
                                  Text(
                                    '八叉树瓦片与 LOD 流式视察器',
                                    style: TextStyle(
                                      fontSize: 15,
                                      fontWeight: FontWeight.w700,
                                      color: AppTheme.textTitle,
                                    ),
                                  ),
                                  SizedBox(height: 2),
                                  Text(
                                    'Octree Tile Pyramid & LOD Streaming Pipeline Inspector',
                                    style: TextStyle(fontSize: 11, color: AppTheme.textDim),
                                    maxLines: 1,
                                    overflow: TextOverflow.ellipsis,
                                  ),
                                ],
                              ),
                            ),
                            IconButton(
                              icon: const Icon(Icons.close_rounded, size: 20, color: AppTheme.textDim),
                              onPressed: () => service.toggleTileInspector(false),
                              splashRadius: 18,
                              tooltip: '关闭',
                            ),
                          ],
                        ),
                      ),

                      // 中部视察详情
                      Flexible(
                        child: ListView(
                          padding: const EdgeInsets.all(20),
                          shrinkWrap: true,
                          children: [
                            // 空间包围盒与数据特征
                            _buildSectionHeader('工区空间范围与几何边界 (GS3D v2)', Icons.view_in_ar_rounded),
                            const SizedBox(height: 8),
                            Container(
                              padding: const EdgeInsets.all(12),
                              decoration: BoxDecoration(
                                color: AppTheme.surfaceMuted,
                                borderRadius: BorderRadius.circular(8),
                                border: Border.all(color: AppTheme.border.withAlpha(100)),
                              ),
                              child: Column(
                                children: [
                                  _buildDataRow('数据集名称', summary.name),
                                  const Divider(height: 14),
                                  _buildDataRow('数据点总数', summary.formattedPointCount),
                                  const Divider(height: 14),
                                  _buildDataRow(
                                    '最小三维边界 (BBox Min)',
                                    '(${bmin[0].toStringAsFixed(2)}, ${bmin[1].toStringAsFixed(2)}, ${bmin[2].toStringAsFixed(2)})',
                                  ),
                                  const Divider(height: 14),
                                  _buildDataRow(
                                    '最大三维边界 (BBox Max)',
                                    '(${bmax[0].toStringAsFixed(2)}, ${bmax[1].toStringAsFixed(2)}, ${bmax[2].toStringAsFixed(2)})',
                                  ),
                                  const Divider(height: 14),
                                  _buildDataRow(
                                    '空间跨度 (ΔX × ΔY × ΔZ)',
                                    '${spanX.toStringAsFixed(1)}m × ${spanY.toStringAsFixed(1)}m × ${spanZ.toStringAsFixed(1)}m',
                                  ),
                                ],
                              ),
                            ),
                            const SizedBox(height: 18),

                            // 八叉树分级 LOD 结构
                            _buildSectionHeader('八叉树 LOD 金字塔分级', Icons.layers_outlined),
                            const SizedBox(height: 8),
                            Container(
                              padding: const EdgeInsets.all(12),
                              decoration: BoxDecoration(
                                color: AppTheme.surfaceMuted,
                                borderRadius: BorderRadius.circular(8),
                                border: Border.all(color: AppTheme.border.withAlpha(100)),
                              ),
                              child: Column(
                                children: lodList.asMap().entries.map((entry) {
                                  final idx = entry.key;
                                  final detail = entry.value;
                                  final isBase = idx == 0;
                                  return Container(
                                    margin: EdgeInsets.only(bottom: idx < lodList.length - 1 ? 8 : 0),
                                    padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
                                    decoration: BoxDecoration(
                                      color: isBase ? AppTheme.primaryBlue.withAlpha(20) : AppTheme.surface,
                                      borderRadius: BorderRadius.circular(6),
                                      border: Border.all(
                                        color: isBase ? AppTheme.primaryBlue.withAlpha(100) : AppTheme.border,
                                      ),
                                    ),
                                    child: Row(
                                      children: [
                                        Container(
                                          padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
                                          decoration: BoxDecoration(
                                            color: isBase ? AppTheme.primaryBlue : AppTheme.textDim,
                                            borderRadius: BorderRadius.circular(4),
                                          ),
                                          child: Text(
                                            'LOD $idx',
                                            style: const TextStyle(fontSize: 10, color: Colors.white, fontWeight: FontWeight.bold),
                                          ),
                                        ),
                                        const SizedBox(width: 10),
                                        Expanded(
                                          child: Text(
                                            detail,
                                            style: const TextStyle(fontSize: 12, color: AppTheme.textTitle),
                                          ),
                                        ),
                                        Icon(
                                          Icons.check_circle_rounded,
                                          size: 15,
                                          color: isBase ? const Color(0xFF10B981) : AppTheme.textDim,
                                        ),
                                      ],
                                    ),
                                  );
                                }).toList(),
                              ),
                            ),
                            const SizedBox(height: 18),

                            // 流式瓦片流水线状态
                            _buildSectionHeader('瓦片流式加载与缓存状态', Icons.memory_rounded),
                            const SizedBox(height: 8),
                            Container(
                              padding: const EdgeInsets.all(12),
                              decoration: BoxDecoration(
                                color: AppTheme.surfaceMuted,
                                borderRadius: BorderRadius.circular(8),
                                border: Border.all(color: AppTheme.border.withAlpha(100)),
                              ),
                              child: Row(
                                children: [
                                  Expanded(
                                    child: _buildTileStatCard('已装载瓦片', '$loadedTiles', AppTheme.primaryBlue),
                                  ),
                                  const SizedBox(width: 10),
                                  Expanded(
                                    child: _buildTileStatCard('等待 IO 队列', '$pendingTiles', Colors.orangeAccent),
                                  ),
                                  const SizedBox(width: 10),
                                  Expanded(
                                    child: _buildTileStatCard('缓存命中率', '${hitRate.toStringAsFixed(1)}%', const Color(0xFF10B981)),
                                  ),
                                ],
                              ),
                            ),
                          ],
                        ),
                      ),

                      // 底部动作栏
                      Container(
                        padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 12),
                        decoration: const BoxDecoration(
                          color: AppTheme.surfaceMuted,
                          border: Border(top: BorderSide(color: AppTheme.border, width: 1)),
                        ),
                        child: Row(
                          children: [
                            OutlinedButton.icon(
                              key: const ValueKey('btn_tile_inspector_clear'),
                              style: OutlinedButton.styleFrom(
                                foregroundColor: AppTheme.textTitle,
                                side: const BorderSide(color: AppTheme.border),
                                padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                              ),
                              icon: const Icon(Icons.refresh_rounded, size: 16),
                              label: const Text('强制刷新瓦片缓存', style: TextStyle(fontSize: 12.5)),
                              onPressed: () {
                                service.clearCache();
                                ScaffoldMessenger.of(context).showSnackBar(
                                  const SnackBar(
                                    content: Text('已触发八叉树瓦片与显存缓存重置'),
                                    duration: Duration(seconds: 2),
                                    behavior: SnackBarBehavior.floating,
                                  ),
                                );
                              },
                            ),
                            const Spacer(),
                            ElevatedButton(
                              style: ElevatedButton.styleFrom(
                                backgroundColor: AppTheme.primaryBlue,
                                foregroundColor: Colors.white,
                                elevation: 0,
                                padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 8),
                              ),
                              onPressed: () => service.toggleTileInspector(false),
                              child: const Text('完成', style: TextStyle(fontSize: 12.5)),
                            ),
                          ],
                        ),
                      ),
                    ],
                  ),
                ),
              ),
            ),
          ],
        );
      },
    );
  }

  Widget _buildSectionHeader(String title, IconData icon) {
    return Row(
      children: [
        Icon(icon, size: 15, color: AppTheme.primaryBlue),
        const SizedBox(width: 6),
        Text(
          title,
          style: const TextStyle(fontSize: 13, fontWeight: FontWeight.w700, color: AppTheme.textTitle),
        ),
      ],
    );
  }

  Widget _buildDataRow(String label, String value) {
    return Row(
      mainAxisAlignment: MainAxisAlignment.spaceBetween,
      children: [
        Text(label, style: const TextStyle(fontSize: 12, color: AppTheme.textDim)),
        Text(
          value,
          style: const TextStyle(
            fontSize: 12,
            fontWeight: FontWeight.w600,
            color: AppTheme.textTitle,
            fontFamily: 'Consolas',
          ),
        ),
      ],
    );
  }

  Widget _buildTileStatCard(String label, String value, Color color) {
    return Container(
      padding: const EdgeInsets.symmetric(vertical: 10, horizontal: 8),
      decoration: BoxDecoration(
        color: AppTheme.surface,
        borderRadius: BorderRadius.circular(6),
        border: Border.all(color: AppTheme.border),
      ),
      child: Column(
        children: [
          Text(label, style: const TextStyle(fontSize: 11, color: AppTheme.textDim)),
          const SizedBox(height: 4),
          Text(
            value,
            style: TextStyle(fontSize: 16, fontWeight: FontWeight.w800, color: color, fontFamily: 'monospace'),
          ),
        ],
      ),
    );
  }
}
