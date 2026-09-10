import 'package:flutter/material.dart';
import '../theme/gs3d_form_factor.dart';
import '../theme/gs3d_tokens.dart';
import '../ffi/geoscatter3d_service.dart';
import '../panels/center_viewport.dart';
import '../panels/left_dock_panel.dart';
import '../panels/right_dock_panel.dart';
import '../widgets/fluid.dart';
import 'standard_workbench_layout.dart';

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
    // 入口档位守卫：暗色分析舱为桌面专属形态；触控档切到本布局时保持
    // 触控工作台并显式提示（与 floatingDock 同一守卫策略）。
    if (context.isTouchPreferred) {
      return _TouchFallbackScaffold(service: widget.service);
    }
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
        color: Gs3dDark.cabinBg,
        border: Border(
          right: BorderSide(color: Gs3dDark.cabinBorder),
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
                colors: [Gs3dDark.accentFrom, Gs3dDark.accentTo],
              ),
              borderRadius: BorderRadius.circular(8),
            ),
            child: const Icon(
              Icons.scatter_plot_rounded,
              color: Gs3dDark.textPrimary,
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
            child: Gs3dPressable(
              onTap: () => setState(() => _showRightPanel = !_showRightPanel),
              hoverScale: 0,
              builder: (context, state) => Container(
                padding: const EdgeInsets.all(10),
                decoration: BoxDecoration(
                  color: state == Gs3dInteractionState.idle
                      ? Colors.transparent
                      : Gs3dDark.hairline,
                  borderRadius: BorderRadius.circular(Gs3dTokens.radiusMd),
                ),
                child: Icon(
                  _showRightPanel
                      ? Icons.dock_rounded
                      : Icons.arrow_back_ios_new_rounded,
                  size: 16,
                  color: Gs3dDark.textTertiary,
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
    final a11y = context.gs3dA11y;

    return Tooltip(
      message: tab.title,
      waitDuration: Gs3dMotion.micro,
      child: Gs3dPressable(
        onTap: () => _onTabSelected(tab),
        hoverScale: 0,
        // 选中 / 悬停 / 静止三态由同一弹簧驱动：颜色强度连续插值，
        // 高频切换标签时从当前位置续接，不闪烁。
        builder: (context, state) => Gs3dSpringValue(
          value: isSelected
              ? 1.0
              : (state == Gs3dInteractionState.idle ? 0.0 : 0.45),
          spring: a11y.resolveSpring(Gs3dMotion.feedback),
          builder: (context, t, child) => Container(
            width: 42,
            height: 42,
            decoration: BoxDecoration(
              color: Gs3dTokens.accent.withValues(alpha: 0.20 * t),
              borderRadius: BorderRadius.circular(Gs3dTokens.radiusMd),
              border: Border.all(
                color: Gs3dTokens.accent.withValues(alpha: t),
                width: 1.5,
              ),
            ),
            child: child,
          ),
          child: Icon(
            tab.icon,
            color: Color.lerp(
              Gs3dDark.textSecondary,
              Gs3dTokens.accent,
              isSelected ? 1.0 : 0.45,
            ),
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
        color: Gs3dDark.railBg,
        border: Border(
          right: BorderSide(color: Gs3dDark.railBorder),
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
                bottom: BorderSide(color: Gs3dDark.railBorder),
              ),
            ),
            child: Row(
              children: [
                Icon(tab.icon, size: 16, color: Gs3dTokens.accent),
                const SizedBox(width: 8),
                Text(
                  tab.title,
                  style: const TextStyle(
                    color: Gs3dDark.textPrimary,
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
                      color: Gs3dDark.textTertiary,
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
              style: TextStyle(color: Gs3dDark.textPrimary, fontSize: 13, fontWeight: FontWeight.bold),
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
        color: Gs3dDark.fillFaint,
        borderRadius: BorderRadius.circular(8),
        border: Border.all(color: Gs3dDark.fillHairline),
      ),
      child: Row(
        children: [
          Icon(icon, size: 18, color: Gs3dDark.textSecondary),
          const SizedBox(width: 10),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  label,
                  style: TextStyle(color: Gs3dDark.fillStrong, fontSize: 10),
                ),
                const SizedBox(height: 2),
                Text(
                  value,
                  style: const TextStyle(color: Gs3dDark.textPrimary, fontSize: 11, fontWeight: FontWeight.w500),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

/// 触控档布局守卫回退：提示 + 触控工作台（StandardWorkbenchLayout 内部
/// 已按档位呈现抽屉形态）。
class _TouchFallbackScaffold extends StatelessWidget {
  const _TouchFallbackScaffold({required this.service});

  final GeoScatter3dService service;

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        Container(
          key: const ValueKey('tablet.layout_guard_banner'),
          width: double.infinity,
          padding: const EdgeInsets.symmetric(
            horizontal: Gs3dTokens.space16,
            vertical: Gs3dTokens.space8,
          ),
          color: Gs3dTokens.infoSubtle,
          child: Text(
            '「${service.layoutMode.displayName}」为桌面专属布局，'
            '已在触控设备保持触控工作台',
            style: Gs3dType.caption.copyWith(color: Gs3dTokens.textBody),
          ),
        ),
        Expanded(child: StandardWorkbenchLayout(service: service)),
      ],
    );
  }
}
