import 'package:flutter/material.dart';

import '../ffi/geoscatter3d_service.dart';
import '../theme/app_tokens.dart';
import '../widgets/telemetry_hud.dart';
import '../widgets/viewport_canvas.dart';

/// 主界面分区骨架。折叠仅改变布局，不承载业务状态。
class MainShell extends StatefulWidget {
  const MainShell({super.key, this.viewport, this.service});

  /// 测试可注入轻量占位；正式应用默认创建真实 Vulkan 视口。
  final Widget? viewport;

  /// 外部已有状态中枢时可注入；否则由 Shell 负责创建与释放。
  final GeoScatter3dService? service;

  @override
  State<MainShell> createState() => _MainShellState();
}

class _MainShellState extends State<MainShell> {
  bool? _leftExpanded;
  bool? _rightExpanded;
  bool? _bottomExpanded;
  late final bool _ownsService;
  late final GeoScatter3dService _service;

  @override
  void initState() {
    super.initState();
    _ownsService = widget.service == null;
    _service = widget.service ?? GeoScatter3dService();
  }

  @override
  void dispose() {
    if (_ownsService) _service.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;

    return Scaffold(
      backgroundColor: tokens.colors.shellBackground,
      body: SafeArea(
        child: LayoutBuilder(
          builder: (context, constraints) {
            final leftExpanded =
                _leftExpanded ??
                constraints.maxWidth >= tokens.sizes.shellSidebarBreakpoint;
            final rightExpanded =
                _rightExpanded ??
                constraints.maxWidth >= tokens.sizes.shellSidebarBreakpoint;
            final bottomExpanded =
                _bottomExpanded ??
                constraints.maxHeight >= tokens.sizes.shellFooterBreakpoint;

            return Padding(
              padding: EdgeInsets.all(tokens.spacing.lg),
              child: Column(
                children: [
                  SizedBox(
                    height: tokens.sizes.shellHeaderHeight,
                    child: const _TopNavigation(
                      key: ValueKey('top_navigation'),
                    ),
                  ),
                  SizedBox(height: tokens.spacing.md),
                  Expanded(
                    child: Row(
                      crossAxisAlignment: CrossAxisAlignment.stretch,
                      children: [
                        if (leftExpanded)
                          Expanded(
                            flex: 17,
                            child: _ShellPanel(
                              key: const ValueKey('left_project_panel'),
                              title: '左侧项目区',
                              action: _PanelToggleButton(
                                key: const ValueKey('toggle_left_project'),
                                tooltip: '折叠左侧项目区',
                                icon: Icons.chevron_left_rounded,
                                onPressed: () {
                                  setState(() => _leftExpanded = false);
                                },
                              ),
                              child: const _ProjectNavigationPlaceholder(),
                            ),
                          )
                        else
                          _CollapsedPanelRail(
                            key: const ValueKey('left_project_rail'),
                            tooltip: '展开左侧项目区',
                            icon: Icons.chevron_right_rounded,
                            onPressed: () {
                              setState(() => _leftExpanded = true);
                            },
                          ),
                        SizedBox(width: tokens.spacing.md),
                        Expanded(
                          flex: 68,
                          child: _ShellPanel(
                            key: const ValueKey('center_viewport_panel'),
                            title: '中央视图区',
                            child:
                                widget.viewport ??
                                ViewportCanvas(
                                  key: const ValueKey('main_viewport'),
                                  service: _service,
                                  embedded: true,
                                ),
                          ),
                        ),
                        SizedBox(width: tokens.spacing.md),
                        if (rightExpanded)
                          Expanded(
                            flex: 19,
                            child: _ShellPanel(
                              key: const ValueKey('right_property_panel'),
                              title: '右侧属性区',
                              action: _PanelToggleButton(
                                key: const ValueKey('toggle_right_property'),
                                tooltip: '折叠右侧属性区',
                                icon: Icons.chevron_right_rounded,
                                onPressed: () {
                                  setState(() => _rightExpanded = false);
                                },
                              ),
                              child: const _PropertySkeleton(),
                            ),
                          )
                        else
                          _CollapsedPanelRail(
                            key: const ValueKey('right_property_rail'),
                            tooltip: '展开右侧属性区',
                            icon: Icons.chevron_left_rounded,
                            onPressed: () {
                              setState(() => _rightExpanded = true);
                            },
                          ),
                      ],
                    ),
                  ),
                  SizedBox(height: tokens.spacing.md),
                  SizedBox(
                    height: bottomExpanded
                        ? tokens.sizes.shellFooterHeight
                        : tokens.sizes.shellFooterCollapsedHeight,
                    child: bottomExpanded
                        ? _BottomStatusBar(
                            key: const ValueKey('bottom_status_bar'),
                            service: _service,
                            onToggle: () {
                              setState(() => _bottomExpanded = false);
                            },
                          )
                        : _CollapsedBottomStatusBar(
                            key: const ValueKey('bottom_status_collapsed'),
                            service: _service,
                            onToggle: () {
                              setState(() => _bottomExpanded = true);
                            },
                          ),
                  ),
                ],
              ),
            );
          },
        ),
      ),
    );
  }
}

class _TopNavigation extends StatelessWidget {
  const _TopNavigation({super.key});

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;

    return _Surface(
      child: Row(
        children: [
          Expanded(
            child: Padding(
              padding: EdgeInsets.symmetric(horizontal: tokens.spacing.lg),
              child: Text(
                'GeoScatter 3D',
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: tokens.typography.shellBrand.copyWith(
                  color: tokens.colors.textPrimary,
                ),
              ),
            ),
          ),
          Expanded(
            child: Text(
              '顶部导航区',
              textAlign: TextAlign.center,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: tokens.typography.sectionTitle.copyWith(
                color: tokens.colors.textPrimary,
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _ShellPanel extends StatelessWidget {
  const _ShellPanel({
    super.key,
    required this.title,
    required this.child,
    this.action,
  });

  final String title;
  final Widget child;
  final Widget? action;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;

    return _Surface(
      child: Padding(
        padding: EdgeInsets.all(tokens.spacing.lg),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Expanded(
                  child: Text(
                    title,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: tokens.typography.sectionTitle.copyWith(
                      color: tokens.colors.textPrimary,
                    ),
                  ),
                ),
                if (action != null) ...[
                  SizedBox(width: tokens.spacing.sm),
                  action!,
                ],
              ],
            ),
            SizedBox(height: tokens.spacing.lg),
            Expanded(child: child),
          ],
        ),
      ),
    );
  }
}

class _CollapsedPanelRail extends StatelessWidget {
  const _CollapsedPanelRail({
    super.key,
    required this.tooltip,
    required this.icon,
    required this.onPressed,
  });

  final String tooltip;
  final IconData icon;
  final VoidCallback onPressed;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;

    return SizedBox(
      width: tokens.sizes.shellPanelRailWidth,
      child: _Surface(
        child: Align(
          alignment: Alignment.topCenter,
          child: Padding(
            padding: EdgeInsets.only(top: tokens.spacing.sm),
            child: _PanelToggleButton(
              tooltip: tooltip,
              icon: icon,
              onPressed: onPressed,
            ),
          ),
        ),
      ),
    );
  }
}

class _PanelToggleButton extends StatelessWidget {
  const _PanelToggleButton({
    super.key,
    required this.tooltip,
    required this.icon,
    required this.onPressed,
  });

  final String tooltip;
  final IconData icon;
  final VoidCallback onPressed;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;

    return IconButton(
      tooltip: tooltip,
      onPressed: onPressed,
      padding: EdgeInsets.zero,
      constraints: BoxConstraints.tightFor(
        width: tokens.sizes.tapTarget,
        height: tokens.sizes.tapTarget,
      ),
      iconSize: tokens.sizes.controlIconSize,
      color: tokens.colors.textSecondary,
      icon: Icon(icon),
    );
  }
}

class _ProjectNavigationPlaceholder extends StatelessWidget {
  const _ProjectNavigationPlaceholder();

  static const _items = <String>['项目', '数据', '场景', '成果'];

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;

    return Column(
      children: [
        for (var index = 0; index < _items.length; index++) ...[
          _ProjectNavigationItem(label: _items[index], selected: index == 0),
          if (index != _items.length - 1) SizedBox(height: tokens.spacing.sm),
        ],
      ],
    );
  }
}

class _ProjectNavigationItem extends StatelessWidget {
  const _ProjectNavigationItem({required this.label, required this.selected});

  final String label;
  final bool selected;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    final foreground = selected
        ? tokens.colors.accent
        : tokens.colors.textSecondary;

    return SizedBox(
      height: tokens.sizes.navItemHeight,
      child: DecoratedBox(
        decoration: BoxDecoration(
          color: selected ? tokens.colors.accentSoft : null,
          borderRadius: tokens.radii.controlRadius,
        ),
        child: Padding(
          padding: EdgeInsets.symmetric(horizontal: tokens.spacing.md),
          child: Row(
            children: [
              Icon(
                Icons.folder_outlined,
                size: tokens.sizes.controlIconSize,
                color: foreground,
              ),
              SizedBox(width: tokens.spacing.md),
              Expanded(
                child: Text(
                  label,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: tokens.typography.navLabel.copyWith(color: foreground),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _PropertySkeleton extends StatelessWidget {
  const _PropertySkeleton();

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;

    return Column(
      children: [
        for (var index = 0; index < 3; index++) ...[
          const Expanded(child: _PropertyCard()),
          if (index != 2) SizedBox(height: tokens.spacing.md),
        ],
      ],
    );
  }
}

class _PropertyCard extends StatelessWidget {
  const _PropertyCard();

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;

    return DecoratedBox(
      decoration: BoxDecoration(
        color: tokens.colors.hudSurfaceRaised,
        borderRadius: tokens.radii.controlRadius,
        border: Border.all(
          color: tokens.colors.hudBorder,
          width: tokens.effects.hairline,
        ),
      ),
      child: Padding(
        padding: EdgeInsets.all(tokens.spacing.lg),
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const _PlaceholderBar(widthFactor: 0.76),
            SizedBox(height: tokens.spacing.md),
            const _PlaceholderBar(widthFactor: 0.56),
          ],
        ),
      ),
    );
  }
}

class _BottomStatusBar extends StatelessWidget {
  const _BottomStatusBar({
    super.key,
    required this.service,
    required this.onToggle,
  });

  final GeoScatter3dService service;
  final VoidCallback onToggle;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;

    return ListenableBuilder(
      listenable: service,
      builder: (context, child) => _Surface(
        child: Padding(
          padding: EdgeInsets.symmetric(horizontal: tokens.spacing.lg),
          child: Row(
            children: [
              Text(
                '底部状态区 / LOD',
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: tokens.typography.sectionTitle.copyWith(
                  color: tokens.colors.textPrimary,
                ),
              ),
              SizedBox(width: tokens.spacing.lg),
              Expanded(
                child: TelemetryStatusBar(
                  key: const ValueKey('telemetry_status_bar'),
                  fps: service.fps,
                  frameTimeMs: service.frameTimeMs,
                  renderTimeMs: service.renderTimeMs,
                  visiblePoints: service.pointCount,
                  lodSummary: service.lodSummary,
                  gpuName: service.gpuName,
                  status: service.status,
                  renderPath: service.renderPath,
                ),
              ),
              SizedBox(width: tokens.spacing.sm),
              _PanelToggleButton(
                key: const ValueKey('toggle_bottom_status'),
                tooltip: '折叠底部状态区',
                icon: Icons.keyboard_arrow_down_rounded,
                onPressed: onToggle,
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _CollapsedBottomStatusBar extends StatelessWidget {
  const _CollapsedBottomStatusBar({
    super.key,
    required this.service,
    required this.onToggle,
  });

  final GeoScatter3dService service;
  final VoidCallback onToggle;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;

    return ListenableBuilder(
      listenable: service,
      builder: (context, child) => _Surface(
        child: Padding(
          padding: EdgeInsets.symmetric(horizontal: tokens.spacing.lg),
          child: Row(
            children: [
              Text(
                '底部状态区 / LOD',
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: tokens.typography.sectionTitle.copyWith(
                  color: tokens.colors.textPrimary,
                ),
              ),
              SizedBox(width: tokens.spacing.lg),
              Expanded(
                child: TelemetryStatusBar(
                  key: const ValueKey('telemetry_status_bar'),
                  fps: service.fps,
                  frameTimeMs: service.frameTimeMs,
                  renderTimeMs: service.renderTimeMs,
                  visiblePoints: service.pointCount,
                  lodSummary: service.lodSummary,
                  gpuName: service.gpuName,
                  status: service.status,
                  renderPath: service.renderPath,
                  compact: true,
                  showStatusPill: false,
                ),
              ),
              SizedBox(width: tokens.spacing.sm),
              _PanelToggleButton(
                key: const ValueKey('toggle_bottom_status'),
                tooltip: '展开底部状态区',
                icon: Icons.keyboard_arrow_up_rounded,
                onPressed: onToggle,
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _PlaceholderBar extends StatelessWidget {
  const _PlaceholderBar({this.widthFactor = 1});

  final double widthFactor;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;

    return FractionallySizedBox(
      alignment: Alignment.centerLeft,
      widthFactor: widthFactor,
      child: SizedBox(
        height: tokens.sizes.placeholderBarHeight,
        child: DecoratedBox(
          decoration: BoxDecoration(
            color: tokens.colors.placeholder,
            borderRadius: tokens.radii.pillRadius,
          ),
        ),
      ),
    );
  }
}

class _Surface extends StatelessWidget {
  const _Surface({required this.child});

  final Widget child;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;

    return DecoratedBox(
      decoration: BoxDecoration(
        color: tokens.colors.hudSurfaceRaised,
        borderRadius: tokens.radii.controlRadius,
        border: Border.all(
          color: tokens.colors.hudBorder,
          width: tokens.effects.hairline,
        ),
        boxShadow: [tokens.effects.panelShadow],
      ),
      child: child,
    );
  }
}
