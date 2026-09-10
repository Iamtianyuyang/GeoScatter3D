import 'package:flutter/material.dart';
import '../ffi/geoscatter3d_service.dart';
import '../theme/gs3d_tokens.dart';

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
            ? Gs3dTokens.success
            : (fps >= 30 ? Gs3dTokens.warning : Gs3dTokens.danger);

        final gpuMb = (gpuBytes / (1024 * 1024)).toStringAsFixed(1);

        return Stack(
          children: [
            // 1. 半透明暗色背景，点击遮罩关闭
            Positioned.fill(
              child: GestureDetector(
                onTap: () => service.togglePerformancePanel(false),
                child: Container(
                  color: Gs3dMaterial.scrimStrong,
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
                  color: Gs3dTokens.surface,
                  borderRadius: BorderRadius.circular(12),
                  border: Border.all(color: Gs3dTokens.border, width: 1),
                  boxShadow: const [
                    BoxShadow(
                      color: Gs3dMaterial.scrimStrong,
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
                          color: Gs3dTokens.surfaceSunken,
                          border: Border(bottom: BorderSide(color: Gs3dTokens.border, width: 1)),
                        ),
                        child: Row(
                          children: [
                            Container(
                              padding: const EdgeInsets.all(6),
                              decoration: BoxDecoration(
                                color: Gs3dTokens.accent.withAlpha(30),
                                borderRadius: BorderRadius.circular(6),
                              ),
                              child: const Icon(Icons.speed_rounded, size: 20, color: Gs3dTokens.accent),
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
                                      color: Gs3dTokens.textPrimary,
                                    ),
                                  ),
                                  SizedBox(height: 2),
                                  Text(
                                    'Real-time Rendering & Tile Streaming Profiler',
                                    style: TextStyle(fontSize: 11, color: Gs3dTokens.textSecondary),
                                    maxLines: 1,
                                    overflow: TextOverflow.ellipsis,
                                  ),
                                ],
                              ),
                            ),
                            IconButton(
                              icon: const Icon(Icons.close_rounded, size: 20, color: Gs3dTokens.textSecondary),
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
                                      color: Gs3dTokens.surfaceSunken,
                                      borderRadius: BorderRadius.circular(8),
                                      border: Border.all(color: Gs3dTokens.border.withAlpha(120)),
                                    ),
                                    child: Column(
                                      crossAxisAlignment: CrossAxisAlignment.start,
                                      children: [
                                        const Text('渲染帧率 (FPS)', style: TextStyle(fontSize: 12, color: Gs3dTokens.textSecondary)),
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
                                            const Text('FPS', style: TextStyle(fontSize: 12, color: Gs3dTokens.textSecondary)),
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
                                      color: Gs3dTokens.surfaceSunken,
                                      borderRadius: BorderRadius.circular(8),
                                      border: Border.all(color: Gs3dTokens.border.withAlpha(120)),
                                    ),
                                    child: Column(
                                      crossAxisAlignment: CrossAxisAlignment.start,
                                      children: [
                                        const Text('单帧耗时 (Frame Time)', style: TextStyle(fontSize: 12, color: Gs3dTokens.textSecondary)),
                                        const SizedBox(height: 6),
                                        Row(
                                          children: [
                                            Text(
                                              frameTime.toStringAsFixed(2),
                                              style: const TextStyle(
                                                fontSize: 26,
                                                fontWeight: FontWeight.w800,
                                                color: Gs3dTokens.accent,
                                                fontFamily: 'monospace',
                                              ),
                                            ),
                                            const SizedBox(width: 6),
                                            const Text('ms', style: TextStyle(fontSize: 12, color: Gs3dTokens.textSecondary)),
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
                                color: Gs3dTokens.surfaceSunken,
                                borderRadius: BorderRadius.circular(8),
                                border: Border.all(color: Gs3dTokens.border.withAlpha(120)),
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
                                      const Text('瓦片缓存命中率', style: TextStyle(fontSize: 12.5, color: Gs3dTokens.textSecondary)),
                                      Text(
                                        '${hitRate.toStringAsFixed(1)}%',
                                        style: const TextStyle(
                                          fontSize: 13,
                                          fontWeight: FontWeight.w700,
                                          color: Gs3dTokens.success,
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
                                      backgroundColor: Gs3dTokens.borderStrong.withAlpha(80),
                                      valueColor: const AlwaysStoppedAnimation<Color>(Gs3dTokens.success),
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
                                color: Gs3dTokens.surfaceSunken,
                                borderRadius: BorderRadius.circular(6),
                                border: Border.all(color: Gs3dTokens.border.withAlpha(80)),
                              ),
                              child: Row(
                                children: [
                                  const Icon(Icons.videocam_outlined, size: 15, color: Gs3dTokens.textSecondary),
                                  const SizedBox(width: 8),
                                  const Text('相机参考坐标: ', style: TextStyle(fontSize: 11.5, color: Gs3dTokens.textSecondary)),
                                  Expanded(
                                    child: Text(
                                      coords,
                                      style: const TextStyle(
                                        fontSize: 11.5,
                                        color: Gs3dTokens.textPrimary,
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
                          color: Gs3dTokens.surfaceSunken,
                          border: Border(top: BorderSide(color: Gs3dTokens.border, width: 1)),
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
                                backgroundColor: Gs3dTokens.accent,
                                foregroundColor: Gs3dTokens.textOnAccent,
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
        Text(label, style: const TextStyle(fontSize: 12.5, color: Gs3dTokens.textSecondary)),
        Text(
          value,
          style: const TextStyle(
            fontSize: 13,
            fontWeight: FontWeight.w600,
            color: Gs3dTokens.textPrimary,
            fontFamily: 'monospace',
          ),
        ),
      ],
    );
  }
}
