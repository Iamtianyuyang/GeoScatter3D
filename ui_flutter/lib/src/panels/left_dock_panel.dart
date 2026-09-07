import 'dart:math' as math;
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import '../ffi/geoscatter3d_service.dart';
import '../models/dataset_model.dart';
import '../models/point_cloud_model.dart';
import '../theme/app_theme.dart';
import '../welcome/welcome_ui_keys.dart';
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
      key: WorkbenchUiKeys.leftDockPanel,
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
              tabKeys: const [
                WorkbenchUiKeys.leftDockTabProject,
                WorkbenchUiKeys.leftDockTabMeasure,
                WorkbenchUiKeys.leftDockTabStats,
              ],
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

              // 导航图 2D 鸟瞰图画布与视口双向同步
              ClipRRect(
                borderRadius: BorderRadius.circular(AppTheme.cardRadius),
                child: LayoutBuilder(
                  builder: (context, constraints) {
                    final mapW = constraints.maxWidth;
                    const mapH = 180.0;
                    final zoom = widget.service.cameraZoom;
                    final panX = widget.service.cameraPanX;
                    final panY = widget.service.cameraPanY;
                    final bmin = summary.bboxMin;
                    final bmax = summary.bboxMax;

                    final boxW = (mapW * 0.45 / zoom).clamp(24.0, mapW);
                    final boxH = (mapH * 0.45 / zoom).clamp(20.0, mapH);
                    final boxLeft = (mapW * 0.5 - panX * 0.15 - boxW * 0.5).clamp(0.0, mapW - boxW);
                    final boxTop = (mapH * 0.5 + panY * 0.15 - boxH * 0.5).clamp(0.0, mapH - boxH);

                    return GestureDetector(
                      onTapDown: (details) {
                        final nx = (details.localPosition.dx / mapW).clamp(0.0, 1.0);
                        final ny = (details.localPosition.dy / mapH).clamp(0.0, 1.0);
                        if (bmin.isNotEmpty && bmax.isNotEmpty) {
                          final wx = bmin[0] + nx * (bmax[0] - bmin[0]);
                          final wy = bmax[1] - ny * (bmax[1] - bmin[1]);
                          widget.service.navigateCameraToWorld(wx, wy);
                        }
                      },
                      child: Container(
                        height: mapH,
                        width: double.infinity,
                        color: const Color(0xFF0B1320),
                        child: Stack(
                          children: [
                            Positioned.fill(
                              child: CustomPaint(
                                painter: NavigationMapBackgroundPainter(
                                  points: widget.service.points,
                                  bmin: bmin,
                                  bmax: bmax,
                                ),
                              ),
                            ),
                            // 动态视锥体高亮框
                            Positioned(
                              left: boxLeft,
                              top: boxTop,
                              width: boxW,
                              height: boxH,
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
                    );
                  },
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
    final lines = widget.service.measurementLines;
    final isMeasuring = widget.service.isMeasurementMode;
    final mode = widget.service.measurementDisplayMode;

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
              Row(
                mainAxisAlignment: MainAxisAlignment.spaceBetween,
                children: [
                  const Text(
                    '三维空间测距与标尺',
                    style: TextStyle(
                      fontSize: 14,
                      fontWeight: FontWeight.w700,
                      color: AppTheme.textTitle,
                    ),
                  ),
                  Container(
                    padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
                    decoration: BoxDecoration(
                      color: isMeasuring ? const Color(0x3300E5FF) : AppTheme.surfaceMuted,
                      borderRadius: BorderRadius.circular(4),
                      border: Border.all(
                        color: isMeasuring ? const Color(0xFF00E5FF) : AppTheme.border,
                      ),
                    ),
                    child: Text(
                      isMeasuring ? '标尺拾取中' : '未激活',
                      style: TextStyle(
                        fontSize: 10,
                        fontWeight: FontWeight.w600,
                        color: isMeasuring ? const Color(0xFF00E5FF) : AppTheme.textDim,
                      ),
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 10),
              Row(
                children: [
                  Expanded(
                    child: _buildToolButton(
                      key: WorkbenchUiKeys.leftDockMeasureDistButton,
                      icon: isMeasuring ? Icons.check_circle_outline : Icons.straighten_rounded,
                      label: isMeasuring ? '退出测距模式' : '进入测距模式',
                      onPressed: () => widget.service.toggleMeasurementMode(),
                    ),
                  ),
                  const SizedBox(width: 6),
                  IconButton(
                    tooltip: '清空所有测量线',
                    icon: const Icon(Icons.delete_sweep_outlined, size: 18, color: AppTheme.textDim),
                    onPressed: lines.isNotEmpty ? () => widget.service.clearMeasurementLines() : null,
                  ),
                ],
              ),
              const SizedBox(height: 8),
              Row(
                children: [
                  const Text('标签显示: ', style: TextStyle(fontSize: 11, color: AppTheme.textDim)),
                  const SizedBox(width: 4),
                  Expanded(
                    child: Wrap(
                      spacing: 4,
                      runSpacing: 4,
                      children: [
                        _buildModeChip('3d', '3D 空间距', mode == '3d'),
                        _buildModeChip('2d', '2D 平面距', mode == '2d'),
                        _buildModeChip('both', '双重', mode == 'both'),
                      ],
                    ),
                  ),
                ],
              ),
            ],
          ),
        ),
        const SizedBox(height: 10),

        if (lines.isNotEmpty) ...[
          ModernCard(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  mainAxisAlignment: MainAxisAlignment.spaceBetween,
                  children: [
                    Text(
                      '已测线段 (${lines.length})',
                      style: const TextStyle(fontSize: 13, fontWeight: FontWeight.w700, color: AppTheme.textTitle),
                    ),
                    Text(
                      '累计长: ${lines.fold<double>(0, (sum, l) => sum + l.distance3D).toStringAsFixed(2)}m',
                      style: const TextStyle(fontSize: 11, color: AppTheme.primaryBlue, fontFamily: 'monospace'),
                    ),
                  ],
                ),
                const SizedBox(height: 8),
                ...lines.map((line) => Container(
                  margin: const EdgeInsets.only(bottom: 6),
                  padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
                  decoration: BoxDecoration(
                    color: AppTheme.surfaceMuted,
                    borderRadius: BorderRadius.circular(AppTheme.controlRadius),
                    border: Border.all(color: AppTheme.border.withAlpha(80)),
                  ),
                  child: Row(
                    children: [
                      Container(
                        width: 8,
                        height: 8,
                        decoration: BoxDecoration(
                          color: line.color,
                          shape: BoxShape.circle,
                        ),
                      ),
                      const SizedBox(width: 8),
                      Expanded(
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Text(
                              '3D: ${line.distance3D.toStringAsFixed(2)} m (ΔZ: ${line.deltaZ.toStringAsFixed(2)} m)',
                              style: const TextStyle(fontSize: 11.5, fontWeight: FontWeight.bold, color: AppTheme.textTitle, fontFamily: 'monospace'),
                            ),
                            const SizedBox(height: 2),
                            Text(
                              '(${line.start.x.toStringAsFixed(1)}, ${line.start.y.toStringAsFixed(1)}) → (${line.end.x.toStringAsFixed(1)}, ${line.end.y.toStringAsFixed(1)})',
                              style: const TextStyle(fontSize: 10, color: AppTheme.textDim, fontFamily: 'monospace'),
                            ),
                          ],
                        ),
                      ),
                      IconButton(
                        icon: Icon(
                          line.isFixed ? Icons.push_pin : Icons.push_pin_outlined,
                          size: 14,
                          color: line.isFixed ? const Color(0xFF00E5FF) : AppTheme.textDim,
                        ),
                        tooltip: line.isFixed ? '已固定' : '固定线段',
                        onPressed: () => widget.service.toggleMeasurementLineFixed(line.id),
                        splashRadius: 14,
                      ),
                      IconButton(
                        icon: const Icon(Icons.close, size: 14, color: AppTheme.textDim),
                        tooltip: '删除此测量线',
                        onPressed: () => widget.service.removeMeasurementLine(line.id),
                        splashRadius: 14,
                      ),
                    ],
                  ),
                )),
              ],
            ),
          ),
          const SizedBox(height: 10),
        ],

        ModernCard(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const Text(
                '全工区几何与包围盒',
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
                    _buildCoordRow('空间对角距', '${dist3d.toStringAsFixed(2)} m', isBold: true),
                    _buildCoordRow('底面投影面积', '${area2d.toStringAsFixed(2)} m²', isBold: true),
                  ],
                ),
              ),
              const SizedBox(height: 12),
              _buildToolButton(
                key: WorkbenchUiKeys.leftDockMeasureAreaButton,
                icon: Icons.square_foot_rounded,
                label: '计算投影底面积',
                onPressed: () => widget.service.calculateMeasurement('area'),
              ),
              const SizedBox(height: 6),
              _buildToolButton(
                key: WorkbenchUiKeys.leftDockMeasureStrikeDipButton,
                icon: Icons.rotate_90_degrees_cw,
                label: '地层产状分析 (走向/倾向/倾角)',
                onPressed: () => widget.service.calculateMeasurement('strike_dip'),
              ),
            ],
          ),
        ),
      ],
    );
  }

  Widget _buildModeChip(String mode, String label, bool active) {
    return InkWell(
      onTap: () => widget.service.setMeasurementDisplayMode(mode),
      borderRadius: BorderRadius.circular(3),
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
        decoration: BoxDecoration(
          color: active ? AppTheme.primaryBlue.withAlpha(40) : AppTheme.surfaceMuted,
          borderRadius: BorderRadius.circular(3),
          border: Border.all(color: active ? AppTheme.primaryBlue : AppTheme.border),
        ),
        child: Text(
          label,
          style: TextStyle(
            fontSize: 10,
            color: active ? AppTheme.primaryBlue : AppTheme.textDim,
            fontWeight: active ? FontWeight.bold : FontWeight.normal,
          ),
        ),
      ),
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
    final activeBoxStats = widget.service.activeRegionStats;
    final points = widget.service.points;
    final count = activeBoxStats != null ? (activeBoxStats['count'] as int) : points.length;

    double mean = 0.0;
    double minVal = widget.service.scalarMin;
    double maxVal = widget.service.scalarMax;
    double stdDev = 0.0;
    List<int> bins = [0, 0, 0, 0, 0];

    if (activeBoxStats != null) {
      mean = (activeBoxStats['mean'] as num).toDouble();
      minVal = (activeBoxStats['min'] as num).toDouble();
      maxVal = (activeBoxStats['max'] as num).toDouble();
      stdDev = (activeBoxStats['std_dev'] as num).toDouble();
      bins = (activeBoxStats['histogram'] as List).cast<int>();
    } else {
      if (count > 0) {
        double sum = 0.0;
        for (final p in points) {
          sum += p.value;
        }
        mean = sum / count;
      }
      if (count > 0 && maxVal > minVal) {
        final step = (maxVal - minVal) / 5.0;
        for (final p in points) {
          final idx = ((p.value - minVal) / step).floor().clamp(0, 4);
          bins[idx]++;
        }
      }
    }

    final maxBin = bins.reduce((a, b) => a > b ? a : b);

    return ListView(
      padding: const EdgeInsets.all(12),
      children: [
        if (activeBoxStats != null)
          Container(
            margin: const EdgeInsets.only(bottom: 10),
            padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
            decoration: BoxDecoration(
              color: const Color(0x333B82F6),
              borderRadius: BorderRadius.circular(6),
              border: Border.all(color: const Color(0xFF3B82F6)),
            ),
            child: Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                const Row(
                  children: [
                    Icon(Icons.crop_free, size: 14, color: Color(0xFF3B82F6)),
                    SizedBox(width: 6),
                    Text('当前展示: 视口矩形选区统计', style: TextStyle(fontSize: 11.5, fontWeight: FontWeight.bold, color: Color(0xFF93C5FD))),
                  ],
                ),
                InkWell(
                  onTap: () => widget.service.clearRegionStats(),
                  child: const Text('清除选区', style: TextStyle(fontSize: 11, color: Colors.white70, decoration: TextDecoration.underline)),
                ),
              ],
            ),
          )
        else
          Container(
            margin: const EdgeInsets.only(bottom: 10),
            padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
            decoration: BoxDecoration(
              color: AppTheme.surfaceMuted,
              borderRadius: BorderRadius.circular(6),
              border: Border.all(color: AppTheme.border),
            ),
            child: const Row(
              children: [
                Icon(Icons.info_outline, size: 14, color: AppTheme.textDim),
                SizedBox(width: 6),
                Expanded(
                  child: Text(
                    '提示：在三维视口中按住 Shift 并拖拽鼠标即可框选任意局部区域进行即时统计。',
                    style: TextStyle(fontSize: 11, color: AppTheme.textDim),
                  ),
                ),
              ],
            ),
          ),

        ModernCard(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                activeBoxStats != null ? '局部选区统计指标' : '全工区属性统计指标',
                style: const TextStyle(
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
                    if (activeBoxStats != null)
                      _buildCoordRow('标准差 StdDev', stdDev.toStringAsFixed(3)),
                  ],
                ),
              ),
              const SizedBox(height: 14),
              const Text('数值直方图分布 (5 Bins)', style: TextStyle(fontSize: 13, fontWeight: FontWeight.w600, color: AppTheme.textTitle)),
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
                                color: (activeBoxStats != null ? const Color(0xFF3B82F6) : AppTheme.primaryBlue)
                                    .withAlpha((100 + i * 35).clamp(0, 255)),
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
              Row(
                children: [
                  Expanded(
                    child: _buildToolButton(
                      key: WorkbenchUiKeys.leftDockStatsRecalculateButton,
                      icon: Icons.copy_all_rounded,
                      label: '复制统计指标数据',
                      onPressed: () {
                        final text = '样本数: $count\n均值: ${mean.toStringAsFixed(3)}\n最小值: ${minVal.toStringAsFixed(3)}\n最大值: ${maxVal.toStringAsFixed(3)}\n极差: ${(maxVal - minVal).toStringAsFixed(3)}\n直方图: $bins';
                        Clipboard.setData(ClipboardData(text: text));
                        ScaffoldMessenger.of(context).showSnackBar(
                          const SnackBar(content: Text('已复制统计指标数据到剪贴板'), duration: Duration(seconds: 2)),
                        );
                      },
                    ),
                  ),
                ],
              ),
            ],
          ),
        ),
      ],
    );
  }

  Widget _buildToolButton({
    Key? key,
    required IconData icon,
    required String label,
    required VoidCallback onPressed,
  }) {
    return OutlinedButton.icon(
      key: key,
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
      onPressed: onPressed,
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
  final List<Point3D>? points;
  final List<double>? bmin;
  final List<double>? bmax;

  const NavigationMapBackgroundPainter({
    this.points,
    this.bmin,
    this.bmax,
  });

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

    if (points != null && points!.isNotEmpty && bmin != null && bmax != null && bmin!.isNotEmpty && bmax!.isNotEmpty) {
      final spanX = (bmax![0] - bmin![0]) > 1e-4 ? (bmax![0] - bmin![0]) : 1.0;
      final spanY = (bmax!.length > 1 && bmin!.length > 1 && (bmax![1] - bmin![1]) > 1e-4) ? (bmax![1] - bmin![1]) : 1.0;
      final step = math.max(1, (points!.length / 200).floor());
      final pPaint = Paint()..color = Colors.white.withAlpha(120);
      for (int i = 0; i < points!.length; i += step) {
        final p = points![i];
        final nx = ((p.x - bmin![0]) / spanX).clamp(0.0, 1.0);
        final ny = ((p.y - bmin![1]) / spanY).clamp(0.0, 1.0);
        final px = nx * size.width;
        final py = (1.0 - ny) * size.height;
        canvas.drawCircle(Offset(px, py), 1.2, pPaint);
      }
    }
  }

  @override
  bool shouldRepaint(covariant NavigationMapBackgroundPainter oldDelegate) =>
      oldDelegate.points?.length != points?.length;
}
