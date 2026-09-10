import 'package:flutter/material.dart';

import '../ffi/geoscatter3d_service.dart';
import '../theme/gs3d_tokens.dart';
import 'welcome_ui_keys.dart';

/// 新建三维散点工程对话框（含高级预处理参数与流式构建进度）。
class NewProjectDialog extends StatefulWidget {
  const NewProjectDialog({super.key, required this.service, this.initialPath});

  final GeoScatter3dService service;
  final String? initialPath;

  @override
  State<NewProjectDialog> createState() => _NewProjectDialogState();
}

class _NewProjectDialogState extends State<NewProjectDialog> {
  late final TextEditingController _pathController;
  late final TextEditingController _nameController;
  double _threads = 16;
  bool get _isBuilding => widget.service.welcomeBusy;
  String? _errorMessage;

  bool _showAdvanced = false;
  String _voxelMode = 'xyz';
  double _finestTargetPoints = 50000;
  PreprocessProgressInfo? get _progressInfo => widget.service.latestProgress;

  @override
  void initState() {
    super.initState();
    widget.service.addListener(_changed);
    _pathController = TextEditingController(
      text: widget.initialPath ?? 'examples/sample-points.csv',
    );
    _nameController = TextEditingController(text: 'sample-points');
    if (widget.initialPath != null && widget.initialPath!.isNotEmpty) {
      _onPathChanged(widget.initialPath!);
    }
  }

  @override
  void dispose() {
    widget.service.removeListener(_changed);
    _pathController.dispose();
    _nameController.dispose();
    super.dispose();
  }

  void _onPathChanged(String path) {
    if (path.isNotEmpty) {
      final norm = path.replaceAll('\\', '/');
      final segs = norm.split('/');
      if (segs.isNotEmpty) {
        var base = segs.last;
        if (base.endsWith('.gs3d.bundle')) {
          base = base.substring(0, base.length - 12);
        } else if (base.contains('.')) {
          base = base.substring(0, base.lastIndexOf('.'));
        }
        _nameController.text = base;
      }
    }
    setState(() {});
  }

  void _onBrowse() {
    if (_isBuilding) return;
    final selected = widget.service.pickFile('point_cloud');
    if (selected != null && selected.isNotEmpty) {
      setState(() {
        _pathController.text = selected;
        _onPathChanged(selected);
      });
    }
  }

  void _changed() {
    if (mounted) setState(() {});
  }

  void _onCancelBuild() => widget.service.cancelPreprocess();

  Future<void> _submit() async {
    setState(() => _errorMessage = null);
    final res = await widget.service.executeActionAsync('welcome.new_project', {
      'path': _pathController.text.trim(),
      'name': _nameController.text.trim(),
      'threads': _threads.toInt(),
      'voxel_mode': _voxelMode,
      'finest_target_points': _finestTargetPoints.toInt(),
    });
    if (!mounted) return;
    if (res['success'] == true) {
      Navigator.of(context).pop(true);
    } else {
      setState(() => _errorMessage = res['message'] as String?);
    }
  }

  @override
  Widget build(BuildContext context) {
    return PopScope(
      canPop: !_isBuilding,
      child: Dialog(
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(Gs3dTokens.radiusXl),
        ),
        backgroundColor: Gs3dTokens.surface,
        surfaceTintColor: Colors.transparent,
        child: Container(
          width: 540,
          constraints: BoxConstraints(
            maxHeight: MediaQuery.of(context).size.height * 0.88,
          ),
          padding: const EdgeInsets.all(Gs3dTokens.space24),
          child: SingleChildScrollView(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                // 标题。
                Row(
                  children: [
                    Container(
                      padding: const EdgeInsets.all(Gs3dTokens.space8),
                      decoration: BoxDecoration(
                        color: Gs3dTokens.accentSubtle,
                        borderRadius: BorderRadius.circular(
                          Gs3dTokens.radiusMd,
                        ),
                      ),
                      child: const Icon(
                        Icons.add_chart_rounded,
                        color: Gs3dTokens.accent,
                        size: 20,
                      ),
                    ),
                    const SizedBox(width: Gs3dTokens.space12),
                    const Text('新建三维散点工程', style: Gs3dType.title2),
                    const Spacer(),
                    IconButton(
                      key: WelcomeUiKeys.newProjectDialogCloseButton,
                      icon: const Icon(
                        Icons.close,
                        size: 18,
                        color: Gs3dTokens.textSecondary,
                      ),
                      onPressed: _isBuilding
                          ? null
                          : () => Navigator.of(context).pop(),
                    ),
                  ],
                ),
                const SizedBox(height: Gs3dTokens.space16 + 2),

                // 数据源路径。
                const _FieldLabel('散点数据源路径 (CSV / DAT / GS3D / Bundle)'),
                const SizedBox(height: 6),
                Row(
                  children: [
                    Expanded(
                      child: TextField(
                        key: WelcomeUiKeys.newProjectDialogPathInput,
                        controller: _pathController,
                        onChanged: _onPathChanged,
                        enabled: !_isBuilding,
                        style: Gs3dType.mono,
                        decoration: _denseInput(
                          hint: '例如: examples/sample-points.csv',
                        ),
                      ),
                    ),
                    const SizedBox(width: Gs3dTokens.space8),
                    OutlinedButton.icon(
                      key: WelcomeUiKeys.newProjectDialogBrowseButton,
                      icon: const Icon(Icons.folder_open, size: 16),
                      label: const Text('浏览'),
                      onPressed: _isBuilding ? null : _onBrowse,
                    ),
                  ],
                ),
                const SizedBox(height: Gs3dTokens.space8),

                // 快捷示例 Chip。
                Row(
                  children: [
                    Text('快捷示例:', style: Gs3dType.caption),
                    const SizedBox(width: Gs3dTokens.space8),
                    ActionChip(
                      key: WelcomeUiKeys.newProjectDialogSampleChip,
                      avatar: const Icon(
                        Icons.auto_awesome,
                        size: 14,
                        color: Gs3dTokens.accent,
                      ),
                      label: const Text(
                        'sample-points.csv',
                        style: TextStyle(fontSize: 12),
                      ),
                      backgroundColor: Gs3dTokens.surfaceSunken,
                      side: const BorderSide(color: Gs3dTokens.border),
                      onPressed: _isBuilding
                          ? null
                          : () {
                              _pathController.text = 'examples/sample-points.csv';
                              _onPathChanged(_pathController.text);
                            },
                    ),
                  ],
                ),
                const SizedBox(height: Gs3dTokens.space12 + 2),

                // 工程名称。
                const _FieldLabel('工程名称'),
                const SizedBox(height: 6),
                TextField(
                  key: WelcomeUiKeys.newProjectDialogNameInput,
                  controller: _nameController,
                  onChanged: (v) => setState(() {}),
                  enabled: !_isBuilding,
                  style: const TextStyle(fontSize: 13),
                  decoration: _denseInput(),
                ),
                const SizedBox(height: Gs3dTokens.space16),

                // 预处理线程数。
                Row(
                  mainAxisAlignment: MainAxisAlignment.spaceBetween,
                  children: [
                    const _FieldLabel('预处理构建线程数'),
                    Text(
                      '${_threads.toInt()} 线程',
                      style: Gs3dType.bodyStrong.copyWith(
                        color: Gs3dTokens.accent,
                      ),
                    ),
                  ],
                ),
                Slider(
                  key: WelcomeUiKeys.newProjectDialogThreadsSlider,
                  value: _threads,
                  min: 1,
                  max: 32,
                  divisions: 31,
                  activeColor: Gs3dTokens.accent,
                  onChanged: _isBuilding
                      ? null
                      : (v) => setState(() => _threads = v),
                ),
                const SizedBox(height: 6),

                // 高级参数折叠面板。
                InkWell(
                  key: WelcomeUiKeys.newProjectDialogAdvancedToggle,
                  onTap: _isBuilding
                      ? null
                      : () => setState(() => _showAdvanced = !_showAdvanced),
                  borderRadius: BorderRadius.circular(Gs3dTokens.radiusSm),
                  child: Padding(
                    padding: const EdgeInsets.symmetric(
                      vertical: Gs3dTokens.space4,
                    ),
                    child: Row(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Icon(
                          _showAdvanced
                              ? Icons.keyboard_arrow_down
                              : Icons.keyboard_arrow_right,
                          size: 18,
                          color: Gs3dTokens.textSecondary,
                        ),
                        const SizedBox(width: Gs3dTokens.space4),
                        Text(
                          _showAdvanced
                              ? '收起高级预处理参数'
                              : '展开高级预处理参数 (体素模式/LOD 点数)',
                          style: Gs3dType.caption.copyWith(
                            fontWeight: FontWeight.w500,
                          ),
                        ),
                      ],
                    ),
                  ),
                ),
                if (_showAdvanced) ...[
                  const SizedBox(height: Gs3dTokens.space8),
                  Container(
                    padding: const EdgeInsets.all(Gs3dTokens.space12),
                    decoration: BoxDecoration(
                      color: Gs3dTokens.surfaceSunken,
                      borderRadius: BorderRadius.circular(Gs3dTokens.radiusMd),
                      border: Border.all(color: Gs3dTokens.border),
                    ),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Row(
                          children: [
                            const Expanded(
                              child: Text(
                                '空间体素划分算法',
                                style: TextStyle(
                                  fontSize: 12,
                                  fontWeight: FontWeight.w600,
                                  color: Gs3dTokens.textBody,
                                ),
                              ),
                            ),
                            DropdownButton<String>(
                              key: WelcomeUiKeys
                                  .newProjectDialogVoxelModeDropdown,
                              value: _voxelMode,
                              isDense: true,
                              style: Gs3dType.body.copyWith(fontSize: 12),
                              underline: const SizedBox(),
                              items: const [
                                DropdownMenuItem(
                                  value: 'xyz',
                                  child: Text('标准网格 (XYZ)'),
                                ),
                                DropdownMenuItem(
                                  value: 'xy',
                                  child: Text('平面网格 (XY)'),
                                ),
                              ],
                              onChanged: _isBuilding
                                  ? null
                                  : (v) {
                                      if (v != null) {
                                        setState(() => _voxelMode = v);
                                      }
                                    },
                            ),
                          ],
                        ),
                        const SizedBox(height: Gs3dTokens.space8 + 2),
                        Row(
                          mainAxisAlignment: MainAxisAlignment.spaceBetween,
                          children: [
                            const Text(
                              '最细 LOD 层目标点数',
                              style: TextStyle(
                                fontSize: 12,
                                fontWeight: FontWeight.w600,
                                color: Gs3dTokens.textBody,
                              ),
                            ),
                            Text(
                              '${_finestTargetPoints.toInt()} 点',
                              style: const TextStyle(
                                fontSize: 12,
                                fontWeight: FontWeight.w600,
                                color: Gs3dTokens.accent,
                              ),
                            ),
                          ],
                        ),
                        Slider(
                          key: WelcomeUiKeys
                              .newProjectDialogPointsPerTileSlider,
                          value: _finestTargetPoints,
                          min: 10000,
                          max: 200000,
                          divisions: 19,
                          activeColor: Gs3dTokens.accent,
                          onChanged: _isBuilding
                              ? null
                              : (v) => setState(() => _finestTargetPoints = v),
                        ),
                      ],
                    ),
                  ),
                ],

                // 流式进度卡片。
                if (_isBuilding && _progressInfo != null) ...[
                  const SizedBox(height: Gs3dTokens.space12 + 2),
                  Container(
                    key: WelcomeUiKeys.newProjectDialogProgressIndicator,
                    padding: const EdgeInsets.all(Gs3dTokens.space12),
                    decoration: BoxDecoration(
                      color: Gs3dTokens.accentSubtle,
                      borderRadius: BorderRadius.circular(Gs3dTokens.radiusMd),
                      border: Border.all(color: Gs3dTokens.accentBorder),
                    ),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Row(
                          mainAxisAlignment: MainAxisAlignment.spaceBetween,
                          children: [
                            Row(
                              children: [
                                const SizedBox(
                                  width: 14,
                                  height: 14,
                                  child: CircularProgressIndicator(
                                    strokeWidth: 2,
                                    color: Gs3dTokens.accent,
                                  ),
                                ),
                                const SizedBox(width: Gs3dTokens.space8),
                                Text(
                                  _progressInfo!.stage,
                                  style: const TextStyle(
                                    fontSize: 13,
                                    fontWeight: FontWeight.w600,
                                    color: Gs3dTokens.accent,
                                  ),
                                ),
                              ],
                            ),
                            Text(
                              _progressInfo!.isFinished ? '完成' : '处理中',
                              style: const TextStyle(
                                fontSize: 13,
                                fontWeight: FontWeight.w700,
                                color: Gs3dTokens.accent,
                              ),
                            ),
                          ],
                        ),
                        const SizedBox(height: Gs3dTokens.space8),
                        ClipRRect(
                          borderRadius: BorderRadius.circular(
                            Gs3dTokens.radiusXs,
                          ),
                          child: LinearProgressIndicator(
                            value: _progressInfo!.progress > 0
                                ? _progressInfo!.progress
                                : null,
                            backgroundColor: Gs3dTokens.border,
                            valueColor: const AlwaysStoppedAnimation<Color>(
                              Gs3dTokens.accent,
                            ),
                            minHeight: 6,
                          ),
                        ),
                        const SizedBox(height: Gs3dTokens.space8),
                        Row(
                          mainAxisAlignment: MainAxisAlignment.spaceBetween,
                          children: [
                            Expanded(
                              child: Text(
                                _progressInfo!.detail,
                                style: Gs3dType.caption.copyWith(fontSize: 11),
                                maxLines: 1,
                                overflow: TextOverflow.ellipsis,
                              ),
                            ),
                            const SizedBox(width: Gs3dTokens.space8),
                            Text(
                              '已耗时: ${_progressInfo!.elapsedSeconds}s',
                              style: Gs3dType.caption.copyWith(fontSize: 11),
                            ),
                          ],
                        ),
                      ],
                    ),
                  ),
                ],
                const SizedBox(height: Gs3dTokens.space12 + 2),

                if (_errorMessage != null)
                  Container(
                    margin: const EdgeInsets.only(
                      bottom: Gs3dTokens.space12,
                    ),
                    padding: const EdgeInsets.symmetric(
                      horizontal: Gs3dTokens.space12,
                      vertical: Gs3dTokens.space8,
                    ),
                    decoration: BoxDecoration(
                      color: Gs3dTokens.dangerSubtle,
                      borderRadius: BorderRadius.circular(Gs3dTokens.radiusSm),
                      border: Border.all(color: const Color(0xFFFCA5A5)),
                    ),
                    child: Row(
                      children: [
                        const Icon(
                          Icons.error_outline,
                          size: 16,
                          color: Gs3dTokens.danger,
                        ),
                        const SizedBox(width: Gs3dTokens.space8),
                        Expanded(
                          child: Text(
                            _errorMessage!,
                            style: Gs3dType.caption.copyWith(
                              color: Gs3dTokens.danger,
                            ),
                          ),
                        ),
                      ],
                    ),
                  ),

                // 底部操作按钮。
                Row(
                  mainAxisAlignment: MainAxisAlignment.end,
                  children: [
                    if (_isBuilding)
                      OutlinedButton.icon(
                        key: WelcomeUiKeys.newProjectDialogCancelBuildButton,
                        style: OutlinedButton.styleFrom(
                          foregroundColor: Gs3dTokens.danger,
                          side: const BorderSide(color: Color(0xFFFCA5A5)),
                        ),
                        icon: const Icon(Icons.stop_circle_outlined, size: 16),
                        label: const Text('取消构建'),
                        onPressed: _onCancelBuild,
                      )
                    else
                      TextButton(
                        key: WelcomeUiKeys.newProjectDialogCancelButton,
                        onPressed: () => Navigator.of(context).pop(),
                        child: const Text('取消'),
                      ),
                    const SizedBox(width: Gs3dTokens.space8 + 2),
                    FilledButton.icon(
                      key: WelcomeUiKeys.newProjectDialogSubmitButton,
                      icon: _isBuilding
                          ? const SizedBox(
                              width: 14,
                              height: 14,
                              child: CircularProgressIndicator(
                                strokeWidth: 2,
                                color: Colors.white,
                              ),
                            )
                          : const Icon(Icons.rocket_launch_rounded, size: 16),
                      label: Text(_isBuilding ? '正在构建工区包...' : '构建并载入工作台'),
                      onPressed: _isBuilding ? null : _submit,
                    ),
                  ],
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }

  InputDecoration _denseInput({String? hint}) => InputDecoration(
    hintText: hint,
    contentPadding: const EdgeInsets.symmetric(
      horizontal: Gs3dTokens.space12,
      vertical: 10,
    ),
    border: OutlineInputBorder(
      borderRadius: BorderRadius.circular(Gs3dTokens.radiusSm),
    ),
    isDense: true,
  );
}

class _FieldLabel extends StatelessWidget {
  const _FieldLabel(this.text);

  final String text;

  @override
  Widget build(BuildContext context) {
    return Text(
      text,
      style: const TextStyle(
        fontSize: 13,
        fontWeight: FontWeight.w600,
        color: Gs3dTokens.textBody,
      ),
    );
  }
}
