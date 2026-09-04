import 'package:flutter/material.dart';
import '../ffi/geoscatter3d_service.dart';
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

  double _cameraAzimuth = 0.0;
  double _cameraElevation = 0.0;

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
                      '视图 1',
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
              const Icon(Icons.fullscreen, size: 18, color: AppTheme.textDim),
            ],
          ),
        ),

        // 3D 渲染主画布与交互 HUD 叠层
        Expanded(
          child: Stack(
            children: [
              // 3D 画布底色与手势探测器
              Positioned.fill(
                child: GestureDetector(
                  onPanUpdate: (details) {
                    setState(() {
                      _cameraAzimuth += details.delta.dx * 0.5;
                      _cameraElevation += details.delta.dy * 0.5;
                    });
                  },
                  child: Container(
                    color: const Color(0xFF1E222A),
                    child: CustomPaint(
                      painter: ViewportCanvasPainter(
                        azimuth: _cameraAzimuth,
                        elevation: _cameraElevation,
                        showMapAxis: _mapAxis,
                        showCrosshair: _crosshair,
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
                    _buildPillButton('复位视角 R', () {
                      setState(() {
                        _cameraAzimuth = 0;
                        _cameraElevation = 0;
                      });
                    }),
                    const SizedBox(width: 6),
                    _buildPillButton('联动相机', () {}),
                    const SizedBox(width: 6),
                    _buildPillToggleButton('地图轴', _mapAxis, (v) => setState(() => _mapAxis = v)),
                    const SizedBox(width: 6),
                    _buildPillToggleButton('世界轴', _worldAxis, (v) => setState(() => _worldAxis = v)),
                    const SizedBox(width: 6),
                    _buildPillToggleButton('十字准线', _crosshair, (v) => setState(() => _crosshair = v)),
                  ],
                ),
              ),

              // 左上角指标 HUD
              Positioned(
                top: 56,
                left: 14,
                child: Container(
                  padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 5),
                  decoration: BoxDecoration(
                    color: Colors.black.withAlpha(160),
                    borderRadius: BorderRadius.circular(4),
                  ),
                  child: ListenableBuilder(
                    listenable: widget.service,
                    builder: (context, _) {
                      final count = widget.service.summary.pointCount;
                      return Text(
                        '$count 点 | 0.52 ms',
                        style: const TextStyle(
                          fontSize: 12,
                          fontFamily: 'Consolas',
                          color: Colors.white,
                        ),
                      );
                    },
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
                    const Text('10 米', style: TextStyle(fontSize: 11, color: Colors.white70)),
                    const SizedBox(height: 2),
                    Container(
                      width: 90,
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
                  width: 76,
                  height: 76,
                  decoration: BoxDecoration(
                    color: Colors.black.withAlpha(110),
                    shape: BoxShape.circle,
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
  final double azimuth;
  final double elevation;
  final bool showMapAxis;
  final bool showCrosshair;

  const ViewportCanvasPainter({
    required this.azimuth,
    required this.elevation,
    required this.showMapAxis,
    required this.showCrosshair,
  });

  @override
  void paint(Canvas canvas, Size size) {
    final cx = size.width * 0.5;
    final cy = size.height * 0.5;

    // 绘制坐标轴微弱网格
    if (showMapAxis) {
      final axisPaint = Paint()
        ..color = Colors.white.withAlpha(25)
        ..strokeWidth = 1.0;

      canvas.drawLine(Offset(0, cy), Offset(size.width, cy), axisPaint);
      canvas.drawLine(Offset(cx, 0), Offset(cx, size.height), axisPaint);
    }

    // 绘制点云模拟粒子
    final pointPaint = Paint()..strokeWidth = 2.0;
    const cols = [Colors.blueAccent, Colors.cyanAccent, Colors.greenAccent, Colors.orangeAccent, Colors.redAccent];

    for (int i = 0; i < 25; i++) {
      final col = cols[i % cols.length];
      pointPaint.color = col;
      final px = cx - 120 + (i % 5) * 60 + azimuth * 0.2;
      final py = cy - 120 + (i ~/ 5) * 60 + elevation * 0.2;
      canvas.drawCircle(Offset(px, py), 2.0, pointPaint);
    }

    // 十字准星
    if (showCrosshair) {
      final crosshairPaint = Paint()
        ..color = AppTheme.primaryBlue.withAlpha(120)
        ..strokeWidth = 1.0;
      canvas.drawLine(Offset(cx - 10, cy), Offset(cx + 10, cy), crosshairPaint);
      canvas.drawLine(Offset(cx, cy - 10), Offset(cx, cy + 10), crosshairPaint);
    }
  }

  @override
  bool shouldRepaint(covariant ViewportCanvasPainter oldDelegate) {
    return oldDelegate.azimuth != azimuth ||
        oldDelegate.elevation != elevation ||
        oldDelegate.showMapAxis != showMapAxis ||
        oldDelegate.showCrosshair != showCrosshair;
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

    // X 轴（红）
    _drawAxis(canvas, cx, cy, 22, 0, Colors.redAccent, 'X');
    // Y 轴（绿）
    _drawAxis(canvas, cx, cy, 0, -22, Colors.greenAccent, 'Y');
    // Z 轴（蓝）
    _drawAxis(canvas, cx, cy, 0, 0, Colors.blueAccent, 'Z');
  }

  void _drawAxis(Canvas canvas, double cx, double cy, double dx, double dy, Color color, String label) {
    final p = Paint()
      ..color = color
      ..strokeWidth = 2.0;

    if (dx != 0 || dy != 0) {
      canvas.drawLine(Offset(cx, cy), Offset(cx + dx, cy + dy), p);
    }
    canvas.drawCircle(Offset(cx + dx, cy + dy), 6.5, Paint()..color = color);

    final textSpan = TextSpan(
      text: label,
      style: const TextStyle(color: Colors.white, fontSize: 8.5, fontWeight: FontWeight.bold),
    );
    final textPainter = TextPainter(
      text: textSpan,
      textDirection: TextDirection.ltr,
    )..layout();
    textPainter.paint(canvas, Offset(cx + dx - 3, cy + dy - 5));
  }

  @override
  bool shouldRepaint(covariant NavigationBallGizmoPainter oldDelegate) => true;
}
