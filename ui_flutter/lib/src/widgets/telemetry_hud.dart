import 'dart:ui';
import 'package:flutter/material.dart';

/// 纯净极简实时遥测 HUD 浮层 (Feature #1)
class TelemetryHud extends StatelessWidget {
  final double fps;
  final double frameTimeMs;
  /// 原生渲染线程上“提交 + 回读”的实际耗时；0 表示不可用（不展示）。
  final double renderTimeMs;
  final int visiblePoints;
  /// LOD 规模摘要（如 “39.3 万 / 1000 万 点 · LOD 6/6”）；空串则不展示。
  final String lodSummary;
  final String gpuName;
  final String status;
  /// 当前生效的取帧通路（如「零拷贝纹理」/「RGBA 回读」）；空串则不展示。
  /// 真机排障用：一眼看出引擎是否已接管 Flutter 纹理层。
  final String renderPath;

  const TelemetryHud({
    super.key,
    required this.fps,
    required this.frameTimeMs,
    this.renderTimeMs = 0,
    required this.visiblePoints,
    this.lodSummary = '',
    this.gpuName = 'Vulkan GPU',
    this.status = '就绪',
    this.renderPath = '',
  });

  @override
  Widget build(BuildContext context) {
    return ClipRRect(
      key: const Key('telemetry_hud'),
      borderRadius: BorderRadius.circular(10),
      child: BackdropFilter(
        filter: ImageFilter.blur(sigmaX: 8, sigmaY: 8),
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 10),
          decoration: BoxDecoration(
            color: const Color(0xCC0F172A), // 半透明 Slate 900
            borderRadius: BorderRadius.circular(10),
            border: Border.all(
              color: const Color(0x3338BDF8),
              width: 1,
            ), // 浅天蓝边框
            boxShadow: const [
              BoxShadow(
                color: Color(0x40000000),
                blurRadius: 12,
                offset: Offset(0, 4),
              ),
            ],
          ),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              // 标题栏与在线指示灯
              Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Container(
                    width: 8,
                    height: 8,
                    decoration: const BoxDecoration(
                      color: Color(0xFF10B981), // Emerald 500
                      shape: BoxShape.circle,
                      boxShadow: [
                        BoxShadow(
                          color: Color(0x8010B981),
                          blurRadius: 4,
                          spreadRadius: 1,
                        ),
                      ],
                    ),
                  ),
                  const SizedBox(width: 8),
                  const Text(
                    'GeoScatter3D Vulkan',
                    style: TextStyle(
                      color: Color(0xFFF1F5F9),
                      fontSize: 12,
                      fontWeight: FontWeight.w600,
                      letterSpacing: 0.3,
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 6),

              // 核心指标栏
              Text(
                key: const ValueKey('viewport_metrics'),
                '${fps.toStringAsFixed(1)} FPS | ${frameTimeMs.toStringAsFixed(2)} ms',
                style: const TextStyle(
                  color: Color(0xFF38BDF8), // Sky 400
                  fontSize: 13,
                  fontWeight: FontWeight.bold,
                  fontFamily: 'monospace',
                ),
              ),
              const SizedBox(height: 2),

              // 引擎侧耗时：渲染线程提交 + 回读，与 UI 端解码/绘制解耦
              if (renderTimeMs > 0) ...[
                Text(
                  key: const ValueKey('viewport_render_cost'),
                  '引擎 ${renderTimeMs.toStringAsFixed(2)} ms',
                  style: const TextStyle(
                    color: Color(0xFF7DD3FC), // Sky 300
                    fontSize: 11,
                    fontFamily: 'monospace',
                  ),
                ),
                const SizedBox(height: 2),
              ],

              // 点数、状态与当前取帧通路
              Text(
                key: const ValueKey('viewport_status'),
                '$visiblePoints 可见点 | $status'
                '${renderPath.isEmpty ? '' : ' · $renderPath'}',
                style: const TextStyle(
                  color: Color(0xFF94A3B8), // Slate 400
                  fontSize: 11,
                  fontFamily: 'monospace',
                ),
              ),
              const SizedBox(height: 2),

              // LOD 规模：源点数远大于绘制量时出现，避免“看不到全量数据”的误解
              if (lodSummary.isNotEmpty) ...[
                Text(
                  key: const ValueKey('viewport_lod'),
                  lodSummary,
                  style: const TextStyle(
                    color: Color(0xFF94A3B8), // Slate 400
                    fontSize: 11,
                    fontFamily: 'monospace',
                  ),
                ),
                const SizedBox(height: 2),
              ],

              // 物理显卡信息
              ConstrainedBox(
                constraints: const BoxConstraints(maxWidth: 240),
                child: Text(
                  gpuName,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: const TextStyle(
                    color: Color(0xFF64748B), // Slate 500
                    fontSize: 10,
                  ),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
