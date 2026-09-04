import 'dart:math' as math;
import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import '../ffi/geoscatter3d_service.dart';
import '../models/point_cloud_model.dart';
import '../theme/app_theme.dart';

class CenterViewport extends StatefulWidget {
  final GeoScatter3dService service;

  const CenterViewport({
    super.key,
    required this.service,
  });

  @override
  State<CenterViewport> createState() => _CenterViewportState();
}

class _CenterViewportState extends State<CenterViewport> {
  bool _mapAxis = true;
  bool _worldAxis = false;
  bool _crosshair = true;

  double _cameraAzimuth = -45.0;
  double _cameraElevation = 30.0;
  double _zoom = 1.0;
  double _panX = 0.0;
  double _panY = 0.0;
  bool _isPanning = false;

  void _resetView() {
    setState(() {
      _cameraAzimuth = -45.0;
      _cameraElevation = 30.0;
      _zoom = 1.0;
      _panX = 0.0;
      _panY = 0.0;
    });
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        // 顶部视口 Tab 条
        Container(
          height: 34,
          padding: const EdgeInsets.symmetric(horizontal: 10),
          decoration: const BoxDecoration(
            color: AppTheme.surfaceMuted,
            border: Border(
              bottom: BorderSide(color: AppTheme.border, width: 1),
            ),
          ),
          child: Row(
            children: [
              Container(
                height: 26,
                padding: const EdgeInsets.symmetric(horizontal: 10),
                decoration: BoxDecoration(
                  color: AppTheme.surface,
                  borderRadius: const BorderRadius.vertical(top: Radius.circular(4)),
                  border: Border.all(color: AppTheme.border),
                ),
                child: Row(
                  children: const [
                    Text(
                      '主三维视口 (Vulkan/Canvas)',
                      style: TextStyle(
                        fontSize: 12.5,
                        fontWeight: FontWeight.w600,
                        color: AppTheme.textTitle,
                      ),
                    ),
                    SizedBox(width: 8),
                    Icon(Icons.close, size: 14, color: AppTheme.textDim),
                  ],
                ),
              ),
              const Spacer(),
              IconButton(
                icon: const Icon(Icons.refresh_rounded, size: 16, color: AppTheme.textDim),
                tooltip: '复位视口',
                onPressed: _resetView,
                splashRadius: 16,
              ),
              const SizedBox(width: 4),
              const Icon(Icons.fullscreen, size: 18, color: AppTheme.textDim),
            ],
          ),
        ),

        // 3D 渲染主画布与交互 HUD 叠层
        Expanded(
          child: ListenableBuilder(
            listenable: widget.service,
            builder: (context, _) {
              final points = widget.service.points;
              final pointSize = widget.service.pointSize;
              final colormap = widget.service.colormap;
              final scalarMin = widget.service.scalarMin;
              final scalarMax = widget.service.scalarMax;

              return Stack(
                children: [
                  // 3D 画布底色与手势探测器
                  Positioned.fill(
                    child: Listener(
                      onPointerSignal: (pointerSignal) {
                        if (pointerSignal is PointerScrollEvent) {
                          setState(() {
                            final factor = pointerSignal.scrollDelta.dy < 0 ? 1.15 : 0.87;
                            _zoom = (_zoom * factor).clamp(0.05, 50.0);
                          });
                        }
                      },
                      child: GestureDetector(
                        onPanUpdate: (details) {
                          setState(() {
                            if (_isPanning) {
                              _panX += details.delta.dx;
                              _panY += details.delta.dy;
                            } else {
                              _cameraAzimuth += details.delta.dx * 0.4;
                              _cameraElevation = (_cameraElevation - details.delta.dy * 0.4).clamp(-89.0, 89.0);
                            }
                          });
                        },
                        child: Container(
                          color: const Color(0xFF161A22),
                          child: CustomPaint(
                            painter: ViewportCanvasPainter(
                              points: points,
                              pointSize: pointSize,
                              colormap: colormap,
                              scalarMin: scalarMin,
                              scalarMax: scalarMax,
                              azimuth: _cameraAzimuth,
                              elevation: _cameraElevation,
                              zoom: _zoom,
                              panX: _panX,
                              panY: _panY,
                              showMapAxis: _mapAxis,
                              showCrosshair: _crosshair,
                              showWorldAxis: _worldAxis,
                            ),
                          ),
                        ),
                      ),
                    ),
                  ),

                  // 视口浮动快捷工具条 (Toolbar Overlay)
                  Positioned(
                    top: 10,
                    left: 14,
                    child: Row(
                      children: [
                        _buildPillButton('复位视角 R', _resetView),
                        const SizedBox(width: 6),
                        _buildPillToggleButton('平移模式', _isPanning, (v) => setState(() => _isPanning = v)),
                        const SizedBox(width: 6),
                        _buildPillToggleButton('地图轴', _mapAxis, (v) => setState(() => _mapAxis = v)),
                        const SizedBox(width: 6),
                        _buildPillToggleButton('世界轴', _worldAxis, (v) => setState(() => _worldAxis = v)),
                        const SizedBox(width: 6),
                        _buildPillToggleButton('十字准线', _crosshair, (v) => setState(() => _crosshair = v)),
                        const SizedBox(width: 8),
                        _buildIconPill(Icons.zoom_in, '放大', () {
                          setState(() => _zoom = (_zoom * 1.2).clamp(0.05, 50.0));
                        }),
                        const SizedBox(width: 4),
                        _buildIconPill(Icons.zoom_out, '缩小', () {
                          setState(() => _zoom = (_zoom / 1.2).clamp(0.05, 50.0));
                        }),
                      ],
                    ),
                  ),

                  // 左上角指标 HUD
                  Positioned(
                    top: 52,
                    left: 14,
                    child: Container(
                      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 5),
                      decoration: BoxDecoration(
                        color: Colors.black.withAlpha(180),
                        borderRadius: BorderRadius.circular(4),
                        border: Border.all(color: Colors.white12),
                      ),
                      child: Text(
                        '${points.length} 点 | 方位角 ${_cameraAzimuth.toStringAsFixed(1)}° | 仰角 ${_cameraElevation.toStringAsFixed(1)}° | 缩放 ${_zoom.toStringAsFixed(2)}x',
                        style: const TextStyle(
                          fontSize: 11.5,
                          fontFamily: 'Consolas',
                          color: Colors.white,
                        ),
                      ),
                    ),
                  ),

                  // 若没有载入数据，显示居中友好引导
                  if (points.isEmpty)
                    Center(
                      child: Container(
                        padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 16),
                        decoration: BoxDecoration(
                          color: const Color(0xFF1E222A).withAlpha(220),
                          borderRadius: BorderRadius.circular(8),
                          border: Border.all(color: AppTheme.border),
                          boxShadow: AppTheme.cardShadow,
                        ),
                        child: Column(
                          mainAxisSize: MainAxisSize.min,
                          children: [
                            const Icon(Icons.scatter_plot_rounded, size: 36, color: AppTheme.primaryBlue),
                            const SizedBox(height: 10),
                            const Text(
                              '暂未载入三维散点数据',
                              style: TextStyle(color: Colors.white, fontSize: 14, fontWeight: FontWeight.w600),
                            ),
                            const SizedBox(height: 6),
                            const Text(
                              '点击下方按钮立即载入内置地质样本数据',
                              style: TextStyle(color: Colors.white70, fontSize: 12),
                            ),
                            const SizedBox(height: 12),
                            ElevatedButton.icon(
                              style: ElevatedButton.styleFrom(
                                backgroundColor: AppTheme.primaryBlue,
                                foregroundColor: Colors.white,
                                padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
                              ),
                              icon: const Icon(Icons.file_open_rounded, size: 16),
                              label: const Text('载入 sample-points.gs3d.bundle'),
                              onPressed: () {
                                widget.service.loadDataset('data/sample-points.gs3d.bundle');
                              },
                            ),
                          ],
                        ),
                      ),
                    ),

                  // 左下角比例尺
                  Positioned(
                    bottom: 20,
                    left: 20,
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          '${(10.0 / _zoom).toStringAsFixed(1)} 米',
                          style: const TextStyle(fontSize: 11, color: Colors.white70),
                        ),
                        const SizedBox(height: 2),
                        Container(
                          width: 80,
                          height: 4,
                          decoration: const BoxDecoration(
                            border: Border(
                              bottom: BorderSide(color: Colors.white70, width: 1.5),
                              left: BorderSide(color: Colors.white70, width: 1.5),
                              right: BorderSide(color: Colors.white70, width: 1.5),
                            ),
                          ),
                        ),
                      ],
                    ),
                  ),

                  // 右下角三维导航球 Gizmo
                  Positioned(
                    bottom: 24,
                    right: 24,
                    child: Container(
                      width: 80,
                      height: 80,
                      decoration: BoxDecoration(
                        color: Colors.black.withAlpha(140),
                        shape: BoxShape.circle,
                        border: Border.all(color: Colors.white12),
                      ),
                      child: CustomPaint(
                        painter: NavigationBallGizmoPainter(
                          azimuth: _cameraAzimuth,
                          elevation: _cameraElevation,
                        ),
                      ),
                    ),
                  ),
                ],
              );
            },
          ),
        ),
      ],
    );
  }

  Widget _buildPillButton(String label, VoidCallback onPressed) {
    return InkWell(
      borderRadius: BorderRadius.circular(16),
      onTap: onPressed,
      child: Container(
        height: 28,
        padding: const EdgeInsets.symmetric(horizontal: 12),
        decoration: BoxDecoration(
          color: AppTheme.surface.withAlpha(235),
          borderRadius: BorderRadius.circular(14),
          border: Border.all(color: AppTheme.border),
          boxShadow: AppTheme.cardShadow,
        ),
        alignment: Alignment.center,
        child: Text(
          label,
          style: const TextStyle(
            fontSize: 12,
            fontWeight: FontWeight.w500,
            color: AppTheme.textBody,
          ),
        ),
      ),
    );
  }

  Widget _buildIconPill(IconData icon, String tooltip, VoidCallback onPressed) {
    return InkWell(
      borderRadius: BorderRadius.circular(14),
      onTap: onPressed,
      child: Container(
        width: 28,
        height: 28,
        decoration: BoxDecoration(
          color: AppTheme.surface.withAlpha(235),
          shape: BoxShape.circle,
          border: Border.all(color: AppTheme.border),
          boxShadow: AppTheme.cardShadow,
        ),
        alignment: Alignment.center,
        child: Icon(icon, size: 15, color: AppTheme.textBody),
      ),
    );
  }

  Widget _buildPillToggleButton(String label, bool active, ValueChanged<bool> onChanged) {
    return InkWell(
      borderRadius: BorderRadius.circular(16),
      onTap: () => onChanged(!active),
      child: Container(
        height: 28,
        padding: const EdgeInsets.symmetric(horizontal: 12),
        decoration: BoxDecoration(
          color: active ? AppTheme.primaryBlueBg : AppTheme.surface.withAlpha(235),
          borderRadius: BorderRadius.circular(14),
          border: Border.all(
            color: active ? AppTheme.primaryBlue : AppTheme.border,
            width: active ? 1.2 : 1.0,
          ),
          boxShadow: AppTheme.cardShadow,
        ),
        alignment: Alignment.center,
        child: Text(
          label,
          style: TextStyle(
            fontSize: 12,
            fontWeight: active ? FontWeight.w600 : FontWeight.w500,
            color: active ? AppTheme.primaryBlue : AppTheme.textBody,
          ),
        ),
      ),
    );
  }
}

/// 绘制视口 3D 点云与标尺网格
class ViewportCanvasPainter extends CustomPainter {
  final List<Point3D> points;
  final double pointSize;
  final String colormap;
  final double scalarMin;
  final double scalarMax;
  final double azimuth;
  final double elevation;
  final double zoom;
  final double panX;
  final double panY;
  final bool showMapAxis;
  final bool showCrosshair;
  final bool showWorldAxis;

  const ViewportCanvasPainter({
    required this.points,
    required this.pointSize,
    required this.colormap,
    required this.scalarMin,
    required this.scalarMax,
    required this.azimuth,
    required this.elevation,
    required this.zoom,
    required this.panX,
    required this.panY,
    required this.showMapAxis,
    required this.showCrosshair,
    required this.showWorldAxis,
  });

  @override
  void paint(Canvas canvas, Size size) {
    final cx = size.width * 0.5 + panX;
    final cy = size.height * 0.5 + panY;

    // 绘制坐标轴微弱网格
    if (showMapAxis) {
      final axisPaint = Paint()
        ..color = Colors.white.withAlpha(20)
        ..strokeWidth = 1.0;

      canvas.drawLine(Offset(0, cy), Offset(size.width, cy), axisPaint);
      canvas.drawLine(Offset(cx, 0), Offset(cx, size.height), axisPaint);

      // 同心网格圆
      for (double r = 60.0; r <= 240.0; r += 60.0) {
        final circlePaint = Paint()
          ..color = Colors.white.withAlpha(12)
          ..style = PaintingStyle.stroke
          ..strokeWidth = 1.0;
        canvas.drawCircle(Offset(cx, cy), r * zoom, circlePaint);
      }
    }

    if (points.isNotEmpty) {
      final radAz = azimuth * (math.pi / 180.0);
      final radEl = elevation * (math.pi / 180.0);
      final cosAz = math.cos(radAz);
      final sinAz = math.sin(radAz);
      final cosEl = math.cos(radEl);
      final sinEl = math.sin(radEl);

      // 计算点云包围盒
      double minX = points[0].x, maxX = points[0].x;
      double minY = points[0].y, maxY = points[0].y;
      double minZ = points[0].z, maxZ = points[0].z;
      for (final p in points) {
        if (p.x < minX) minX = p.x;
        if (p.x > maxX) maxX = p.x;
        if (p.y < minY) minY = p.y;
        if (p.y > maxY) maxY = p.y;
        if (p.z < minZ) minZ = p.z;
        if (p.z > maxZ) maxZ = p.z;
      }
      final midX = (minX + maxX) * 0.5;
      final midY = (minY + maxY) * 0.5;
      final midZ = (minZ + maxZ) * 0.5;
      final maxSpan = math.max(maxX - minX, math.max(maxY - minY, maxZ - minZ));
      final baseScale = (maxSpan > 1e-4) ? (math.min(size.width, size.height) * 0.65 / maxSpan) : 1.0;
      final totalScale = baseScale * zoom;

      final pointPaint = Paint()..style = PaintingStyle.fill;

      for (final p in points) {
        final rx = p.x - midX;
        final ry = p.y - midY;
        final rz = p.z - midZ;

        // 3D 旋转：先绕 Z 轴方位角旋转，再绕 X 轴仰角旋转
        final x1 = rx * cosAz - ry * sinAz;
        final y1 = rx * sinAz + ry * cosAz;
        final z1 = rz;

        final x2 = x1;
        final y2 = y1 * cosEl - z1 * sinEl;

        final px = cx + x2 * totalScale;
        final py = cy - y2 * totalScale;

        // 视口边界裁剪剔除
        if (px >= -20 && px <= size.width + 20 && py >= -20 && py <= size.height + 20) {
          pointPaint.color = p.toColor(colormap, scalarMin, scalarMax);
          canvas.drawCircle(Offset(px, py), pointSize, pointPaint);
        }
      }
    }

    // 十字准星
    if (showCrosshair) {
      final crosshairPaint = Paint()
        ..color = AppTheme.primaryBlue.withAlpha(140)
        ..strokeWidth = 1.2;
      canvas.drawLine(Offset(cx - 10, cy), Offset(cx + 10, cy), crosshairPaint);
      canvas.drawLine(Offset(cx, cy - 10), Offset(cx, cy + 10), crosshairPaint);
    }
  }

  @override
  bool shouldRepaint(covariant ViewportCanvasPainter oldDelegate) {
    return oldDelegate.azimuth != azimuth ||
        oldDelegate.elevation != elevation ||
        oldDelegate.zoom != zoom ||
        oldDelegate.panX != panX ||
        oldDelegate.panY != panY ||
        oldDelegate.showMapAxis != showMapAxis ||
        oldDelegate.showCrosshair != showCrosshair ||
        oldDelegate.showWorldAxis != showWorldAxis ||
        oldDelegate.pointSize != pointSize ||
        oldDelegate.colormap != colormap ||
        oldDelegate.points.length != points.length ||
        oldDelegate.scalarMin != scalarMin ||
        oldDelegate.scalarMax != scalarMax;
  }
}

/// 绘制导航球三维坐标轴指示器
class NavigationBallGizmoPainter extends CustomPainter {
  final double azimuth;
  final double elevation;

  const NavigationBallGizmoPainter({
    required this.azimuth,
    required this.elevation,
  });

  @override
  void paint(Canvas canvas, Size size) {
    final cx = size.width * 0.5;
    final cy = size.height * 0.5;

    final radAz = azimuth * (math.pi / 180.0);
    final radEl = elevation * (math.pi / 180.0);
    final cosAz = math.cos(radAz);
    final sinAz = math.sin(radAz);
    final cosEl = math.cos(radEl);
    final sinEl = math.sin(radEl);

    // 绘制随视角旋转的三维轴线
    void drawAxis3D(double x, double y, double z, Color color, String label) {
      final x1 = x * cosAz - y * sinAz;
      final y1 = x * sinAz + y * cosAz;
      final z1 = z;

      final x2 = x1;
      final y2 = y1 * cosEl - z1 * sinEl;

      const len = 25.0;
      final px = cx + x2 * len;
      final py = cy - y2 * len;

      final p = Paint()
        ..color = color
        ..strokeWidth = 2.0;

      canvas.drawLine(Offset(cx, cy), Offset(px, py), p);
      canvas.drawCircle(Offset(px, py), 5.5, Paint()..color = color);

      final textSpan = TextSpan(
        text: label,
        style: const TextStyle(color: Colors.white, fontSize: 8.5, fontWeight: FontWeight.bold),
      );
      final textPainter = TextPainter(
        text: textSpan,
        textDirection: TextDirection.ltr,
      )..layout();
      textPainter.paint(canvas, Offset(px - 3, py - 5));
    }

    // X 轴（红，+X）
    drawAxis3D(1.0, 0.0, 0.0, Colors.redAccent, 'X');
    // Y 轴（绿，+Y）
    drawAxis3D(0.0, 1.0, 0.0, Colors.greenAccent, 'Y');
    // Z 轴（蓝，+Z）
    drawAxis3D(0.0, 0.0, 1.0, Colors.blueAccent, 'Z');
  }

  @override
  bool shouldRepaint(covariant NavigationBallGizmoPainter oldDelegate) {
    return oldDelegate.azimuth != azimuth || oldDelegate.elevation != elevation;
  }
}
