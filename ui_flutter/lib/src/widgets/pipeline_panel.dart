import 'dart:io';

import 'package:flutter/material.dart';

import '../ffi/document_picker.dart';
import '../ffi/pipeline_history.dart';
import '../ffi/pipeline_service.dart';
import '../theme/app_tokens.dart';
import '../ui/format.dart';
import '../ui/ui_kit.dart';

/// 预处理面板：CSV/DAT → 工程包。
///
/// 三个阶段（校验输入 / 统计数据 / 生成点块）是这条流水线真实的边界，
/// 所以进度就按这三段显示，取消也只在阶段结束时生效——说清楚比转圈诚实。
class PipelinePanel extends StatefulWidget {
  const PipelinePanel({
    super.key,
    required this.source,
    required this.bundle,
    required this.pipeline,
    required this.getPipeline,
  });

  final TextEditingController source;
  final TextEditingController bundle;
  final PipelineService? pipeline;
  final PipelineService Function() getPipeline;

  @override
  State<PipelinePanel> createState() => _PipelinePanelState();
}

class _PipelinePanelState extends State<PipelinePanel> {
  String? _error;
  bool _preparing = false;

  static const _stages = [
    ('fingerprint', '校验输入'),
    ('stats', '统计数据'),
    ('points', '生成点块'),
  ];

  Future<void> _pick({required bool directory}) async {
    try {
      final path = directory
          ? await pickBundleDirectory()
          : await pickPointFile();
      if (!mounted || path == null) return;
      setState(() {
        _error = null;
        if (directory) {
          widget.bundle.text = path;
        } else {
          widget.source.text = path;
          if (widget.bundle.text.isEmpty) {
            widget.bundle.text = '$path.gs3d.bundle';
          }
        }
      });
    } catch (error) {
      if (mounted) setState(() => _error = '选择失败：$error');
    }
  }

  Future<void> _run({required bool resume}) async {
    final source = widget.source.text.trim();
    final bundle = widget.bundle.text.trim();
    if (bundle.isEmpty || (!resume && source.isEmpty)) {
      setState(
        () => _error = resume ? '请选择工程包目录。' : '请选择输入文件和工程包目录。',
      );
      return;
    }
    setState(() => _preparing = true);
    try {
      await savePipelineHistory(await pipelineHistoryFile(), source, bundle);
      if (!mounted) return;
      final pipeline = widget.getPipeline();
      if (resume) {
        await pipeline.resume(bundle);
      } else {
        await pipeline.start(source, bundle);
      }
      if (mounted) setState(() => _error = null);
    } catch (error) {
      if (mounted) setState(() => _error = '无法启动任务：$error');
    } finally {
      if (mounted) setState(() => _preparing = false);
    }
  }

  Future<void> _cancel() async {
    try {
      await widget.pipeline!.cancel();
      if (mounted) setState(() => _error = null);
    } catch (error) {
      if (mounted) setState(() => _error = '无法取消任务：$error');
    }
  }

  @override
  Widget build(BuildContext context) {
    final pipeline = widget.pipeline;
    return pipeline == null
        ? _content(null)
        : ListenableBuilder(
            listenable: pipeline,
            builder: (context, _) => _content(pipeline.status),
          );
  }

  Widget _content(PipelineStatus? status) {
    final tokens = context.tokens;
    final state = status?.state;
    final running = state == PipelineState.running;
    final busy = running || _preparing;
    final stageIndex = status == null
        ? -1
        : _stages.indexWhere((stage) => stage.$1 == status.stage);
    final done = state == PipelineState.done;
    final failed = state == PipelineState.failed;

    final headline = switch (state) {
      PipelineState.running => '正在${_stageLabel(status!.stage)}',
      PipelineState.done =>
        '完成 · ${formatCount(status!.points)} 点 · ${status.chunks} 块',
      PipelineState.failed => '处理失败',
      PipelineState.cancelled => '已取消，可从完成的阶段恢复',
      _ => 'CSV / DAT → 离线工程包',
    };

    return ListView(
      padding: const EdgeInsets.only(bottom: 24),
      children: [
        PanelSection(
          title: '任务',
          trailing: running
              ? Text(
                  '${status!.chunks} 块',
                  style: tokens.text.meta.copyWith(
                    color: tokens.colors.textLow,
                  ),
                )
              : null,
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Text(
                headline,
                key: const ValueKey('pipeline_headline'),
                style: tokens.text.body.copyWith(
                  color: failed
                      ? tokens.colors.alert
                      : done
                          ? tokens.colors.signal
                          : tokens.colors.textHigh,
                  fontWeight: FontWeight.w700,
                ),
              ),
              const SizedBox(height: 10),
              _StageTrack(
                stages: _stages,
                activeIndex: running ? stageIndex : (done ? _stages.length : -1),
                failed: failed,
              ),
              if (running) ...[
                const SizedBox(height: 10),
                const ThinProgress(value: null),
                const SizedBox(height: 10),
                AppButton(
                  label: '取消任务',
                  icon: Icons.stop_circle_outlined,
                  tone: ButtonTone.danger,
                  buttonKey: const ValueKey('pipeline_cancel'),
                  onPressed: _cancel,
                ),
                const SizedBox(height: 8),
                Text(
                  '取消在当前阶段结束后生效，已提交的块会保留。',
                  style: tokens.text.meta.copyWith(
                    color: tokens.colors.textLow,
                    fontSize: 10.5,
                  ),
                ),
              ],
              if (failed)
                Padding(
                  padding: const EdgeInsets.only(top: 10),
                  child: NoticeBanner(
                    text: status!.error,
                    tone: ButtonTone.danger,
                    icon: Icons.error_outline,
                  ),
                ),
            ],
          ),
        ),
        const Hairline(),
        PanelSection(
          title: '输入',
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              _PathField(
                label: '源文件',
                help: 'CSV 或 DAT，空格/逗号分隔，可带表头。',
                controller: widget.source,
                enabled: !busy,
                buttonKey: const ValueKey('pick_source_button'),
                tooltip: '选择点云文件',
                icon: Icons.file_open_outlined,
                onPressed: () => _pick(directory: false),
              ),
              const SizedBox(height: 12),
              _PathField(
                label: '工程包目录',
                help: '输出目录；已有的可恢复任务也在这里。',
                controller: widget.bundle,
                enabled: !busy,
                buttonKey: const ValueKey('pick_bundle_button'),
                tooltip: '选择工程目录',
                icon: Icons.folder_open_outlined,
                onPressed: () => _pick(directory: true),
              ),
            ],
          ),
        ),
        if (!running) ...[
          const Hairline(),
          PanelSection(
            title: '执行',
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                AppButton(
                  label: _preparing ? '正在启动…' : '创建工程包',
                  icon: Icons.play_arrow_rounded,
                  tone: ButtonTone.primary,
                  buttonKey: const ValueKey('pipeline_start'),
                  onPressed: _preparing ? null : () => _run(resume: false),
                ),
                const SizedBox(height: 8),
                AppButton(
                  label: '恢复未完成的任务',
                  icon: Icons.restore,
                  buttonKey: const ValueKey('pipeline_resume'),
                  onPressed: _preparing ? null : () => _run(resume: true),
                ),
                const SizedBox(height: 12),
                Text(
                  Platform.isAndroid
                      ? '任务在后台前台服务里继续；系统回收进程后可用同一目录恢复。'
                      : '退出应用会停止当前任务；重新打开同一个工程包目录即可从完成的阶段继续。',
                  style: tokens.text.meta.copyWith(
                    color: tokens.colors.textLow,
                    fontSize: 10.5,
                    height: 1.5,
                  ),
                ),
              ],
            ),
          ),
        ],
        if (_error != null)
          PanelSection(
            child: NoticeBanner(
              text: _error!,
              tone: ButtonTone.danger,
              icon: Icons.error_outline,
            ),
          ),
      ],
    );
  }

  String _stageLabel(String stage) => switch (stage) {
    'fingerprint' => '校验输入',
    'stats' => '统计数据',
    'points' => '生成点块',
    _ => '准备中',
  };
}

class _PathField extends StatelessWidget {
  const _PathField({
    required this.label,
    required this.help,
    required this.controller,
    required this.enabled,
    required this.buttonKey,
    required this.tooltip,
    required this.icon,
    required this.onPressed,
  });

  final String label;
  final String help;
  final TextEditingController controller;
  final bool enabled;
  final Key buttonKey;
  final String tooltip;
  final IconData icon;
  final VoidCallback onPressed;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        FieldLabel(text: label, help: help),
        const SizedBox(height: 4),
        Row(
          children: [
            Expanded(
              child: TextField(
                controller: controller,
                enabled: enabled,
                style: tokens.text.numeric.copyWith(
                  fontSize: 12,
                  color: tokens.colors.textHigh,
                ),
                decoration: InputDecoration(
                  isDense: true,
                  filled: true,
                  fillColor: tokens.colors.raised,
                  contentPadding:
                      const EdgeInsets.symmetric(horizontal: 8, vertical: 9),
                  hintText: '未选择',
                  hintStyle: tokens.text.meta.copyWith(
                    color: tokens.colors.textLow,
                  ),
                  border: OutlineInputBorder(
                    borderRadius: tokens.radii.smAll,
                    borderSide: BorderSide(color: tokens.colors.line),
                  ),
                  enabledBorder: OutlineInputBorder(
                    borderRadius: tokens.radii.smAll,
                    borderSide: BorderSide(color: tokens.colors.line),
                  ),
                ),
              ),
            ),
            const SizedBox(width: 6),
            SizedBox(
              width: 34,
              height: 34,
              child: IconButton(
                key: buttonKey,
                tooltip: tooltip,
                onPressed: enabled ? onPressed : null,
                padding: EdgeInsets.zero,
                iconSize: 17,
                color: tokens.colors.textMid,
                icon: Icon(icon),
              ),
            ),
          ],
        ),
      ],
    );
  }
}

/// 三段式阶段指示：已过 / 当前 / 未到。
class _StageTrack extends StatelessWidget {
  const _StageTrack({
    required this.stages,
    required this.activeIndex,
    required this.failed,
  });

  final List<(String, String)> stages;
  final int activeIndex;
  final bool failed;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    return Row(
      children: [
        for (var i = 0; i < stages.length; i++) ...[
          if (i > 0)
            Expanded(
              child: Container(
                height: 1,
                margin: const EdgeInsets.symmetric(horizontal: 6),
                color: i <= activeIndex
                    ? tokens.colors.signal.withValues(alpha: 0.5)
                    : tokens.colors.line,
              ),
            ),
          Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Container(
                width: 8,
                height: 8,
                decoration: BoxDecoration(
                  shape: BoxShape.circle,
                  color: i < activeIndex
                      ? tokens.colors.signal
                      : i == activeIndex
                          ? (failed ? tokens.colors.alert : tokens.colors.signal)
                          : Colors.transparent,
                  border: Border.all(
                    color: i <= activeIndex
                        ? (failed && i == activeIndex
                            ? tokens.colors.alert
                            : tokens.colors.signal)
                        : tokens.colors.lineStrong,
                  ),
                ),
              ),
              const SizedBox(height: 5),
              Text(
                stages[i].$2,
                style: tokens.text.meta.copyWith(
                  fontSize: 10,
                  color: i <= activeIndex
                      ? tokens.colors.textMid
                      : tokens.colors.textLow,
                ),
              ),
            ],
          ),
        ],
      ],
    );
  }
}
