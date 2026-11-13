import 'dart:io';

import 'package:flutter/material.dart';

import '../ffi/document_picker.dart';
import '../ffi/pipeline_history.dart';
import '../ffi/pipeline_service.dart';
import '../theme/app_tokens.dart';

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
      setState(() => _error = resume ? '请选择工程包目录。' : '请选择输入文件和工程包目录。');
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

  @override
  Widget build(BuildContext context) {
    final pipeline = widget.pipeline;
    return pipeline == null
        ? _content(context, null)
        : ListenableBuilder(
            listenable: pipeline,
            builder: (context, _) => _content(context, pipeline.status),
          );
  }

  Widget _content(BuildContext context, PipelineStatus? status) {
    final tokens = context.tokens;
    final running = status?.state == PipelineState.running;
    final busy = running || _preparing;
    final textStyle = tokens.typography.meta.copyWith(
      color: tokens.colors.chromeTextPrimary,
    );
    final labelStyle = tokens.typography.meta.copyWith(
      color: tokens.colors.chromeTextSecondary,
    );
    Widget pathField(
      String label,
      TextEditingController controller,
      bool directory,
    ) {
      return TextField(
        controller: controller,
        enabled: !busy,
        style: textStyle,
        decoration: InputDecoration(
          labelText: label,
          labelStyle: labelStyle,
          suffixIcon: IconButton(
            tooltip: directory ? '选择工程目录' : '选择点云文件',
            onPressed: busy ? null : () => _pick(directory: directory),
            color: tokens.colors.chromeTextPrimary,
            icon: Icon(directory ? Icons.folder_open : Icons.file_open),
          ),
        ),
      );
    }

    String stageLabel(String stage) => switch (stage) {
      'fingerprint' => '校验输入',
      'stats' => '统计数据',
      'points' => '生成点块',
      _ => '准备中',
    };
    final message = switch (status?.state) {
      PipelineState.running => '正在${stageLabel(status!.stage)}',
      PipelineState.done => '已完成：${status!.points} 个点，${status.chunks} 个点块',
      PipelineState.failed => '处理失败：${status!.error}',
      PipelineState.cancelled => '任务已取消，可从已完成的阶段恢复。',
      _ => '选择 CSV 或 DAT 文件，创建离线工程包。',
    };

    return ListView(
      children: [
        Text(message, style: labelStyle),
        if (running) ...[
          SizedBox(height: tokens.spacing.md),
          const LinearProgressIndicator(),
          if (status!.error.isNotEmpty) Text(status.error, style: labelStyle),
        ],
        SizedBox(height: tokens.spacing.lg),
        pathField('输入文件', widget.source, false),
        SizedBox(height: tokens.spacing.md),
        pathField('工程包目录', widget.bundle, true),
        SizedBox(height: tokens.spacing.lg),
        if (running)
          OutlinedButton(onPressed: pipelineCancel, child: const Text('取消任务'))
        else ...[
          FilledButton(
            onPressed: _preparing ? null : () => _run(resume: false),
            child: const Text('创建工程包'),
          ),
          TextButton(
            onPressed: _preparing ? null : () => _run(resume: true),
            child: const Text('恢复未完成的任务'),
          ),
        ],
        if (_error != null) ...[
          SizedBox(height: tokens.spacing.md),
          Text(_error!, style: textStyle.copyWith(color: tokens.colors.error)),
        ],
        SizedBox(height: tokens.spacing.lg),
        Text(
          Platform.isAndroid
              ? '取消将在当前阶段结束后生效。任务可在后台继续；系统中断后可恢复。'
              : '取消将在当前阶段结束后生效。退出应用会停止当前任务；可选择工程包目录恢复。',
          style: labelStyle,
        ),
      ],
    );
  }

  Future<void> pipelineCancel() async {
    try {
      await widget.pipeline!.cancel();
      if (mounted) setState(() => _error = null);
    } catch (error) {
      if (mounted) setState(() => _error = '无法取消任务：$error');
    }
  }
}
