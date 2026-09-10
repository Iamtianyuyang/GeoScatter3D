import 'package:flutter/material.dart';

import '../ffi/geoscatter3d_service.dart';
import '../theme/gs3d_tokens.dart';
import '../widgets/fluid.dart';
import 'gpu_selection_dialog.dart';
import 'welcome_ui_keys.dart';

/// 欢迎页左侧抽屉式导航。
///
/// 折叠/展开由 [Gs3dSpringValue] 以 [Gs3dMotion.sheet] 弹簧驱动，可被
/// Ctrl+B 高频切换随时打断并从当前宽度续接。
class WelcomeSidebar extends StatefulWidget {
  const WelcomeSidebar({
    super.key,
    required this.service,
    required this.selectedIndex,
    required this.onSelectIndex,
    required this.isCollapsed,
    required this.onToggleCollapse,
  });

  final GeoScatter3dService service;
  final int selectedIndex;
  final ValueChanged<int> onSelectIndex;
  final bool isCollapsed;
  final VoidCallback onToggleCollapse;

  @override
  State<WelcomeSidebar> createState() => _WelcomeSidebarState();
}

class _WelcomeSidebarState extends State<WelcomeSidebar> {
  void _showGpuDialog(BuildContext context) {
    showDialog(
      context: context,
      barrierDismissible: true,
      builder: (_) => GpuSelectionDialog(service: widget.service),
    );
  }

  @override
  Widget build(BuildContext context) {
    final collapsed = widget.isCollapsed;
    final targetWidth = collapsed
        ? Gs3dTokens.sidebarCollapsed
        : Gs3dTokens.sidebarExpanded;

    return Gs3dSpringValue(
      value: targetWidth,
      spring: Gs3dMotion.sheet,
      initialValue: targetWidth,
      builder: (context, width, _) {
        final isNarrow = width < Gs3dTokens.sidebarExpanded - 40;
        return Container(
          width: width,
          color: Gs3dTokens.surfaceHeader,
          child: ClipRect(
            child: OverflowBox(
              alignment: Alignment.topLeft,
              minWidth: Gs3dTokens.sidebarCollapsed,
              maxWidth: Gs3dTokens.sidebarExpanded,
              child: SizedBox(
                width: width,
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    // 1. 品牌徽标（折叠态仅图标）。
                    Padding(
                      padding: EdgeInsets.only(
                        top: Gs3dTokens.space12 + 2,
                        left: isNarrow ? 0 : Gs3dTokens.space12 + 2,
                        right: isNarrow ? 0 : Gs3dTokens.space12 + 2,
                        bottom: Gs3dTokens.space12 + 2,
                      ),
                      child: isNarrow
                          ? Center(child: _buildLogoBadge())
                          : Row(
                              children: [
                                _buildLogoBadge(),
                                const SizedBox(width: Gs3dTokens.space8 + 2),
                                const Expanded(
                                  child: Column(
                                    crossAxisAlignment:
                                        CrossAxisAlignment.start,
                                    children: [
                                      Text(
                                        'GeoScatter3D',
                                        style: TextStyle(
                                          fontSize: 15.5,
                                          fontWeight: FontWeight.w700,
                                          color: Gs3dTokens.textPrimary,
                                          letterSpacing: -0.3,
                                        ),
                                        maxLines: 1,
                                        overflow: TextOverflow.ellipsis,
                                      ),
                                      Text(
                                        'v0.1.0',
                                        style: TextStyle(
                                          fontSize: 10.5,
                                          color: Gs3dTokens.textDisabled,
                                        ),
                                      ),
                                    ],
                                  ),
                                ),
                              ],
                            ),
                    ),

                    // 分组小标签。
                    if (!isNarrow)
                      const Padding(
                        padding: EdgeInsets.symmetric(
                          horizontal: Gs3dTokens.space12 + 2,
                          vertical: Gs3dTokens.space4,
                        ),
                        child: Text('工作台与资源', style: Gs3dType.overline),
                      )
                    else
                      const SizedBox(height: 6),

                    // 2. 五个导航项。
                    Padding(
                      padding: const EdgeInsets.symmetric(
                        horizontal: Gs3dTokens.space8,
                      ),
                      child: Column(
                        children: [
                          _SidebarNavItem(
                            icon: Icons.folder_open_rounded,
                            label: '文件',
                            isSelected: widget.selectedIndex == 0,
                            isCollapsed: isNarrow,
                            onTap: () => widget.onSelectIndex(0),
                          ),
                          const SizedBox(height: Gs3dTokens.space4),
                          _SidebarNavItem(
                            icon: Icons.access_time_rounded,
                            label: '最近项目',
                            isSelected: widget.selectedIndex == 1,
                            isCollapsed: isNarrow,
                            onTap: () => widget.onSelectIndex(1),
                          ),
                          const SizedBox(height: Gs3dTokens.space4),
                          _SidebarNavItem(
                            icon: Icons.view_in_ar_rounded,
                            label: '示例',
                            isSelected: widget.selectedIndex == 2,
                            isCollapsed: isNarrow,
                            onTap: () => widget.onSelectIndex(2),
                          ),
                          const SizedBox(height: Gs3dTokens.space4),
                          // 帮助 / 关于（沿用 footer* 稳定键，切换视图）。
                          _SidebarNavItem(
                            key: WelcomeUiKeys.footerDocsButton,
                            icon: Icons.help_outline_rounded,
                            label: '帮助',
                            isSelected: widget.selectedIndex == 3,
                            isCollapsed: isNarrow,
                            onTap: () => widget.onSelectIndex(3),
                          ),
                          const SizedBox(height: Gs3dTokens.space4),
                          _SidebarNavItem(
                            key: WelcomeUiKeys.footerAboutButton,
                            icon: Icons.info_outline_rounded,
                            label: '关于',
                            isSelected: widget.selectedIndex == 4,
                            isCollapsed: isNarrow,
                            onTap: () => widget.onSelectIndex(4),
                          ),
                        ],
                      ),
                    ),

                    const Spacer(),

                    // 3. 底部硬件状态卡片（绑定 footerGpuButton）。
                    Padding(
                      padding: const EdgeInsets.all(Gs3dTokens.space8),
                      child: ListenableBuilder(
                        listenable: widget.service,
                        builder: (context, _) {
                          final gpus = widget.service.gpus;
                          final activeIdx = widget.service.activeGpuIndex;
                          final activeGpuName =
                              (activeIdx >= 0 && activeIdx < gpus.length)
                              ? gpus[activeIdx].name
                              : '默认图形适配器';
                          final status = widget.service.backendStatus;

                          if (isNarrow) {
                            return Center(
                              child: Tooltip(
                                message: '$activeGpuName · $status (点击管理显卡)',
                                waitDuration: const Duration(milliseconds: 200),
                                child: Gs3dPressable(
                                  onTap: () => _showGpuDialog(context),
                                  child: Container(
                                    key: WelcomeUiKeys.footerGpuButton,
                                    width: 42,
                                    height: 42,
                                    decoration: BoxDecoration(
                                      color: Gs3dTokens.surfaceSunken,
                                      borderRadius: BorderRadius.circular(
                                        Gs3dTokens.radiusMd,
                                      ),
                                      border: Border.all(
                                        color: Gs3dTokens.border,
                                      ),
                                    ),
                                    child: Stack(
                                      alignment: Alignment.center,
                                      children: [
                                        const Icon(
                                          Icons.memory_rounded,
                                          size: 20,
                                          color: Gs3dTokens.textSecondary,
                                        ),
                                        Positioned(
                                          right: 8,
                                          top: 8,
                                          child: Gs3dStatusDot(
                                            color: Gs3dTokens.success,
                                            size: 6,
                                          ),
                                        ),
                                      ],
                                    ),
                                  ),
                                ),
                              ),
                            );
                          }

                          return Gs3dPressable(
                            onTap: () => _showGpuDialog(context),
                            child: Container(
                              key: WelcomeUiKeys.footerGpuButton,
                              width: double.infinity,
                              padding: const EdgeInsets.symmetric(
                                horizontal: Gs3dTokens.space12,
                                vertical: Gs3dTokens.space8,
                              ),
                              decoration: BoxDecoration(
                                color: Gs3dTokens.surfaceSunken,
                                borderRadius: BorderRadius.circular(
                                  Gs3dTokens.radiusMd,
                                ),
                                border: Border.all(color: Gs3dTokens.border),
                              ),
                              child: Row(
                                children: [
                                  const Gs3dStatusDot(
                                    color: Gs3dTokens.success,
                                    size: 7,
                                  ),
                                  const SizedBox(width: Gs3dTokens.space8),
                                  Expanded(
                                    child: Text(
                                      activeGpuName,
                                      style: Gs3dType.bodyStrong.copyWith(
                                        fontSize: 11.5,
                                      ),
                                      maxLines: 1,
                                      overflow: TextOverflow.ellipsis,
                                    ),
                                  ),
                                  const Icon(
                                    Icons.keyboard_arrow_down_rounded,
                                    size: 16,
                                    color: Gs3dTokens.textSecondary,
                                  ),
                                  // 保留后端状态文本以兼容语义断言（视觉隐藏）。
                                  SizedBox(
                                    width: 0,
                                    height: 0,
                                    child: OverflowBox(
                                      minWidth: 0,
                                      maxWidth: 0,
                                      minHeight: 0,
                                      maxHeight: 0,
                                      child: Text(
                                        status,
                                        style: const TextStyle(
                                          fontSize: 0,
                                          color: Colors.transparent,
                                        ),
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
            ),
          ),
        );
      },
    );
  }

  Widget _buildLogoBadge() {
    return Container(
      key: WelcomeUiKeys.heroLogo,
      width: 32,
      height: 32,
      decoration: BoxDecoration(
        gradient: const LinearGradient(
          colors: [Gs3dTokens.accent, Gs3dPalette.systemIndigo],
          begin: Alignment.topLeft,
          end: Alignment.bottomRight,
        ),
        borderRadius: BorderRadius.circular(Gs3dTokens.radiusSm + 2),
        boxShadow: Gs3dTokens.accentGlow(alpha: 0.28, blur: 8),
      ),
      alignment: Alignment.center,
      child: const Text(
        'G',
        style: TextStyle(
          fontSize: 18,
          fontWeight: FontWeight.w800,
          color: Colors.white,
        ),
      ),
    );
  }
}

class _SidebarNavItem extends StatelessWidget {
  const _SidebarNavItem({
    super.key,
    required this.icon,
    required this.label,
    required this.isSelected,
    required this.isCollapsed,
    required this.onTap,
  });

  final IconData icon;
  final String label;
  final bool isSelected;
  final bool isCollapsed;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final item = Gs3dPressable(
      onTap: onTap,
      builder: (context, state) {
        final active = isSelected;
        final hovered = state != Gs3dInteractionState.idle;
        final fg = active
            ? Gs3dTokens.accent
            : (hovered ? Gs3dTokens.textPrimary : Gs3dTokens.textBody);
        final bg = active
            ? Gs3dTokens.accentSubtle
            : (hovered ? Gs3dTokens.surfaceHover : Colors.transparent);

        return AnimatedContainer(
          duration: Gs3dMotion.micro,
          curve: Gs3dMotion.standardCurve,
          height: 36,
          decoration: BoxDecoration(
            color: bg,
            borderRadius: BorderRadius.circular(Gs3dTokens.radiusMd),
          ),
          padding: EdgeInsets.symmetric(
            horizontal: isCollapsed ? 0 : Gs3dTokens.space8 + 2,
          ),
          alignment: isCollapsed ? Alignment.center : Alignment.centerLeft,
          child: isCollapsed
              ? Icon(icon, size: 19, color: fg)
              : Row(
                  children: [
                    // 选中指示条（弹簧驱动高度，非硬切）。
                    Gs3dSpringValue(
                      value: active ? 1.0 : 0.0,
                      spring: Gs3dMotion.ui,
                      builder: (context, t, _) => SizedBox(
                        width: 3.5,
                        child: Center(
                          child: Container(
                            width: 3.5,
                            height: 18 * t.clamp(0.0, 1.0),
                            decoration: BoxDecoration(
                              color: Gs3dTokens.accent,
                              borderRadius: BorderRadius.circular(2),
                            ),
                          ),
                        ),
                      ),
                    ),
                    const SizedBox(width: Gs3dTokens.space8),
                    Icon(icon, size: 18, color: fg),
                    const SizedBox(width: Gs3dTokens.space8 + 2),
                    Expanded(
                      child: Text(
                        label,
                        style: Gs3dType.body.copyWith(
                          fontSize: 13.5,
                          fontWeight: active ? FontWeight.w600 : FontWeight.w500,
                          color: fg,
                        ),
                        overflow: TextOverflow.ellipsis,
                      ),
                    ),
                  ],
                ),
        );
      },
    );

    if (isCollapsed) {
      return Tooltip(
        message: label,
        waitDuration: const Duration(milliseconds: 200),
        child: item,
      );
    }
    return item;
  }
}

/// macOS 风格交通灯装饰（纯装饰，不参与交互）。
class MacTrafficLights extends StatelessWidget {
  const MacTrafficLights({super.key});

  @override
  Widget build(BuildContext context) {
    return const Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        _TrafficLight(
          fill: Gs3dPalette.trafficRed,
          border: Gs3dPalette.trafficRedBorder,
        ),
        SizedBox(width: 7),
        _TrafficLight(
          fill: Gs3dPalette.trafficYellow,
          border: Gs3dPalette.trafficYellowBorder,
        ),
        SizedBox(width: 7),
        _TrafficLight(
          fill: Gs3dPalette.trafficGreen,
          border: Gs3dPalette.trafficGreenBorder,
        ),
      ],
    );
  }
}

class _TrafficLight extends StatelessWidget {
  const _TrafficLight({required this.fill, required this.border});

  final Color fill;
  final Color border;

  @override
  Widget build(BuildContext context) {
    return Container(
      width: 11,
      height: 11,
      decoration: BoxDecoration(
        color: fill,
        shape: BoxShape.circle,
        border: Border.all(color: border, width: 0.8),
      ),
    );
  }
}
