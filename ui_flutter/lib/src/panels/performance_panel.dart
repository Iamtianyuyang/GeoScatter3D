import 'package:flutter/material.dart';
import '../ffi/geoscatter3d_service.dart';
import '../theme/app_theme.dart';

/// 性能指标与显存实时诊断面板 (Performance & Memory Diagnostic Panel)
class PerformancePanel extends StatelessWidget {
  final GeoScatter3dService service;

  const PerformancePanel({
    super.key,
    required this.service,
  });

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: service,
      builder: (context, _) {
        final fps = service.fps;
        final frameTime = service.frameTimeMs;
        final visiblePts = service.visiblePoints;
        final gpuBytes = service.gpuMemoryBytes;
        final loadedTiles = service.loadedTiles;
        final pendingTiles = service.pendingTiles;
        final hitRate = service.cacheHitRate;
        final coords = service.cameraCoordsString;

        final fpsColor = fps >= 60
            ? const Color(0xFF10B981)
            : (fps >= 30 ? const Color(0xFFF59E0B) : const Color(0xFFEF4444));

        final gpuMb = (gpuBytes / (1024 * 1024)).toStringAsFixed(1);

        return Stack(
          children: [
            // 1. 半透明暗色背景，点击遮罩关闭
            Positioned.fill(
              child: GestureDetector(
                onTap: () => service.togglePerformancePanel(false),
                child: Container(
                  color: Colors.black.withAlpha(120),
                ),
              ),
            ),

            // 2. 居中诊断卡片
            Center(
              child: Container(
                key: const ValueKey('performance_panel_modal'),
                width: 520,
                constraints: const BoxConstraints(maxHeight: 580),
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
                        padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 14),
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
                              child: const Icon(Icons.speed_rounded, size: 20, color: AppTheme.primaryBlue),
                            ),
                            const SizedBox(width: 12),
                            Expanded(
                              child: Column(
                                crossAxisAlignment: CrossAxisAlignment.start,
                                children: const [
                                  Text(
                                    '性能指标与显存实时诊断',
                                    style: TextStyle(
                                      fontSize: 15,
                                      fontWeight: FontWeight.w700,
                                      color: AppTheme.textTitle,
                                    ),
                                  ),
                                  SizedBox(height: 2),
                                  Text(
                                    'Real-time Rendering & Tile Streaming Profiler',
                                    style: TextStyle(fontSize: 11, color: AppTheme.textDim),
                                    maxLines: 1,
                                    overflow: TextOverflow.ellipsis,
                                  ),
                                ],
                              ),
                            ),
                            IconButton(
                              icon: const Icon(Icons.close_rounded, size: 20, color: AppTheme.textDim),
                              onPressed: () => service.togglePerformancePanel(false),
                              splashRadius: 18,
                              tooltip: '关闭',
                            ),
                          ],
                        ),
                      ),

                      // 内容区域
                      Padding(
                        padding: const EdgeInsets.all(18),
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            // 帧率与帧耗时突出展示
                            Row(
                              children: [
                                Expanded(
                                  child: Container(
                                    padding: const EdgeInsets.all(14),
                                    decoration: BoxDecoration(
                                      color: AppTheme.surfaceMuted,
                                      borderRadius: BorderRadius.circular(8),
                                      border: Border.all(color: AppTheme.border.withAlpha(120)),
                                    ),
                                    child: Column(
                                      crossAxisAlignment: CrossAxisAlignment.start,
                                      children: [
                                        const Text('渲染帧率 (FPS)', style: TextStyle(fontSize: 12, color: AppTheme.textDim)),
                                        const SizedBox(height: 6),
                                        Row(
                                          children: [
                                            Text(
                                              fps.toStringAsFixed(1),
                                              style: TextStyle(
                                                fontSize: 26,
                                                fontWeight: FontWeight.w800,
                                                color: fpsColor,
                                                fontFamily: 'monospace',
                                              ),
                                            ),
                                            const SizedBox(width: 6),
                                            const Text('FPS', style: TextStyle(fontSize: 12, color: AppTheme.textDim)),
                                          ],
                                        ),
                                      ],
                                    ),
                                  ),
                                ),
                                const SizedBox(width: 12),
                                Expanded(
                                  child: Container(
                                    padding: const EdgeInsets.all(14),
                                    decoration: BoxDecoration(
                                      color: AppTheme.surfaceMuted,
                                      borderRadius: BorderRadius.circular(8),
                                      border: Border.all(color: AppTheme.border.withAlpha(120)),
                                    ),
                                    child: Column(
                                      crossAxisAlignment: CrossAxisAlignment.start,
                                      children: [
                                        const Text('单帧耗时 (Frame Time)', style: TextStyle(fontSize: 12, color: AppTheme.textDim)),
                                        const SizedBox(height: 6),
                                        Row(
                                          children: [
                                            Text(
                                              frameTime.toStringAsFixed(2),
                                              style: const TextStyle(
                                                fontSize: 26,
                                                fontWeight: FontWeight.w800,
                                                color: AppTheme.primaryBlue,
                                                fontFamily: 'monospace',
                                              ),
                                            ),
                                            const SizedBox(width: 6),
                                            const Text('ms', style: TextStyle(fontSize: 12, color: AppTheme.textDim)),
                                          ],
                                        ),
                                      ],
                                    ),
                                  ),
                                ),
                              ],
                            ),
                            const SizedBox(height: 14),

                            // 流式瓦片与显存状态卡片
                            Container(
                              padding: const EdgeInsets.all(14),
                              decoration: BoxDecoration(
                                color: AppTheme.surfaceMuted,
                                borderRadius: BorderRadius.circular(8),
                                border: Border.all(color: AppTheme.border.withAlpha(120)),
                              ),
                              child: Column(
                                children: [
                                  _buildMetricRow('可见点云规模', '$visiblePts 点'),
                                  const Divider(height: 16),
                                  _buildMetricRow('显存与缓冲占用', '$gpuMb MB'),
                                  const Divider(height: 16),
                                  _buildMetricRow('已加载瓦片数 (Loaded)', '$loadedTiles 块'),
                                  const Divider(height: 16),
                                  _buildMetricRow('挂起等待 IO 瓦片 (Pending)', '$pendingTiles 块'),
                                  const Divider(height: 16),
                                  Row(
                                    mainAxisAlignment: MainAxisAlignment.spaceBetween,
                                    children: [
                                      const Text('瓦片缓存命中率', style: TextStyle(fontSize: 12.5, color: AppTheme.textDim)),
                                      Text(
                                        '${hitRate.toStringAsFixed(1)}%',
                                        style: const TextStyle(
                                          fontSize: 13,
                                          fontWeight: FontWeight.w700,
                                          color: Color(0xFF10B981),
                                          fontFamily: 'monospace',
                                        ),
                                      ),
                                    ],
                                  ),
                                  const SizedBox(height: 6),
                                  ClipRRect(
                                    borderRadius: BorderRadius.circular(4),
                                    child: LinearProgressIndicator(
                                      value: (hitRate / 100.0).clamp(0.0, 1.0),
                                      backgroundColor: AppTheme.borderSubtle.withAlpha(80),
                                      valueColor: const AlwaysStoppedAnimation<Color>(Color(0xFF10B981)),
                                      minHeight: 6,
                                    ),
                                  ),
                                ],
                              ),
                            ),
                            const SizedBox(height: 12),

                            // 视口与相机位姿
                            Container(
                              width: double.infinity,
                              padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                              decoration: BoxDecoration(
                                color: AppTheme.surfaceMuted,
                                borderRadius: BorderRadius.circular(6),
                                border: Border.all(color: AppTheme.border.withAlpha(80)),
                              ),
                              child: Row(
                                children: [
                                  const Icon(Icons.videocam_outlined, size: 15, color: AppTheme.textDim),
                                  const SizedBox(width: 8),
                                  const Text('相机参考坐标: ', style: TextStyle(fontSize: 11.5, color: AppTheme.textDim)),
                                  Expanded(
                                    child: Text(
                                      coords,
                                      style: const TextStyle(
                                        fontSize: 11.5,
                                        color: AppTheme.textTitle,
                                        fontFamily: 'monospace',
                                        fontWeight: FontWeight.w600,
                                      ),
                                    ),
                                  ),
                                ],
                              ),
                            ),
                          ],
                        ),
                      ),

                      // 底部按钮栏
                      Container(
                        padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 12),
                        decoration: const BoxDecoration(
                          color: AppTheme.surfaceMuted,
                          border: Border(top: BorderSide(color: AppTheme.border, width: 1)),
                        ),
                        child: Row(
                          children: [
                            OutlinedButton.icon(
                              key: const ValueKey('btn_clear_cache'),
                              style: OutlinedButton.styleFrom(
                                foregroundColor: Colors.redAccent,
                                side: const BorderSide(color: Colors.redAccent),
                                padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                              ),
                              icon: const Icon(Icons.delete_outline_rounded, size: 16),
                              label: const Text('清空显存缓存', style: TextStyle(fontSize: 12.5)),
                              onPressed: () {
                                service.clearCache();
                                ScaffoldMessenger.of(context).showSnackBar(
                                  const SnackBar(
                                    content: Text('已清空显存与瓦片缓存'),
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
                                padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 8),
                              ),
                              onPressed: () => service.togglePerformancePanel(false),
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

  Widget _buildMetricRow(String label, String value) {
    return Row(
      mainAxisAlignment: MainAxisAlignment.spaceBetween,
      children: [
        Text(label, style: const TextStyle(fontSize: 12.5, color: AppTheme.textDim)),
        Text(
          value,
          style: const TextStyle(
            fontSize: 13,
            fontWeight: FontWeight.w600,
            color: AppTheme.textTitle,
            fontFamily: 'monospace',
          ),
        ),
      ],
    );
  }
}
