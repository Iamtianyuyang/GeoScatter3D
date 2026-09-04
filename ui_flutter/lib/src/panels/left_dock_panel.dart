import 'dart:math' as math;
import 'package:flutter/material.dart';
import '../ffi/geoscatter3d_service.dart';
import '../models/dataset_model.dart';
import '../theme/app_theme.dart';
import '../widgets/modern_card.dart';
import '../widgets/segmented_tabs.dart';

class LeftDockPanel extends StatefulWidget {
  final GeoScatter3dService service;

  const LeftDockPanel({
    super.key,
    required this.service,
  });

  @override
  State<LeftDockPanel> createState() => _LeftDockPanelState();
}

class _LeftDockPanelState extends State<LeftDockPanel> {
  int _currentTab = 0;

  @override
  Widget build(BuildContext context) {
    return Container(
      width: 320,
      decoration: const BoxDecoration(
        color: AppTheme.background,
        border: Border(
          right: BorderSide(color: AppTheme.border, width: 1),
        ),
      ),
      child: Column(
        children: [
          // 1. 顶部分段切换器 [ 项目 | 测量 | 区域统计 ]
          Padding(
            padding: const EdgeInsets.fromLTRB(12, 12, 12, 8),
            child: ModernSegmentedTabs(
              tabs: const ['项目', '测量', '区域统计'],
              selectedIndex: _currentTab,
              onTabSelected: (index) {
                setState(() {
                  _currentTab = index;
                });
              },
            ),
          ),

          // 2. 内容区
          Expanded(
            child: ListenableBuilder(
              listenable: widget.service,
              builder: (context, _) {
                if (_currentTab == 1) {
                  return _buildMeasurementTab();
                } else if (_currentTab == 2) {
                  return _buildRegionStatsTab();
                }
                return _buildDatasetCardStream(widget.service.summary);
              },
            ),
          ),
        ],
      ),
    );
  }

  /// 项目卡片流 (完全还原 media_1788518817411.png 核心视觉)
  Widget _buildDatasetCardStream(DatasetSummary summary) {
    return ListView(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
      children: [
        // -------------------------------------------------------------
        // Card 1：数据集概要卡片 (Dataset Summary Card)
        // -------------------------------------------------------------
        ModernCard(
          padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 14),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                summary.name,
                style: const TextStyle(
                  fontSize: 16,
                  fontWeight: FontWeight.w700,
                  color: AppTheme.textTitle,
                  letterSpacing: -0.2,
                ),
              ),
              const SizedBox(height: 6),
              Row(
                children: [
                  Text(
                    summary.formattedPointCount,
                    style: const TextStyle(
                      fontSize: 12.5,
                      fontWeight: FontWeight.w500,
                      color: AppTheme.textDim,
                    ),
                  ),
                  Padding(
                    padding: const EdgeInsets.symmetric(horizontal: 8),
                    child: Text(
                      '|',
                      style: TextStyle(
                        fontSize: 12,
                        color: AppTheme.borderSubtle,
                      ),
                    ),
                  ),
                  Text(
                    summary.fileSize,
                    style: const TextStyle(
                      fontSize: 12.5,
                      fontWeight: FontWeight.w500,
                      color: AppTheme.textDim,
                    ),
                  ),
                ],
              ),
            ],
          ),
        ),
        const SizedBox(height: 12),

        // -------------------------------------------------------------
        // 章节标题：场景 (Scene Header)
        // -------------------------------------------------------------
        const Padding(
          padding: EdgeInsets.only(left: 2, bottom: 8),
          child: Text(
            '场景',
            style: TextStyle(
              fontSize: 13.5,
              fontWeight: FontWeight.w700,
              color: AppTheme.textTitle,
            ),
          ),
        ),

        // -------------------------------------------------------------
        // Card 2：细节层级卡片 (LOD Card，可折叠)
        // -------------------------------------------------------------
        CollapsibleCard(
          title: '细节层级',
          initialOpen: true,
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                summary.lodEnabled ? '已启用细节层级' : '未启用细节层级',
                style: const TextStyle(
                  fontSize: 13,
                  fontWeight: FontWeight.w500,
                  color: AppTheme.textBody,
                ),
              ),
              const SizedBox(height: 3),
              Text(
                summary.lodEnabled
                    ? '状态: 已启用（${summary.lodDetails.length} 层）'
                    : '状态: 未启用',
                style: const TextStyle(
                  fontSize: 12,
                  color: AppTheme.textDim,
                ),
              ),
              if (summary.lodEnabled && summary.lodDetails.isNotEmpty) ...[
                const SizedBox(height: 6),
                ...summary.lodDetails.map((detail) => Padding(
                      padding: const EdgeInsets.only(bottom: 3),
                      child: Text(
                        detail,
                        style: const TextStyle(
                          fontSize: 12,
                          fontFamily: 'Consolas',
                          color: AppTheme.textBody,
                        ),
                      ),
                    )),
              ],
            ],
          ),
        ),
        const SizedBox(height: 10),

        // -------------------------------------------------------------
        // Card 3：属性卡片 (Attributes Card，可折叠)
        // -------------------------------------------------------------
        CollapsibleCard(
          title: '属性',
          initialOpen: true,
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                '共 ${summary.attributes.length} 个属性',
                style: const TextStyle(
                  fontSize: 12.5,
                  fontWeight: FontWeight.w500,
                  color: AppTheme.textDim,
                ),
              ),
              const SizedBox(height: 8),
              ...summary.attributes.map((attr) => Padding(
                    padding: const EdgeInsets.only(bottom: 6),
                    child: Row(
                      children: [
                        Container(
                          width: 6,
                          height: 6,
                          margin: const EdgeInsets.only(left: 4, right: 10),
                          decoration: const BoxDecoration(
                            color: AppTheme.primaryBlue,
                            shape: BoxShape.circle,
                          ),
                        ),
                        Text(
                          attr,
                          style: const TextStyle(
                            fontSize: 13,
                            fontWeight: FontWeight.w500,
                            color: AppTheme.textBody,
                          ),
                        ),
                      ],
                    ),
                  )),
            ],
          ),
        ),
        const SizedBox(height: 10),

        // -------------------------------------------------------------
        // Card 4：导航图卡片 (Navigation Map Card)
        // -------------------------------------------------------------
        ModernCard(
          padding: const EdgeInsets.all(12),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                mainAxisAlignment: MainAxisAlignment.spaceBetween,
                children: [
                  const Text(
                    '导航图',
                    style: TextStyle(
                      fontSize: 14,
                      fontWeight: FontWeight.w700,
                      color: AppTheme.textTitle,
                    ),
                  ),
                  InkWell(
                    borderRadius: BorderRadius.circular(4),
                    onTap: () {
                      ScaffoldMessenger.of(context).showSnackBar(
                        const SnackBar(
                          content: Text('导航图视口已最大化'),
                          duration: Duration(milliseconds: 800),
                        ),
                      );
                    },
                    child: Padding(
                      padding: const EdgeInsets.all(3.0),
                      child: CustomPaint(
                        size: const Size(14, 14),
                        painter: ViewfinderCornerPainter(color: AppTheme.textDim),
                      ),
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 10),

              // 导航图 2D 鸟瞰图画布
              ClipRRect(
                borderRadius: BorderRadius.circular(AppTheme.cardRadius),
                child: Container(
                  height: 180,
                  width: double.infinity,
                  color: const Color(0xFF0B1320),
                  child: Stack(
                    children: [
                      // 鸟瞰地形图渐变模拟与网格
                      Positioned.fill(
                        child: CustomPaint(
                          painter: NavigationMapBackgroundPainter(),
                        ),
                      ),
                      // 视锥体高亮框
                      Positioned(
                        left: 40,
                        top: 50,
                        width: 90,
                        height: 70,
                        child: Container(
                          decoration: BoxDecoration(
                            color: AppTheme.primaryBlue.withAlpha(45),
                            border: Border.all(
                              color: AppTheme.primaryBlue,
                              width: 1.5,
                            ),
                            borderRadius: BorderRadius.circular(2),
                          ),
                        ),
                      ),
                    ],
                  ),
                ),
              ),
            ],
          ),
        ),
        const SizedBox(height: 20),
      ],
    );
  }

  Widget _buildMeasurementTab() {
    final summary = widget.service.summary;
    final bmin = summary.bboxMin;
    final bmax = summary.bboxMax;

    final dx = bmax.isNotEmpty && bmin.isNotEmpty ? (bmax[0] - bmin[0]).abs() : 0.0;
    final dy = bmax.length > 1 && bmin.length > 1 ? (bmax[1] - bmin[1]).abs() : 0.0;
    final dz = bmax.length > 2 && bmin.length > 2 ? (bmax[2] - bmin[2]).abs() : 0.0;
    final dist3d = math.sqrt(dx * dx + dy * dy + dz * dz);
    final area2d = dx * dy;

    return ListView(
      padding: const EdgeInsets.all(12),
      children: [
        ModernCard(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const Text(
                '三维空间测距',
                style: TextStyle(
                  fontSize: 14,
                  fontWeight: FontWeight.w700,
                  color: AppTheme.textTitle,
                ),
              ),
              const SizedBox(height: 10),
              Container(
                padding: const EdgeInsets.all(10),
                decoration: BoxDecoration(
                  color: AppTheme.surfaceMuted,
                  borderRadius: BorderRadius.circular(AppTheme.controlRadius),
                ),
                child: Column(
                  children: [
                    _buildCoordRow('起点 A', bmin.isNotEmpty ? '(${bmin[0].toStringAsFixed(1)}, ${bmin[1].toStringAsFixed(1)}, ${bmin[2].toStringAsFixed(1)})' : '--'),
                    const SizedBox(height: 6),
                    _buildCoordRow('终点 B', bmax.isNotEmpty ? '(${bmax[0].toStringAsFixed(1)}, ${bmax[1].toStringAsFixed(1)}, ${bmax[2].toStringAsFixed(1)})' : '--'),
                    const Divider(height: 16),
                    _buildCoordRow('空间对角距', '${dist3d.toStringAsFixed(2)} m', isBold: true),
                    _buildCoordRow('水平投影距', '${dx.toStringAsFixed(2)} m'),
                    _buildCoordRow('绝对高差 ΔZ', '${dz.toStringAsFixed(2)} m'),
                  ],
                ),
              ),
              const SizedBox(height: 12),
              _buildToolButton(Icons.straighten_rounded, '重新拾取测量两点'),
            ],
          ),
        ),
        const SizedBox(height: 10),

        ModernCard(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const Text(
                '几何与投影面积',
                style: TextStyle(
                  fontSize: 14,
                  fontWeight: FontWeight.w700,
                  color: AppTheme.textTitle,
                ),
              ),
              const SizedBox(height: 10),
              Container(
                padding: const EdgeInsets.all(10),
                decoration: BoxDecoration(
                  color: AppTheme.surfaceMuted,
                  borderRadius: BorderRadius.circular(AppTheme.controlRadius),
                ),
                child: Column(
                  children: [
                    _buildCoordRow('X 跨度', '${dx.toStringAsFixed(2)} m'),
                    _buildCoordRow('Y 跨度', '${dy.toStringAsFixed(2)} m'),
                    _buildCoordRow('Z 跨度', '${dz.toStringAsFixed(2)} m'),
                    const Divider(height: 16),
                    _buildCoordRow('底面投影面积', '${area2d.toStringAsFixed(2)} m²', isBold: true),
                  ],
                ),
              ),
              const SizedBox(height: 12),
              _buildToolButton(Icons.square_foot_rounded, '绘制多边形测面'),
              const SizedBox(height: 6),
              _buildToolButton(Icons.rotate_90_degrees_cw, '地层产状分析 (走向/倾向/倾角)'),
            ],
          ),
        ),
      ],
    );
  }

  Widget _buildCoordRow(String label, String value, {bool isBold = false}) {
    return Row(
      mainAxisAlignment: MainAxisAlignment.spaceBetween,
      children: [
        Text(label, style: TextStyle(fontSize: 12, color: isBold ? AppTheme.textTitle : AppTheme.textDim, fontWeight: isBold ? FontWeight.w600 : FontWeight.normal)),
        Text(value, style: TextStyle(fontSize: 12, color: isBold ? AppTheme.primaryBlue : AppTheme.textBody, fontWeight: isBold ? FontWeight.w700 : FontWeight.w500, fontFamily: 'Consolas')),
      ],
    );
  }

  Widget _buildRegionStatsTab() {
    final points = widget.service.points;
    final count = points.length;

    double mean = 0.0;
    if (count > 0) {
      double sum = 0.0;
      for (final p in points) {
        sum += p.value;
      }
      mean = sum / count;
    }

    final minVal = widget.service.scalarMin;
    final maxVal = widget.service.scalarMax;

    // 计算 5 个区间的直方图分布
    final bins = [0, 0, 0, 0, 0];
    if (count > 0 && maxVal > minVal) {
      final step = (maxVal - minVal) / 5.0;
      for (final p in points) {
        final idx = ((p.value - minVal) / step).floor().clamp(0, 4);
        bins[idx]++;
      }
    }

    final maxBin = bins.reduce((a, b) => a > b ? a : b);

    return ListView(
      padding: const EdgeInsets.all(12),
      children: [
        ModernCard(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const Text(
                '属性统计指标 (Statistics)',
                style: TextStyle(
                  fontSize: 14,
                  fontWeight: FontWeight.w700,
                  color: AppTheme.textTitle,
                ),
              ),
              const SizedBox(height: 10),
              Container(
                padding: const EdgeInsets.all(10),
                decoration: BoxDecoration(
                  color: AppTheme.surfaceMuted,
                  borderRadius: BorderRadius.circular(AppTheme.controlRadius),
                ),
                child: Column(
                  children: [
                    _buildCoordRow('有效样本数 N', '$count 点'),
                    _buildCoordRow('标量均值 Mean', mean.toStringAsFixed(3)),
                    _buildCoordRow('最小值 Min', minVal.toStringAsFixed(3)),
                    _buildCoordRow('最大值 Max', maxVal.toStringAsFixed(3)),
                    _buildCoordRow('极差 Range', (maxVal - minVal).toStringAsFixed(3)),
                  ],
                ),
              ),
              const SizedBox(height: 14),
              const Text('数值直方图分布', style: TextStyle(fontSize: 13, fontWeight: FontWeight.w600, color: AppTheme.textTitle)),
              const SizedBox(height: 8),

              // 直方图 5 根柱子
              SizedBox(
                height: 80,
                child: Row(
                  crossAxisAlignment: CrossAxisAlignment.end,
                  children: List.generate(5, (i) {
                    final fraction = maxBin > 0 ? (bins[i] / maxBin) : 0.1;
                    return Expanded(
                      child: Padding(
                        padding: const EdgeInsets.symmetric(horizontal: 3),
                        child: Column(
                          mainAxisAlignment: MainAxisAlignment.end,
                          children: [
                            Text('${bins[i]}', style: const TextStyle(fontSize: 9.5, color: AppTheme.textDim)),
                            const SizedBox(height: 2),
                            Container(
                              height: (60 * fraction).clamp(4.0, 60.0),
                              decoration: BoxDecoration(
                                color: AppTheme.primaryBlue.withAlpha((100 + i * 35).clamp(0, 255)),
                                borderRadius: const BorderRadius.vertical(top: Radius.circular(3)),
                              ),
                            ),
                          ],
                        ),
                      ),
                    );
                  }),
                ),
              ),
              const SizedBox(height: 6),
              Row(
                mainAxisAlignment: MainAxisAlignment.spaceBetween,
                children: [
                  Text(minVal.toStringAsFixed(1), style: const TextStyle(fontSize: 10, color: AppTheme.textDim)),
                  Text(((minVal + maxVal) * 0.5).toStringAsFixed(1), style: const TextStyle(fontSize: 10, color: AppTheme.textDim)),
                  Text(maxVal.toStringAsFixed(1), style: const TextStyle(fontSize: 10, color: AppTheme.textDim)),
                ],
              ),
              const SizedBox(height: 12),
              _buildToolButton(Icons.highlight_alt_rounded, '框选局部区域重算统计'),
            ],
          ),
        ),
      ],
    );
  }

  Widget _buildToolButton(IconData icon, String label) {
    return OutlinedButton.icon(
      style: OutlinedButton.styleFrom(
        alignment: Alignment.centerLeft,
        side: const BorderSide(color: AppTheme.border),
        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(AppTheme.controlRadius),
        ),
      ),
      icon: Icon(icon, size: 16, color: AppTheme.primaryBlue),
      label: Text(
        label,
        style: const TextStyle(
          fontSize: 12.5,
          color: AppTheme.textBody,
          fontWeight: FontWeight.w500,
        ),
      ),
      onPressed: () {},
    );
  }
}

/// 绘制右上角 [ ] 取景框四角图标
class ViewfinderCornerPainter extends CustomPainter {
  final Color color;
  const ViewfinderCornerPainter({required this.color});

  @override
  void paint(Canvas canvas, Size size) {
    final paint = Paint()
      ..color = color
      ..strokeWidth = 1.3
      ..style = PaintingStyle.stroke;

    const arm = 3.5;
    // ┌
    canvas.drawLine(const Offset(0, arm), const Offset(0, 0), paint);
    canvas.drawLine(const Offset(0, 0), const Offset(arm, 0), paint);

    // ┐
    canvas.drawLine(Offset(size.width - arm, 0), Offset(size.width, 0), paint);
    canvas.drawLine(Offset(size.width, 0), Offset(size.width, arm), paint);

    // └
    canvas.drawLine(Offset(0, size.height - arm), Offset(0, size.height), paint);
    canvas.drawLine(Offset(0, size.height), Offset(arm, size.height), paint);

    // ┘
    canvas.drawLine(Offset(size.width - arm, size.height), Offset(size.width, size.height), paint);
    canvas.drawLine(Offset(size.width, size.height - arm), Offset(size.width, size.height), paint);
  }

  @override
  bool shouldRepaint(covariant CustomPainter oldDelegate) => false;
}

/// 绘制鸟瞰导航图彩色地貌与热力背景
class NavigationMapBackgroundPainter extends CustomPainter {
  @override
  void paint(Canvas canvas, Size size) {
    final rect = Offset.zero & size;
    final gradient = RadialGradient(
      center: const Alignment(-0.2, -0.1),
      radius: 0.9,
      colors: const [
        Color(0xFF00E5FF),
        Color(0xFF00E676),
        Color(0xFFFFEA00),
        Color(0xFFFF3D00),
        Color(0xFF311B92),
      ],
      stops: const [0.0, 0.3, 0.55, 0.8, 1.0],
    );

    final paint = Paint()..shader = gradient.createShader(rect);
    canvas.drawRect(rect, paint);

    // 绘制等高线微弱网格
    final gridPaint = Paint()
      ..color = Colors.white.withAlpha(25)
      ..strokeWidth = 0.5;

    for (double x = 0; x < size.width; x += 24) {
      canvas.drawLine(Offset(x, 0), Offset(x, size.height), gridPaint);
    }
    for (double y = 0; y < size.height; y += 24) {
      canvas.drawLine(Offset(0, y), Offset(size.width, y), gridPaint);
    }
  }

  @override
  bool shouldRepaint(covariant CustomPainter oldDelegate) => false;
}
