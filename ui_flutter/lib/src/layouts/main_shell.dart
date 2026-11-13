import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';

import '../ffi/document_picker.dart';
import '../ffi/geoscatter3d_service.dart';
import '../ffi/pipeline_history.dart';
import '../ffi/pipeline_service.dart';
import '../ffi/render_settings.dart';
import '../theme/app_tokens.dart';
import '../ui/format.dart';
import '../ui/ui_kit.dart';
import '../widgets/camera_gizmo.dart';
import '../widgets/camera_panel.dart';
import '../widgets/colormap_legend.dart';
import '../widgets/diagnostics_panel.dart';
import '../widgets/measurement_panel.dart';
import '../widgets/pipeline_panel.dart';
import '../widgets/project_panel.dart';
import '../widgets/render_panel.dart';
import '../widgets/result_panel.dart';
import '../widgets/status_strip.dart';
import '../widgets/viewport_canvas.dart';

/// 工作台主框架：顶栏（数据身份 + 主操作）、左侧工具轨 + 面板、视口、状态条。
///
/// 布局而不是浮层：面板展开时视口让位（IDE 式的稳定几何），
/// 这样点云的投影中心不会因为开一个抽屉而跳一下。
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

enum ShellSection { project, pipeline, render, camera, measure, output, diagnostics }

class _SectionSpec {
  const _SectionSpec(this.title, this.subtitle, this.icon, this.keyName);

  final String title;
  final String subtitle;
  final IconData icon;
  final String keyName;
}

const Map<ShellSection, _SectionSpec> _sections = {
  ShellSection.project: _SectionSpec(
    '工程',
    '数据集与最近项目',
    Icons.inventory_2_outlined,
    'project',
  ),
  ShellSection.pipeline: _SectionSpec(
    '预处理',
    'CSV / DAT → 工程包',
    Icons.tune,
    'pipeline',
  ),
  ShellSection.render: _SectionSpec(
    '渲染',
    '配色、点形与值域',
    Icons.palette_outlined,
    'render',
  ),
  ShellSection.camera: _SectionSpec(
    '相机',
    '正交视角与缩放',
    Icons.videocam_outlined,
    'camera',
  ),
  ShellSection.measure: _SectionSpec(
    '测量',
    '拾取点距离与高差',
    Icons.straighten,
    'measure',
  ),
  ShellSection.output: _SectionSpec(
    '成果',
    '导出点云与帧',
    Icons.ios_share,
    'output',
  ),
  ShellSection.diagnostics: _SectionSpec(
    '诊断',
    '显卡、缓存与快照',
    Icons.monitor_heart_outlined,
    'diagnostics',
  ),
};

class _MainShellState extends State<MainShell> {
  final _sourcePath = TextEditingController();
  final _bundlePath = TextEditingController();
  late final bool _ownsService;
  late final GeoScatter3dService _service;
  ShellSection? _section = ShellSection.project;
  PipelineService? _pipeline;

  double get _panelWidth {
    final width = MediaQuery.sizeOf(context).width;
    return width < 520 ? (width - 56).clamp(200.0, 320.0) : 320.0;
  }

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
      _toast('无法读取显示设置：$error');
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
      _toast('无法读取上次任务：$error');
    }
  }

  void _toast(String message) {
    if (!mounted) return;
    ScaffoldMessenger.maybeOf(context)
        ?.showSnackBar(SnackBar(content: Text(message)));
  }

  Future<void> _openBundle() async {
    final picker = widget.pickBundle ?? pickBundleDirectory;
    final path = await picker();
    if (path == null || path.isEmpty) return;
    await _openPath(path);
  }

  Future<void> _openPath(String path) async {
    final opened = await _service.openBundle(path);
    if (opened || !mounted) return;
    final error = _service.error;
    if (error != null) _toast(error);
  }

  @override
  void dispose() {
    _pipeline?.dispose();
    _sourcePath.dispose();
    _bundlePath.dispose();
    if (_ownsService) _service.dispose();
    super.dispose();
  }

  void _toggle(ShellSection section) {
    setState(() => _section = _section == section ? null : section);
  }

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    final section = _section;
    return Scaffold(
      backgroundColor: tokens.colors.ink,
      body: SafeArea(
        child: Column(
          children: [
            _TopBar(
              key: const ValueKey('top_navigation'),
              service: _service,
              onOpenBundle: _openBundle,
              onOpenPipeline: () {
                setState(() => _section = ShellSection.pipeline);
              },
            ),
            Expanded(
              child: Row(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  _ToolRail(
                    key: const ValueKey('left_tool_rail'),
                    selected: section,
                    onSelect: _toggle,
                    diagnosticsBusy: _service.exporting,
                  ),
                  AnimatedContainer(
                    duration: tokens.motion.base,
                    curve: tokens.motion.ease,
                    width: section == null ? 0 : _panelWidth,
                    child: ClipRect(
                      child: OverflowBox(
                        alignment: Alignment.centerLeft,
                        minWidth: _panelWidth,
                        maxWidth: _panelWidth,
                        child: Container(
                          decoration: BoxDecoration(
                            color: tokens.colors.panel,
                            border: Border(
                              right: BorderSide(color: tokens.colors.line),
                            ),
                          ),
                          child: section == null
                              ? const SizedBox.shrink()
                              : Column(
                                  key: const ValueKey('side_panel'),
                                  crossAxisAlignment:
                                      CrossAxisAlignment.stretch,
                                  children: [
                                    PanelHeader(
                                      title: _sections[section]!.title,
                                      subtitle: _sections[section]!.subtitle,
                                      onClose: () =>
                                          setState(() => _section = null),
                                    ),
                                    Expanded(
                                      child: _panelFor(section),
                                    ),
                                  ],
                                ),
                        ),
                      ),
                    ),
                  ),
                  Expanded(
                    child: Stack(
                      key: const ValueKey('center_viewport_panel'),
                      children: [
                        Positioned.fill(
                          child: widget.viewport ??
                              ViewportCanvas(
                                key: const ValueKey('main_viewport'),
                                service: _service,
                              ),
                        ),
                        _ViewportOverlays(
                          service: _service,
                          onOpenBundle: _openBundle,
                        ),
                      ],
                    ),
                  ),
                ],
              ),
            ),
            _StatusBar(service: _service, onOpenBundle: _openBundle),
          ],
        ),
      ),
    );
  }

  Widget _panelFor(ShellSection section) {
    return switch (section) {
      ShellSection.project => ProjectPanel(
          service: _service,
          onOpenBundle: _openBundle,
          onOpenRecent: _openPath,
        ),
      ShellSection.pipeline => PipelinePanel(
          source: _sourcePath,
          bundle: _bundlePath,
          pipeline: _pipeline,
          getPipeline: _getPipeline,
        ),
      ShellSection.render => RenderPanel(service: _service),
      ShellSection.camera => CameraPanel(service: _service),
      ShellSection.measure => MeasurementPanel(service: _service),
      ShellSection.output => ResultPanel(service: _service),
      ShellSection.diagnostics => DiagnosticsPanel(service: _service),
    };
  }
}

/// 顶栏：品牌、当前数据身份、主操作与 GPU。
class _TopBar extends StatelessWidget {
  const _TopBar({
    super.key,
    required this.service,
    required this.onOpenBundle,
    required this.onOpenPipeline,
  });

  final GeoScatter3dService service;
  final VoidCallback onOpenBundle;
  final VoidCallback onOpenPipeline;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    // 大字号时整条主栏一起长高，而不是把文字挤成溢出条纹。
    final scale = MediaQuery.textScalerOf(context).scale(1).clamp(1.0, 2.0);
    return Container(
      height: 46 + (scale - 1) * 22,
      padding: const EdgeInsets.symmetric(horizontal: 12),
      decoration: BoxDecoration(
        color: tokens.colors.ink,
        border: Border(bottom: BorderSide(color: tokens.colors.line)),
      ),
      child: ListenableBuilder(
        listenable: service,
        builder: (context, _) {
          final path = service.datasetPath;
          final title = path == null || path.isEmpty
              ? (service.ready ? '内置样例' : '未打开数据集')
              : fileName(path);
          return Row(
            children: [
              // 品牌标记：一个方角 + 信号色描边，和面板的 1px 语言一致。
              Container(
                width: 18,
                height: 18,
                decoration: BoxDecoration(
                  borderRadius: BorderRadius.circular(3),
                  border: Border.all(color: tokens.colors.signal, width: 1.5),
                ),
                child: Center(
                  child: Container(
                    width: 6,
                    height: 6,
                    decoration: BoxDecoration(
                      color: tokens.colors.signal,
                      borderRadius: BorderRadius.circular(1),
                    ),
                  ),
                ),
              ),
              const SizedBox(width: 8),
              Text(
                'GeoScatter 3D',
                style: tokens.text.bodyStrong.copyWith(
                  color: tokens.colors.textHigh,
                  fontSize: 13,
                  letterSpacing: 0.01,
                ),
              ),
              const SizedBox(width: 12),
              Container(width: 1, height: 16, color: tokens.colors.line),
              const SizedBox(width: 12),
              Flexible(
                child: Column(
                  mainAxisAlignment: MainAxisAlignment.center,
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      title,
                      key: const ValueKey('topbar_dataset'),
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: tokens.text.body.copyWith(
                        color: tokens.colors.textHigh,
                        fontSize: 12.5,
                      ),
                    ),
                    Text(
                      path == null || path.isEmpty
                          ? '离线点云工作台'
                          : shortPath(path),
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: tokens.text.meta.copyWith(
                        color: tokens.colors.textLow,
                        fontSize: 10,
                      ),
                    ),
                  ],
                ),
              ),
              const SizedBox(width: 12),
              _GpuChip(name: service.gpuName),
              const SizedBox(width: 8),
              IconButton(
                tooltip: '预处理',
                onPressed: onOpenPipeline,
                visualDensity: VisualDensity.compact,
                iconSize: 18,
                color: tokens.colors.textMid,
                icon: const Icon(Icons.tune),
              ),
              const SizedBox(width: 4),
              SizedBox(
                width: 118,
                child: AppButton(
                  label: '打开工程包',
                  icon: Icons.inventory_2_outlined,
                  tone: ButtonTone.primary,
                  dense: true,
                  buttonKey: const ValueKey('open_bundle_button'),
                  onPressed: onOpenBundle,
                ),
              ),
            ],
          );
        },
      ),
    );
  }
}

class _GpuChip extends StatelessWidget {
  const _GpuChip({required this.name});

  final String name;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    final label = name.isEmpty ? 'GPU —' : name;
    return Tooltip(
      message: label,
      child: Container(
        constraints: const BoxConstraints(maxWidth: 190),
        height: 24,
        padding: const EdgeInsets.symmetric(horizontal: 8),
        decoration: BoxDecoration(
          border: Border.all(color: tokens.colors.line),
          borderRadius: tokens.radii.smAll,
        ),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(Icons.memory, size: 12, color: tokens.colors.textLow),
            const SizedBox(width: 6),
            Flexible(
              child: Text(
                label,
                key: const ValueKey('open_bundle_status'),
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: tokens.text.meta.copyWith(
                  fontSize: 10.5,
                  color: tokens.colors.textMid,
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

/// 左侧工具轨：一次只展开一个面板。
class _ToolRail extends StatelessWidget {
  const _ToolRail({
    super.key,
    required this.selected,
    required this.onSelect,
    required this.diagnosticsBusy,
  });

  final ShellSection? selected;
  final ValueChanged<ShellSection> onSelect;
  final bool diagnosticsBusy;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    return Container(
      width: 52,
      decoration: BoxDecoration(
        color: tokens.colors.ink,
        border: Border(right: BorderSide(color: tokens.colors.line)),
      ),
      child: Column(
        children: [
          const SizedBox(height: 8),
          for (final section in ShellSection.values)
            _RailButton(
              key: ValueKey('rail_${_sections[section]!.keyName}'),
              spec: _sections[section]!,
              selected: selected == section,
              onPressed: () => onSelect(section),
            ),
          const Spacer(),
          _RailButton(
            spec: const _SectionSpec('帮助', '', Icons.help_outline, 'help'),
            selected: false,
            onPressed: () => showDialog<void>(
              context: context,
              builder: (context) => const _ShortcutSheet(),
            ),
          ),
          const SizedBox(height: 8),
        ],
      ),
    );
  }
}

class _RailButton extends StatelessWidget {
  const _RailButton({
    super.key,
    required this.spec,
    required this.selected,
    required this.onPressed,
  });

  final _SectionSpec spec;
  final bool selected;
  final VoidCallback onPressed;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    return Tooltip(
      message: spec.title,
      child: InkWell(
        onTap: onPressed,
        child: Container(
          height: 44,
          width: 52,
          color: selected ? tokens.colors.signalSoft : Colors.transparent,
          child: Row(
            children: [
              Container(
                width: 2,
                height: 44,
                color: selected ? tokens.colors.signal : Colors.transparent,
              ),
              Expanded(
                child: Icon(
                  spec.icon,
                  size: 17,
                  color: selected ? tokens.colors.signal : tokens.colors.textMid,
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

/// 视口浮层：视图轴、缩放、色标与测量读数。
class _ViewportOverlays extends StatelessWidget {
  const _ViewportOverlays({required this.service, required this.onOpenBundle});

  final GeoScatter3dService service;
  final VoidCallback onOpenBundle;

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: service,
      builder: (context, _) {
        final tokens = context.tokens;
        final ready = service.ready;
        final points = service.pickedPoints;
        final distance = service.measurementDistance;
        final height = service.measurementHeightDelta;
        final info = service.datasetInfo;
        final valueMin = info['value_min'];
        final valueMax = info['value_max'];
        final hasDataset = service.datasetPath != null &&
            (info['point_count'] as num? ?? 0) > 0;

        void action(String name, [Map<String, double> values = const {}]) {
          if (!ready) return;
          service.executeAction(name, values);
        }

        return Stack(
          children: [
            // 空工作台：只在没有数据且没有错误时出现，给一条明确的路。
            if (!hasDataset && service.error == null && !service.ready)
              Center(
                child: Container(
                  width: 300,
                  padding: const EdgeInsets.all(20),
                  decoration: BoxDecoration(
                    color: tokens.colors.panel.withValues(alpha: 0.9),
                    border: Border.all(color: tokens.colors.line),
                    borderRadius: tokens.radii.mdAll,
                  ),
                  child: Column(
                    mainAxisSize: MainAxisSize.min,
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        '没有打开的数据',
                        style: tokens.text.panelTitle.copyWith(
                          color: tokens.colors.textHigh,
                        ),
                      ),
                      const SizedBox(height: 6),
                      Text(
                        '打开预处理好的 .gs3d 工程包，或先建一个新的。',
                        style: tokens.text.meta.copyWith(
                          color: tokens.colors.textMid,
                        ),
                      ),
                      const SizedBox(height: 14),
                      AppButton(
                        label: '打开工程包',
                        icon: Icons.inventory_2_outlined,
                        tone: ButtonTone.primary,
                        onPressed: onOpenBundle,
                      ),
                    ],
                  ),
                ),
              ),
            Positioned(
              top: 12,
              right: 12,
              child: CameraGizmo(
                enabled: ready,
                onAxisSelected: service.gizmoAxis,
                onReset: () => action('camera.reset'),
              ),
            ),
            Positioned(
              right: 12,
              top: 150,
              child: ZoomControls(
                enabled: ready,
                onZoomIn: () => action('camera.input', {'scrollY': 1.0}),
                onZoomOut: () => action('camera.input', {'scrollY': -1.0}),
              ),
            ),
            if (hasDataset)
              Positioned(
                left: 12,
                bottom: 12,
                child: ColormapLegend(
                  colormapIndex: service.renderSettings.colormapIndex,
                  minLabel: formatNumber(valueMin as num?),
                  maxLabel: formatNumber(valueMax as num?),
                  unit: (info['attributes'] is List &&
                          (info['attributes'] as List).isNotEmpty &&
                          (info['attributes'] as List).first is Map)
                      ? ((info['attributes'] as List).first as Map)['unit']
                          ?.toString()
                      : null,
                ),
              ),
            if (points.length == 2 && distance != null)
              Positioned(
                bottom: 12,
                left: 0,
                right: 0,
                child: Center(
                  child: Container(
                    padding:
                        const EdgeInsets.symmetric(horizontal: 14, vertical: 8),
                    decoration: BoxDecoration(
                      color: tokens.colors.panel.withValues(alpha: 0.92),
                      border: Border.all(color: tokens.colors.line),
                      borderRadius: tokens.radii.mdAll,
                    ),
                    child: Row(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Icon(Icons.straighten,
                            size: 14, color: tokens.colors.signal),
                        const SizedBox(width: 8),
                        Text(
                          '距离 ${distance.toStringAsFixed(3)}',
                          key: const ValueKey('overlay_measure_distance'),
                          style: tokens.text.numeric.copyWith(
                            fontSize: 13,
                            color: tokens.colors.textHigh,
                          ),
                        ),
                        Container(
                          width: 1,
                          height: 12,
                          margin: const EdgeInsets.symmetric(horizontal: 10),
                          color: tokens.colors.line,
                        ),
                        Text(
                          '高差 ${height! >= 0 ? '+' : ''}'
                          '${height.toStringAsFixed(3)}',
                          key: const ValueKey('overlay_measure_height'),
                          style: tokens.text.numeric.copyWith(
                            fontSize: 13,
                            color: tokens.colors.textMid,
                          ),
                        ),
                      ],
                    ),
                  ),
                ),
              ),
            if (points.length == 1)
              Positioned(
                bottom: 12,
                left: 0,
                right: 0,
                child: Center(
                  child: Text(
                    '已拾取起点，再点一个点完成测量',
                    style: tokens.text.meta.copyWith(
                      color: tokens.colors.textMid,
                      fontSize: 11,
                    ),
                  ),
                ),
              ),
          ],
        );
      },
    );
  }
}

class _StatusBar extends StatelessWidget {
  const _StatusBar({required this.service, required this.onOpenBundle});

  final GeoScatter3dService service;
  final VoidCallback onOpenBundle;

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: service,
      builder: (context, _) {
        final state = service.error != null
            ? 2
            : service.ready
                ? 0
                : 1;
        return StatusStrip(
          state: state,
          sourcePoints: service.sourcePointCount,
          drawnPoints: service.pointCount,
          fps: service.fps,
          frameMs: service.frameTimeMs,
          renderMs: service.renderTimeMs,
          gpuName: service.gpuName,
          lodSummary: service.lodSummary,
          datasetName: service.datasetPath == null
              ? null
              : fileName(service.datasetPath!),
          error: service.error,
          onOpenBundle: onOpenBundle,
        );
      },
    );
  }
}

/// 快捷键与手势速查。
class _ShortcutSheet extends StatelessWidget {
  const _ShortcutSheet();

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    const rows = [
      ('单指拖动 / 左键拖动', '旋转'),
      ('双指拖动 / 中键拖动', '平移'),
      ('双指捏合 / 滚轮', '缩放'),
      ('单击', '拾取测量点'),
      ('Esc', '关闭面板'),
    ];
    return Dialog(
      backgroundColor: tokens.colors.panel,
      shape: RoundedRectangleBorder(
        borderRadius: tokens.radii.mdAll,
        side: BorderSide(color: tokens.colors.line),
      ),
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 420),
        child: Padding(
          padding: const EdgeInsets.all(20),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                '操作',
                style: tokens.text.panelTitle.copyWith(
                  color: tokens.colors.textHigh,
                ),
              ),
              const SizedBox(height: 12),
              for (final (gesture, effect) in rows)
                Padding(
                  padding: const EdgeInsets.symmetric(vertical: 5),
                  child: Row(
                    children: [
                      Expanded(
                        child: Text(
                          gesture,
                          style: tokens.text.body.copyWith(
                            color: tokens.colors.textMid,
                          ),
                        ),
                      ),
                      Text(
                        effect,
                        style: tokens.text.meta.copyWith(
                          color: tokens.colors.textLow,
                        ),
                      ),
                    ],
                  ),
                ),
              const SizedBox(height: 16),
              Text(
                '数据与预处理都在本机完成，不上传、不联网。',
                style: tokens.text.meta.copyWith(color: tokens.colors.textLow),
              ),
            ],
          ),
        ),
      ),
    );
  }
}
