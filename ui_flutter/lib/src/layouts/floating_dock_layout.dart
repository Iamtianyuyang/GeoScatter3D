import 'dart:ui';
import 'package:flutter/material.dart';
import '../ffi/geoscatter3d_service.dart';
import '../panels/center_viewport.dart';
import '../theme/app_theme.dart';

enum FloatingDockPanelType {
  dataset,
  measurement,
  stats,
  rendering;

  String get title {
    switch (this) {
      case FloatingDockPanelType.dataset:
        return '数据集与工区';
      case FloatingDockPanelType.measurement:
        return '几何空间测量';
      case FloatingDockPanelType.stats:
        return '区域统计分布';
      case FloatingDockPanelType.rendering:
        return '渲染与着色外观';
    }
  }

  IconData get icon {
    switch (this) {
      case FloatingDockPanelType.dataset:
        return Icons.folder_open_rounded;
      case FloatingDockPanelType.measurement:
        return Icons.straighten_rounded;
      case FloatingDockPanelType.stats:
        return Icons.bar_chart_rounded;
      case FloatingDockPanelType.rendering:
        return Icons.palette_outlined;
    }
  }
}

/// 方案 B · 悬浮胶囊 Dock 布局 (Floating Dock Layout)
/// 全沉浸视口 + 底部悬浮胶囊 Dock 工具栏 + 弹出式半透明悬浮卡片
class FloatingDockLayout extends StatefulWidget {
  final GeoScatter3dService service;

  const FloatingDockLayout({
    super.key,
    required this.service,
  });

  @override
  State<FloatingDockLayout> createState() => _FloatingDockLayoutState();
}

class _FloatingDockLayoutState extends State<FloatingDockLayout> {
  FloatingDockPanelType? _activePanel;

  void _togglePanel(FloatingDockPanelType type) {
    setState(() {
      if (_activePanel == type) {
        _activePanel = null;
      } else {
        _activePanel = type;
      }
    });
  }

  @override
  Widget build(BuildContext context) {
    return Stack(
      children: [
        // 1. 底层：充满全屏的真 3D 渲染主视口
        Positioned.fill(
          child: CenterViewport(service: widget.service),
        ),

        // 2. 弹出的悬浮半透明卡片
        if (_activePanel != null)
          Positioned(
            left: 24,
            bottom: 84,
            width: 320,
            child: _buildFloatingCard(_activePanel!),
          ),

        // 3. 底部悬浮胶囊 Dock
        Positioned(
          left: 0,
          right: 0,
          bottom: 18,
          child: Center(
            child: _buildCapsuleDock(),
          ),
        ),

        // 4. 左下角沉浸式微型状态 HUD
        Positioned(
          left: 20,
          bottom: 24,
          child: _buildMiniHud(),
        ),
      ],
    );
  }

  Widget _buildCapsuleDock() {
    return ClipRRect(
      borderRadius: BorderRadius.circular(32),
      child: BackdropFilter(
        filter: ImageFilter.blur(sigmaX: 16, sigmaY: 16),
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 6),
          decoration: BoxDecoration(
            color: const Color(0xE0181F2C),
            borderRadius: BorderRadius.circular(32),
            border: Border.all(color: Colors.white.withValues(alpha: 0.16)),
            boxShadow: [
              BoxShadow(
                color: Colors.black.withValues(alpha: 0.35),
                blurRadius: 20,
                offset: const Offset(0, 8),
              ),
            ],
          ),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              _buildDockItem(
                type: FloatingDockPanelType.dataset,
                tooltip: '工区数据集',
              ),
              const SizedBox(width: 6),
              _buildDockItem(
                type: FloatingDockPanelType.measurement,
                tooltip: '几何测量',
              ),
              const SizedBox(width: 6),
              _buildDockItem(
                type: FloatingDockPanelType.stats,
                tooltip: '标量统计',
              ),
              const SizedBox(width: 6),
              _buildDockItem(
                type: FloatingDockPanelType.rendering,
                tooltip: '渲染着色',
              ),
              const SizedBox(width: 10),
              Container(
                width: 1,
                height: 22,
                color: Colors.white.withValues(alpha: 0.2),
              ),
              const SizedBox(width: 10),
              // 快速重置视角
              Tooltip(
                message: '复位视角',
                child: InkWell(
                  onTap: () {
                    ScaffoldMessenger.of(context).showSnackBar(
                      const SnackBar(
                        content: Text('视角已重置为俯瞰默认姿态'),
                        duration: Duration(milliseconds: 900),
                      ),
                    );
                  },
                  borderRadius: BorderRadius.circular(20),
                  child: Container(
                    padding: const EdgeInsets.all(8),
                    decoration: BoxDecoration(
                      shape: BoxShape.circle,
                      color: Colors.white.withValues(alpha: 0.06),
                    ),
                    child: const Icon(
                      Icons.refresh_rounded,
                      color: Colors.white70,
                      size: 18,
                    ),
                  ),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildDockItem({
    required FloatingDockPanelType type,
    required String tooltip,
  }) {
    final isSelected = _activePanel == type;
    return Tooltip(
      message: tooltip,
      child: InkWell(
        onTap: () => _togglePanel(type),
        borderRadius: BorderRadius.circular(22),
        child: AnimatedContainer(
          duration: const Duration(milliseconds: 200),
          padding: EdgeInsets.symmetric(
            horizontal: isSelected ? 14 : 10,
            vertical: 8,
          ),
          decoration: BoxDecoration(
            color: isSelected
                ? AppTheme.accentBlue
                : Colors.white.withValues(alpha: 0.06),
            borderRadius: BorderRadius.circular(22),
          ),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(
                type.icon,
                color: isSelected ? Colors.white : Colors.white70,
                size: 19,
              ),
              if (isSelected) ...[
                const SizedBox(width: 6),
                Text(
                  type.title,
                  style: const TextStyle(
                    color: Colors.white,
                    fontSize: 12,
                    fontWeight: FontWeight.w600,
                  ),
                ),
              ],
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildFloatingCard(FloatingDockPanelType type) {
    return ClipRRect(
      borderRadius: BorderRadius.circular(16),
      child: BackdropFilter(
        filter: ImageFilter.blur(sigmaX: 20, sigmaY: 20),
        child: Container(
          constraints: const BoxConstraints(maxHeight: 460),
          decoration: BoxDecoration(
            color: const Color(0xEB1C2331),
            borderRadius: BorderRadius.circular(16),
            border: Border.all(color: Colors.white.withValues(alpha: 0.18)),
            boxShadow: [
              BoxShadow(
                color: Colors.black.withValues(alpha: 0.4),
                blurRadius: 24,
                offset: const Offset(0, 10),
              ),
            ],
          ),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              // 卡片头部
              Container(
                padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 10),
                decoration: BoxDecoration(
                  border: Border(
                    bottom: BorderSide(color: Colors.white.withValues(alpha: 0.1)),
                  ),
                ),
                child: Row(
                  children: [
                    Icon(type.icon, size: 18, color: AppTheme.accentBlue),
                    const SizedBox(width: 8),
                    Text(
                      type.title,
                      style: const TextStyle(
                        color: Colors.white,
                        fontSize: 13,
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                    const Spacer(),
                    InkWell(
                      onTap: () => setState(() => _activePanel = null),
                      borderRadius: BorderRadius.circular(12),
                      child: Padding(
                        padding: const EdgeInsets.all(4),
                        child: Icon(
                          Icons.close_rounded,
                          size: 16,
                          color: Colors.white.withValues(alpha: 0.6),
                        ),
                      ),
                    ),
                  ],
                ),
              ),

              // 卡片内容区
              Flexible(
                child: SingleChildScrollView(
                  padding: const EdgeInsets.all(14),
                  child: _buildCardContent(type),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildCardContent(FloatingDockPanelType type) {
    switch (type) {
      case FloatingDockPanelType.dataset:
        return Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            _buildDataRow('工区名称', widget.service.summary.name),
            _buildDataRow('文件大小', widget.service.summary.fileSize),
            _buildDataRow('点云点数', '${widget.service.summary.pointCount} 测点'),
            _buildDataRow('八叉树层级', '${widget.service.summary.lodDetails.length} 级 LOD'),
            _buildDataRow('LOD 状态', widget.service.summary.lodEnabled ? '已就绪' : '未构建'),
          ],
        );
      case FloatingDockPanelType.measurement:
        return Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Container(
              padding: const EdgeInsets.all(10),
              decoration: BoxDecoration(
                color: Colors.white.withValues(alpha: 0.04),
                borderRadius: BorderRadius.circular(8),
              ),
              child: const Column(
                children: [
                  Row(
                    mainAxisAlignment: MainAxisAlignment.spaceBetween,
                    children: [
                      Text('三维测距 (3D Distance)', style: TextStyle(color: Colors.white70, fontSize: 12)),
                      Text('184.62 m', style: TextStyle(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 12)),
                    ],
                  ),
                  SizedBox(height: 6),
                  Row(
                    mainAxisAlignment: MainAxisAlignment.spaceBetween,
                    children: [
                      Text('水平投影距 (Horizontal)', style: TextStyle(color: Colors.white70, fontSize: 12)),
                      Text('165.20 m', style: TextStyle(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 12)),
                    ],
                  ),
                  SizedBox(height: 6),
                  Row(
                    mainAxisAlignment: MainAxisAlignment.spaceBetween,
                    children: [
                      Text('绝对高差 (Elevation ΔZ)', style: TextStyle(color: Colors.white70, fontSize: 12)),
                      Text('82.35 m', style: TextStyle(color: Colors.white, fontWeight: FontWeight.bold, fontSize: 12)),
                    ],
                  ),
                ],
              ),
            ),
          ],
        );
      case FloatingDockPanelType.stats:
        return Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text(
              '标量分布直方图 (Histogram)',
              style: TextStyle(color: Colors.white70, fontSize: 12),
            ),
            const SizedBox(height: 10),
            SizedBox(
              height: 70,
              child: Row(
                crossAxisAlignment: CrossAxisAlignment.end,
                mainAxisAlignment: MainAxisAlignment.spaceEvenly,
                children: [
                  _buildHistBar(0.25, '0-20'),
                  _buildHistBar(0.55, '20-40'),
                  _buildHistBar(0.95, '40-60'),
                  _buildHistBar(0.70, '60-80'),
                  _buildHistBar(0.35, '80-100'),
                ],
              ),
            ),
            const SizedBox(height: 12),
            _buildDataRow('均值 (Mean)', '48.35'),
            _buildDataRow('极差 (Range)', '0.00 ~ 98.42'),
          ],
        );
      case FloatingDockPanelType.rendering:
        return Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                const Text('散点半径', style: TextStyle(color: Colors.white70, fontSize: 12)),
                Text('${widget.service.pointSize.toStringAsFixed(1)} px', style: const TextStyle(color: Colors.white, fontSize: 12, fontWeight: FontWeight.bold)),
              ],
            ),
            Slider(
              value: widget.service.pointSize,
              min: 1.0,
              max: 10.0,
              activeColor: AppTheme.accentBlue,
              inactiveColor: Colors.white.withValues(alpha: 0.1),
              onChanged: (val) => widget.service.setPointSize(val),
            ),
            const SizedBox(height: 10),
            const Text('科学色表映射', style: TextStyle(color: Colors.white70, fontSize: 12)),
            const SizedBox(height: 6),
            DropdownButtonFormField<String>(
              initialValue: widget.service.colormap,
              dropdownColor: const Color(0xFF1E2533),
              style: const TextStyle(color: Colors.white, fontSize: 12),
              decoration: InputDecoration(
                contentPadding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
                filled: true,
                fillColor: Colors.white.withValues(alpha: 0.06),
                border: OutlineInputBorder(borderRadius: BorderRadius.circular(8)),
              ),
              items: const [
                DropdownMenuItem(value: 'viridis', child: Text('Viridis (默认高反差)')),
                DropdownMenuItem(value: 'plasma', child: Text('Plasma (热力紫黄)')),
                DropdownMenuItem(value: 'turbo', child: Text('Turbo (彩虹平滑)')),
                DropdownMenuItem(value: 'jet', child: Text('Jet (经典色度)')),
                DropdownMenuItem(value: 'coolwarm', child: Text('Coolwarm (双极冷暖)')),
              ],
              onChanged: (val) {
                if (val != null) widget.service.setColormap(val);
              },
            ),
          ],
        );
    }
  }

  Widget _buildDataRow(String label, String value) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 8),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          SizedBox(
            width: 80,
            child: Text(
              label,
              style: TextStyle(color: Colors.white.withValues(alpha: 0.6), fontSize: 11),
            ),
          ),
          Expanded(
            child: Text(
              value,
              style: const TextStyle(color: Colors.white, fontSize: 11, fontWeight: FontWeight.w500),
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildHistBar(double fraction, String label) {
    return Column(
      mainAxisAlignment: MainAxisAlignment.end,
      children: [
        Container(
          width: 28,
          height: 50 * fraction,
          decoration: BoxDecoration(
            color: AppTheme.accentBlue.withValues(alpha: 0.85),
            borderRadius: BorderRadius.circular(4),
          ),
        ),
        const SizedBox(height: 4),
        Text(
          label,
          style: TextStyle(color: Colors.white.withValues(alpha: 0.5), fontSize: 9),
        ),
      ],
    );
  }

  Widget _buildMiniHud() {
    return ClipRRect(
      borderRadius: BorderRadius.circular(16),
      child: BackdropFilter(
        filter: ImageFilter.blur(sigmaX: 12, sigmaY: 12),
        child: Container(
          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
          decoration: BoxDecoration(
            color: const Color(0xB0121720),
            borderRadius: BorderRadius.circular(16),
            border: Border.all(color: Colors.white.withValues(alpha: 0.12)),
          ),
          child: Row(
            mainAxisSize: MainAxisSize.min,
            children: [
              Container(
                width: 6,
                height: 6,
                decoration: const BoxDecoration(
                  color: AppTheme.statusGreen,
                  shape: BoxShape.circle,
                ),
              ),
              const SizedBox(width: 6),
              Text(
                '${widget.service.points.length} 测点 | 60 FPS | 沉浸 Dock',
                style: const TextStyle(
                  color: Colors.white70,
                  fontSize: 11,
                  fontWeight: FontWeight.w500,
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
