import 'dart:async';
import 'dart:io';
import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../ffi/document_picker.dart';
import '../ffi/geoscatter3d_service.dart';
import '../ffi/pipeline_history.dart';
import '../ffi/pipeline_service.dart';
import '../ffi/render_settings.dart';
import '../theme/app_tokens.dart';
import '../widgets/display_panel.dart';
import '../widgets/measurement_panel.dart';
import '../widgets/result_panel.dart';
import '../widgets/pipeline_panel.dart';
import '../widgets/telemetry_hud.dart';
import '../widgets/viewport_canvas.dart';

/// 全屏点云工作台：画布是底层，导航、工具和状态均为按需浮层。
class MainShell extends StatefulWidget {
  const MainShell({
    super.key,
    this.viewport,
    this.service,
    this.pickBundle,
    this.settingsFile,
  });

  /// 测试可注入轻量占位；正式应用默认创建真实 Vulkan 视口。
  final Widget? viewport;

  /// 外部已有状态中枢时可注入；否则由 Shell 负责创建与释放。
  final GeoScatter3dService? service;

  /// 选择工程包目录（测试注入）；默认走平台选择器。
  final Future<String?> Function()? pickBundle;

  /// 显示设置文件（测试注入）；默认放平台应用状态目录。
  final File? settingsFile;

  @override
  State<MainShell> createState() => _MainShellState();
}

class _MainShellState extends State<MainShell> {
  int? _leftDrawerIndex;
  int? _rightDrawerIndex;
  bool? _bottomExpanded;
  late final bool _ownsService;
  late final GeoScatter3dService _service;
  final _sourcePath = TextEditingController();
  final _bundlePath = TextEditingController();
  PipelineService? _pipeline;

  PipelineService _getPipeline() {
    if (_pipeline == null) {
      final pipeline = PipelineService();
      setState(() => _pipeline = pipeline);
    }
    return _pipeline!;
  }

  @override
  void initState() {
    super.initState();
    _ownsService = widget.service == null;
    _service = widget.service ?? GeoScatter3dService();
    if (Platform.isAndroid) _pipeline = PipelineService();
    unawaited(_restorePipelineHistory());
    unawaited(_restoreRenderSettings());
  }

  Future<void> _restoreRenderSettings() async {
    try {
      final file = widget.settingsFile ?? await renderSettingsFile();
      await _service.loadRenderSettings(file);
    } catch (error) {
      if (mounted) {
        ScaffoldMessenger.maybeOf(
          context,
        )?.showSnackBar(SnackBar(content: Text('无法读取显示设置：$error')));
      }
    }
  }

  Future<void> _restorePipelineHistory() async {
    try {
      final history = await readPipelineHistory(await pipelineHistoryFile());
      if (mounted &&
          history != null &&
          _sourcePath.text.isEmpty &&
          _bundlePath.text.isEmpty) {
        _sourcePath.text = history.$1;
        _bundlePath.text = history.$2;
      }
    } catch (error) {
      if (mounted) {
        ScaffoldMessenger.maybeOf(
          context,
        )?.showSnackBar(SnackBar(content: Text('无法读取上次任务：$error')));
      }
    }
  }

  Future<void> _openBundle() async {
    final picker = widget.pickBundle ?? pickBundleDirectory;
    final path = await picker();
    if (path == null || path.isEmpty || !mounted) return;
    final opened = await _service.openBundle(path);
    if (opened || !mounted) return;
    final error = _service.error;
    if (error != null) {
      ScaffoldMessenger.maybeOf(
        context,
      )?.showSnackBar(SnackBar(content: Text(error)));
    }
  }

  @override
  void dispose() {
    _pipeline?.dispose();
    _sourcePath.dispose();
    _bundlePath.dispose();
    if (_ownsService) _service.dispose();
    super.dispose();
  }

  void _toggleLeftDrawer(int index) {
    setState(() {
      _leftDrawerIndex = _leftDrawerIndex == index ? null : index;
      if (_leftDrawerIndex != null) _rightDrawerIndex = null;
    });
  }

  void _toggleRightDrawer(int index) {
    setState(() {
      _rightDrawerIndex = _rightDrawerIndex == index ? null : index;
      if (_rightDrawerIndex != null) _leftDrawerIndex = null;
    });
  }

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    final bottomExpanded = _bottomExpanded ?? true;
    final footerHeight = bottomExpanded
        ? tokens.sizes.shellFooterHeight
        : tokens.sizes.shellFooterCollapsedHeight;
    final rightInset =
        tokens.sizes.shellPanelRailWidth +
        tokens.spacing.sm +
        (_rightDrawerIndex == null
            ? 0.0
            : tokens.sizes.shellDrawerWidth + tokens.spacing.md);

    return Scaffold(
      backgroundColor: tokens.colors.viewport,
      body: Stack(
        children: [
          Positioned.fill(
            child: KeyedSubtree(
              key: const ValueKey('center_viewport_panel'),
              child:
                  widget.viewport ??
                  ViewportCanvas(
                    key: const ValueKey('main_viewport'),
                    service: _service,
                    embedded: true,
                    controlInsets: EdgeInsets.only(
                      right: rightInset,
                      bottom: footerHeight + tokens.spacing.md,
                    ),
                  ),
            ),
          ),
          SafeArea(
            child: Padding(
              padding: EdgeInsets.all(tokens.spacing.md),
              child: LayoutBuilder(
                builder: (context, constraints) {
                  final width = constraints.maxWidth;
                  final height = constraints.maxHeight;
                  final leftDrawerOpen = _leftDrawerIndex != null;
                  final rightDrawerOpen = _rightDrawerIndex != null;
                  final bottomExpanded =
                      _bottomExpanded ??
                      height >= tokens.sizes.shellFooterBreakpoint;
                  final activeFooterHeight = bottomExpanded
                      ? tokens.sizes.shellFooterHeight
                      : tokens.sizes.shellFooterCollapsedHeight;
                  final railTop =
                      tokens.sizes.shellHeaderHeight + tokens.spacing.md;
                  final railBottom = activeFooterHeight + tokens.spacing.md;
                  final drawerWidth = math.max(
                    tokens.sizes.tapTarget * 4,
                    math.min(
                      tokens.sizes.shellDrawerWidth,
                      width -
                          tokens.sizes.shellPanelRailWidth -
                          tokens.spacing.md * 3,
                    ),
                  );

                  return Stack(
                    children: [
                      if (leftDrawerOpen)
                        Positioned(
                          left:
                              tokens.sizes.shellPanelRailWidth +
                              tokens.spacing.sm,
                          top: railTop,
                          bottom: railBottom,
                          width: drawerWidth,
                          child: _ChromeDrawer(
                            key: const ValueKey('left_project_drawer'),
                            title: _workspaceSections[_leftDrawerIndex!].title,
                            onClose: () {
                              setState(() => _leftDrawerIndex = null);
                            },
                            child: switch (_leftDrawerIndex) {
                              0 => PipelinePanel(
                                source: _sourcePath,
                                bundle: _bundlePath,
                                pipeline: _pipeline,
                                getPipeline: _getPipeline,
                              ),
                              3 => ResultPanel(service: _service),
                              _ => _WorkspaceDrawerContent(
                                section: _workspaceSections[_leftDrawerIndex!],
                                service: _service,
                                onOpenBundle: _openBundle,
                              ),
                            },
                          ),
                        ),
                      if (rightDrawerOpen)
                        Positioned(
                          right:
                              tokens.sizes.shellPanelRailWidth +
                              tokens.spacing.sm,
                          top: railTop,
                          bottom: railBottom,
                          width: drawerWidth,
                          child: _ChromeDrawer(
                            key: const ValueKey('right_property_drawer'),
                            title: _rightTools[_rightDrawerIndex!].title,
                            onClose: () {
                              setState(() => _rightDrawerIndex = null);
                            },
                            child: switch (_rightDrawerIndex) {
                              0 => MeasurementPanel(service: _service),
                              2 => DisplayPanel(service: _service),
                              _ => const _PropertySkeleton(),
                            },
                          ),
                        ),
                      Positioned(
                        left: 0,
                        top: railTop,
                        child: _ChromeToolRail(
                          key: const ValueKey('left_tool_rail'),
                          children: [
                            for (
                              var index = 0;
                              index < _workspaceSections.length;
                              index++
                            )
                              _RailButton(
                                key: ValueKey(
                                  'rail_${_workspaceSections[index].keyName}',
                                ),
                                tooltip: _workspaceSections[index].label,
                                icon: _workspaceSections[index].icon,
                                selected: _leftDrawerIndex == index,
                                onPressed: () => _toggleLeftDrawer(index),
                              ),
                          ],
                        ),
                      ),
                      Positioned(
                        right: 0,
                        top: railTop,
                        child: _ChromeToolRail(
                          key: const ValueKey('right_tool_rail'),
                          children: [
                            for (
                              var index = 0;
                              index < _rightTools.length;
                              index++
                            )
                              _RailButton(
                                key: ValueKey(
                                  'rail_${_rightTools[index].keyName}',
                                ),
                                tooltip: _rightTools[index].label,
                                icon: _rightTools[index].icon,
                                selected: _rightDrawerIndex == index,
                                onPressed: () => _toggleRightDrawer(index),
                              ),
                          ],
                        ),
                      ),
                      Positioned(
                        left: 0,
                        right: 0,
                        top: 0,
                        height: tokens.sizes.shellHeaderHeight,
                        child: _TopCommandBar(
                          key: const ValueKey('top_navigation'),
                        ),
                      ),
                      Positioned(
                        left: 0,
                        right: 0,
                        bottom: 0,
                        height: activeFooterHeight,
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
                  );
                },
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _TopCommandBar extends StatelessWidget {
  const _TopCommandBar({super.key});

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;

    return _ChromeSurface(
      child: Padding(
        padding: EdgeInsets.symmetric(horizontal: tokens.spacing.lg),
        child: Row(
          children: [
            Expanded(
              child: Text(
                'GeoScatter 3D',
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: tokens.typography.shellBrand.copyWith(
                  color: tokens.colors.chromeTextPrimary,
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
                  color: tokens.colors.chromeTextPrimary,
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _ChromeToolRail extends StatelessWidget {
  const _ChromeToolRail({super.key, required this.children});

  final List<Widget> children;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;

    return _ChromeSurface(
      child: Padding(
        padding: EdgeInsets.symmetric(
          horizontal: tokens.spacing.xs,
          vertical: tokens.spacing.sm,
        ),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            for (var index = 0; index < children.length; index++) ...[
              children[index],
              if (index != children.length - 1)
                SizedBox(height: tokens.spacing.sm),
            ],
          ],
        ),
      ),
    );
  }
}

class _RailButton extends StatelessWidget {
  const _RailButton({
    super.key,
    required this.tooltip,
    required this.icon,
    required this.selected,
    required this.onPressed,
  });

  final String tooltip;
  final IconData icon;
  final bool selected;
  final VoidCallback onPressed;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;

    return IconButton(
      tooltip: tooltip,
      onPressed: onPressed,
      iconSize: tokens.sizes.controlIconSize,
      constraints: BoxConstraints.tightFor(
        width: tokens.sizes.tapTarget,
        height: tokens.sizes.tapTarget,
      ),
      padding: EdgeInsets.zero,
      style: IconButton.styleFrom(
        foregroundColor: selected
            ? tokens.colors.chromeAccent
            : tokens.colors.chromeTextSecondary,
        backgroundColor: selected ? tokens.colors.chromeAccentSoft : null,
        shape: RoundedRectangleBorder(borderRadius: tokens.radii.controlRadius),
      ),
      icon: Icon(icon),
    );
  }
}

class _ChromeDrawer extends StatelessWidget {
  const _ChromeDrawer({
    super.key,
    required this.title,
    required this.onClose,
    required this.child,
  });

  final String title;
  final VoidCallback onClose;
  final Widget child;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;

    return _ChromeSurface(
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
                      color: tokens.colors.chromeTextPrimary,
                    ),
                  ),
                ),
                SizedBox(width: tokens.spacing.sm),
                IconButton(
                  tooltip: '关闭面板',
                  onPressed: onClose,
                  iconSize: tokens.sizes.controlIconSize,
                  constraints: BoxConstraints.tightFor(
                    width: tokens.sizes.tapTarget,
                    height: tokens.sizes.tapTarget,
                  ),
                  padding: EdgeInsets.zero,
                  color: tokens.colors.chromeTextSecondary,
                  icon: const Icon(Icons.close_rounded),
                ),
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

class _WorkspaceDrawerContent extends StatelessWidget {
  const _WorkspaceDrawerContent({
    required this.section,
    required this.service,
    required this.onOpenBundle,
  });

  final _WorkspaceSection section;
  final GeoScatter3dService service;
  final VoidCallback onOpenBundle;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;

    return ListView(
      children: [
        Text(
          section.description,
          style: tokens.typography.meta.copyWith(
            color: tokens.colors.chromeTextSecondary,
          ),
        ),
        if (section.keyName == 'data') ...[
          SizedBox(height: tokens.spacing.lg),
          _OpenBundleAction(service: service, onPressed: onOpenBundle),
        ],
        SizedBox(height: tokens.spacing.xl),
        const _PlaceholderBar(widthFactor: 1, onChrome: true),
        SizedBox(height: tokens.spacing.md),
        const _PlaceholderBar(widthFactor: 0.72, onChrome: true),
        SizedBox(height: tokens.spacing.xl),
        const _PlaceholderBar(widthFactor: 0.88, onChrome: true),
        SizedBox(height: tokens.spacing.md),
        const _PlaceholderBar(widthFactor: 0.56, onChrome: true),
      ],
    );
  }
}

/// 打开 v3 工程包：装载在引擎工作线程，这里只显示阶段与结果。
class _OpenBundleAction extends StatelessWidget {
  const _OpenBundleAction({required this.service, required this.onPressed});

  final GeoScatter3dService service;
  final VoidCallback onPressed;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;

    return ListenableBuilder(
      listenable: service,
      builder: (context, child) {
        final opening = service.openingDataset;
        final path = service.datasetPath;
        return _ChromeSurface(
          raised: true,
          child: Padding(
            padding: EdgeInsets.all(tokens.spacing.lg),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  '当前工程包',
                  style: tokens.typography.sectionTitle.copyWith(
                    color: tokens.colors.chromeTextPrimary,
                  ),
                ),
                SizedBox(height: tokens.spacing.xs),
                Text(
                  opening
                      ? '正在装载${service.datasetStage.isEmpty ? '' : '（${service.datasetStage}）'}'
                      : (path ?? '尚未打开，显示内置样例'),
                  key: const ValueKey('open_bundle_status'),
                  maxLines: 2,
                  overflow: TextOverflow.ellipsis,
                  style: tokens.typography.meta.copyWith(
                    color: tokens.colors.chromeTextSecondary,
                  ),
                ),
                SizedBox(height: tokens.spacing.md),
                FilledButton.icon(
                  key: const ValueKey('open_bundle_button'),
                  onPressed: opening ? null : onPressed,
                  icon: const Icon(Icons.folder_open_rounded, size: 18),
                  label: Text(opening ? '装载中…' : '打开工程包'),
                ),
              ],
            ),
          ),
        );
      },
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

    return _ChromeSurface(
      raised: true,
      child: Padding(
        padding: EdgeInsets.all(tokens.spacing.lg),
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const _PlaceholderBar(widthFactor: 0.76, onChrome: true),
            SizedBox(height: tokens.spacing.md),
            const _PlaceholderBar(widthFactor: 0.56, onChrome: true),
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
      builder: (context, child) => _ChromeSurface(
        child: Padding(
          padding: EdgeInsets.symmetric(horizontal: tokens.spacing.lg),
          child: Row(
            children: [
              Text(
                '底部状态区 / LOD',
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: tokens.typography.sectionTitle.copyWith(
                  color: tokens.colors.chromeTextPrimary,
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
                  showStatusPill: false,
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
      builder: (context, child) => _ChromeSurface(
        child: Padding(
          padding: EdgeInsets.symmetric(horizontal: tokens.spacing.lg),
          child: Row(
            children: [
              Text(
                '底部状态区 / LOD',
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: tokens.typography.sectionTitle.copyWith(
                  color: tokens.colors.chromeTextPrimary,
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
      color: tokens.colors.chromeTextSecondary,
      icon: Icon(icon),
    );
  }
}

class _PlaceholderBar extends StatelessWidget {
  const _PlaceholderBar({this.widthFactor = 1, this.onChrome = false});

  final double widthFactor;
  final bool onChrome;

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
            color: onChrome
                ? tokens.colors.chromeDivider
                : tokens.colors.placeholder,
            borderRadius: tokens.radii.pillRadius,
          ),
        ),
      ),
    );
  }
}

class _ChromeSurface extends StatelessWidget {
  const _ChromeSurface({required this.child, this.raised = false});

  final Widget child;
  final bool raised;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;

    return DecoratedBox(
      decoration: BoxDecoration(
        color: raised
            ? tokens.colors.chromeSurfaceRaised
            : tokens.colors.chromeSurface,
        borderRadius: tokens.radii.controlRadius,
        border: Border.all(
          color: tokens.colors.chromeBorder,
          width: tokens.effects.hairline,
        ),
        boxShadow: [tokens.effects.chromeShadow],
      ),
      child: child,
    );
  }
}

class _WorkspaceSection {
  const _WorkspaceSection({
    required this.keyName,
    required this.label,
    required this.title,
    required this.description,
    required this.icon,
  });

  final String keyName;
  final String label;
  final String title;
  final String description;
  final IconData icon;
}

class _RightTool {
  const _RightTool({
    required this.keyName,
    required this.label,
    required this.title,
    required this.icon,
  });

  final String keyName;
  final String label;
  final String title;
  final IconData icon;
}

const _workspaceSections = <_WorkspaceSection>[
  _WorkspaceSection(
    keyName: 'project',
    label: '项目',
    title: '项目资源',
    description: '管理当前项目、数据集和场景入口。',
    icon: Icons.folder_outlined,
  ),
  _WorkspaceSection(
    keyName: 'data',
    label: '数据',
    title: '数据图层',
    description: '查看数据源、属性和加载状态。',
    icon: Icons.dataset_outlined,
  ),
  _WorkspaceSection(
    keyName: 'scene',
    label: '场景',
    title: '场景配置',
    description: '管理显示参数、相机和空间范围。',
    icon: Icons.view_in_ar_outlined,
  ),
  _WorkspaceSection(
    keyName: 'result',
    label: '成果',
    title: '成果与导出',
    description: '整理分析结果、截图和导出记录。',
    icon: Icons.description_outlined,
  ),
];

const _rightTools = <_RightTool>[
  _RightTool(
    keyName: 'properties',
    label: '属性',
    title: '属性检查器',
    icon: Icons.tune_rounded,
  ),
  _RightTool(
    keyName: 'layers',
    label: '图层',
    title: '图层控制',
    icon: Icons.layers_outlined,
  ),
  _RightTool(
    keyName: 'display',
    label: '显示',
    title: '显示设置',
    icon: Icons.wb_sunny_outlined,
  ),
];
