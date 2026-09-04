import 'package:flutter/material.dart';
import '../ffi/geoscatter3d_service.dart';
import '../panels/center_viewport.dart';
import '../panels/left_dock_panel.dart';
import '../panels/right_dock_panel.dart';
import '../theme/app_theme.dart';

enum RailTabType {
  dataset,
  measurement,
  stats,
  settings;

  String get title {
    switch (this) {
      case RailTabType.dataset:
        return '工程与数据集';
      case RailTabType.measurement:
        return '空间几何测量';
      case RailTabType.stats:
        return '标量区域统计';
      case RailTabType.settings:
        return '系统与首选项';
    }
  }

  IconData get icon {
    switch (this) {
      case RailTabType.dataset:
        return Icons.folder_open_rounded;
      case RailTabType.measurement:
        return Icons.straighten_rounded;
      case RailTabType.stats:
        return Icons.bar_chart_rounded;
      case RailTabType.settings:
        return Icons.tune_rounded;
    }
  }
}

/// 方案 C · 暗色分析舱布局 (Analysis Rail Layout)
/// 左侧 54px 紧凑图标导轨 + 272px 互斥滑出式抽屉 + 中央宽广视口 + 右侧属性栏
class AnalysisRailLayout extends StatefulWidget {
  final GeoScatter3dService service;

  const AnalysisRailLayout({
    super.key,
    required this.service,
  });

  @override
  State<AnalysisRailLayout> createState() => _AnalysisRailLayoutState();
}

class _AnalysisRailLayoutState extends State<AnalysisRailLayout> {
  RailTabType? _activeTab = RailTabType.dataset;
  bool _showRightPanel = true;

  void _onTabSelected(RailTabType tab) {
    setState(() {
      if (_activeTab == tab) {
        _activeTab = null; // 再次点击折叠收起
      } else {
        _activeTab = tab;
      }
    });
  }

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        // 1. 左侧 54px 紧凑深色图标导轨 (Activity Rail)
        _buildIconRail(),

        // 2. 320px 宽的互斥滑动展开抽屉 (Drawer)
        AnimatedContainer(
          duration: const Duration(milliseconds: 220),
          curve: Curves.easeOutCubic,
          width: _activeTab != null ? 320 : 0,
          child: _activeTab != null
              ? _buildDrawerContent(_activeTab!)
              : const SizedBox.shrink(),
        ),

        // 3. 中央宽广 3D 渲染视口
        Expanded(
          child: CenterViewport(service: widget.service),
        ),

        // 4. 右侧快捷属性栏 (带收起/展开控制)
        if (_showRightPanel)
          RightDockPanel(service: widget.service),
      ],
    );
  }

  Widget _buildIconRail() {
    return Container(
      width: 54,
      decoration: const BoxDecoration(
        color: Color(0xFF161C26),
        border: Border(
          right: BorderSide(color: Color(0xFF232B3A)),
        ),
      ),
      child: Column(
        children: [
          const SizedBox(height: 12),
          // 顶部标志点
          Container(
            width: 32,
            height: 32,
            decoration: BoxDecoration(
              gradient: const LinearGradient(
                colors: [Color(0xFF2563EB), Color(0xFF0D9488)],
              ),
              borderRadius: BorderRadius.circular(8),
            ),
            child: const Icon(
              Icons.scatter_plot_rounded,
              color: Colors.white,
              size: 20,
            ),
          ),
          const SizedBox(height: 20),

          // 核心工具导轨项
          _buildRailItem(RailTabType.dataset),
          const SizedBox(height: 8),
          _buildRailItem(RailTabType.measurement),
          const SizedBox(height: 8),
          _buildRailItem(RailTabType.stats),
          const SizedBox(height: 8),
          _buildRailItem(RailTabType.settings),

          const Spacer(),

          // 右侧栏折叠开关
          Tooltip(
            message: _showRightPanel ? '收起右侧属性栏' : '展开右侧属性栏',
            child: InkWell(
              onTap: () => setState(() => _showRightPanel = !_showRightPanel),
              borderRadius: BorderRadius.circular(8),
              child: Container(
                padding: const EdgeInsets.all(10),
                child: Icon(
                  _showRightPanel
                      ? Icons.dock_rounded
                      : Icons.arrow_back_ios_new_rounded,
                  size: 16,
                  color: Colors.white54,
                ),
              ),
            ),
          ),
          const SizedBox(height: 12),
        ],
      ),
    );
  }

  Widget _buildRailItem(RailTabType tab) {
    final isSelected = _activeTab == tab;
    return Tooltip(
      message: tab.title,
      waitDuration: const Duration(milliseconds: 300),
      child: InkWell(
        onTap: () => _onTabSelected(tab),
        borderRadius: BorderRadius.circular(10),
        child: AnimatedContainer(
          duration: const Duration(milliseconds: 160),
          width: 42,
          height: 42,
          decoration: BoxDecoration(
            color: isSelected
                ? AppTheme.accentBlue.withValues(alpha: 0.2)
                : Colors.transparent,
            borderRadius: BorderRadius.circular(10),
            border: Border.all(
              color: isSelected ? AppTheme.accentBlue : Colors.transparent,
              width: 1.5,
            ),
          ),
          child: Icon(
            tab.icon,
            color: isSelected ? AppTheme.accentBlue : Colors.white60,
            size: 20,
          ),
        ),
      ),
    );
  }

  Widget _buildDrawerContent(RailTabType tab) {
    return Container(
      width: 320,
      decoration: const BoxDecoration(
        color: Color(0xFF1A2230),
        border: Border(
          right: BorderSide(color: Color(0xFF283344)),
        ),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          // 抽屉顶栏
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 12),
            decoration: const BoxDecoration(
              border: Border(
                bottom: BorderSide(color: Color(0xFF283344)),
              ),
            ),
            child: Row(
              children: [
                Icon(tab.icon, size: 16, color: AppTheme.accentBlue),
                const SizedBox(width: 8),
                Text(
                  tab.title,
                  style: const TextStyle(
                    color: Colors.white,
                    fontSize: 13,
                    fontWeight: FontWeight.bold,
                  ),
                ),
                const Spacer(),
                InkWell(
                  onTap: () => setState(() => _activeTab = null),
                  borderRadius: BorderRadius.circular(8),
                  child: const Padding(
                    padding: EdgeInsets.all(4),
                    child: Icon(
                      Icons.close_rounded,
                      size: 16,
                      color: Colors.white54,
                    ),
                  ),
                ),
              ],
            ),
          ),

          // 抽屉主体 (直连 LeftDockPanel，内部自包含滚动视口)
          Expanded(
            child: _buildDrawerBody(tab),
          ),
        ],
      ),
    );
  }

  Widget _buildDrawerBody(RailTabType tab) {
    switch (tab) {
      case RailTabType.dataset:
      case RailTabType.measurement:
      case RailTabType.stats:
        // 复用 LeftDockPanel 的完备数据卡片流
        return LeftDockPanel(service: widget.service);
      case RailTabType.settings:
        return Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text(
              '首选项与运行环境',
              style: TextStyle(color: Colors.white, fontSize: 13, fontWeight: FontWeight.bold),
            ),
            const SizedBox(height: 12),
            _buildSettingItem(
              '当前渲染后端',
              'Vulkan 1.3 / Direct C-ABI',
              Icons.developer_board_rounded,
            ),
            _buildSettingItem(
              '活跃 GPU 设备',
              widget.service.gpus.isNotEmpty
                  ? widget.service.gpus[widget.service.activeGpuIndex].name
                  : 'NVIDIA RTX 4070 Laptop',
              Icons.memory_rounded,
            ),
            _buildSettingItem(
              '内存池状态',
              '统一堆分配 · Zero-Copy 映射',
              Icons.speed_rounded,
            ),
          ],
        );
    }
  }

  Widget _buildSettingItem(String label, String value, IconData icon) {
    return Container(
      margin: const EdgeInsets.only(bottom: 10),
      padding: const EdgeInsets.all(10),
      decoration: BoxDecoration(
        color: Colors.white.withValues(alpha: 0.04),
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: Colors.white.withValues(alpha: 0.08)),
      ),
      child: Row(
        children: [
          Icon(icon, size: 18, color: Colors.white60),
          const SizedBox(width: 10),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  label,
                  style: TextStyle(color: Colors.white.withValues(alpha: 0.6), fontSize: 10),
                ),
                const SizedBox(height: 2),
                Text(
                  value,
                  style: const TextStyle(color: Colors.white, fontSize: 11, fontWeight: FontWeight.w500),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}
