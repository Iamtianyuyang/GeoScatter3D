import 'package:flutter/material.dart';
import '../ffi/geoscatter3d_service.dart';
import '../theme/app_theme.dart';
import 'welcome_ui_keys.dart';

class NewProjectDialog extends StatefulWidget {
  final GeoScatter3dService service;
  final String? initialPath;

  const NewProjectDialog({
    super.key,
    required this.service,
    this.initialPath,
  });

  @override
  State<NewProjectDialog> createState() => _NewProjectDialogState();
}

class _NewProjectDialogState extends State<NewProjectDialog> {
  late final TextEditingController _pathController;
  late final TextEditingController _nameController;
  double _threads = 16;
  bool _isBuilding = false;
  String? _errorMessage;

  bool _showAdvanced = false;
  String _voxelMode = 'xyz';
  double _maxPointsPerTile = 50000;
  PreprocessProgressInfo? _progressInfo;

  @override
  void initState() {
    super.initState();
    _pathController = TextEditingController(text: widget.initialPath ?? 'examples/sample-points.csv');
    _nameController = TextEditingController(text: 'sample-points');
    if (widget.initialPath != null && widget.initialPath!.isNotEmpty) {
      _onPathChanged(widget.initialPath!);
    }
  }

  @override
  void dispose() {
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

  void _onCancelBuild() {
    widget.service.cancelPreprocess();
    setState(() {
      _isBuilding = false;
      _errorMessage = '构建已由用户取消';
    });
  }

  Future<void> _submit() async {
    final path = _pathController.text.trim();
    if (path.isEmpty) {
      setState(() => _errorMessage = '请输入或选择有效的数据文件路径');
      return;
    }

    setState(() {
      _isBuilding = true;
      _errorMessage = null;
      _progressInfo = const PreprocessProgressInfo(
        stage: '准备启动构建管线',
        progress: 0.05,
        detail: '正在校验数据文件与工区参数...',
        elapsedSeconds: 0,
      );
    });

    await Future.delayed(const Duration(milliseconds: 40));

    final res = await widget.service.buildAndLoadProjectAsync(
      path: path,
      name: _nameController.text.trim(),
      threads: _threads.toInt(),
      voxelMode: _voxelMode,
      maxPointsPerTile: _maxPointsPerTile.toInt(),
      onProgress: (info) {
        if (mounted) {
          setState(() => _progressInfo = info);
        }
      },
    );

    if (mounted) {
      setState(() => _isBuilding = false);
      if (res['success'] == true) {
        Navigator.of(context).pop(true);
      } else {
        setState(() => _errorMessage = res['message'] as String? ?? '加载或转换数据失败，请检查文件格式是否有效');
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    final hasConflict = widget.service.checkProjectNameConflict(_nameController.text.trim());

    return Dialog(
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
      backgroundColor: AppTheme.surface,
      surfaceTintColor: Colors.transparent,
      child: Container(
        width: 540,
        constraints: BoxConstraints(maxHeight: MediaQuery.of(context).size.height * 0.88),
        padding: const EdgeInsets.all(24),
        child: SingleChildScrollView(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              // 标题
              Row(
                children: [
                  Container(
                    padding: const EdgeInsets.all(8),
                    decoration: BoxDecoration(
                      color: AppTheme.primaryBlueBg,
                      borderRadius: BorderRadius.circular(8),
                    ),
                    child: const Icon(Icons.add_chart_rounded, color: AppTheme.primaryBlue, size: 20),
                  ),
                  const SizedBox(width: 12),
                  const Text(
                    '新建三维散点工程',
                    style: TextStyle(
                      fontSize: 18,
                      fontWeight: FontWeight.w700,
                      color: AppTheme.textTitle,
                    ),
                  ),
                  const Spacer(),
                  IconButton(
                    key: WelcomeUiKeys.newProjectDialogCloseButton,
                    icon: const Icon(Icons.close, size: 18, color: AppTheme.textDim),
                    onPressed: _isBuilding ? null : () => Navigator.of(context).pop(),
                  ),
                ],
              ),
              const SizedBox(height: 18),

              // 数据源路径
              const Text('散点数据源路径 (CSV / DAT / GS3D / Bundle)',
                  style: TextStyle(fontSize: 13, fontWeight: FontWeight.w600, color: AppTheme.textBody)),
              const SizedBox(height: 6),
              Row(
                children: [
                  Expanded(
                    child: TextField(
                      key: WelcomeUiKeys.newProjectDialogPathInput,
                      controller: _pathController,
                      onChanged: _onPathChanged,
                      enabled: !_isBuilding,
                      style: const TextStyle(fontSize: 13, fontFamily: 'Consolas'),
                      decoration: InputDecoration(
                        hintText: '例如: examples/sample-points.csv',
                        contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
                        border: OutlineInputBorder(borderRadius: BorderRadius.circular(6)),
                        isDense: true,
                      ),
                    ),
                  ),
                  const SizedBox(width: 8),
                  OutlinedButton.icon(
                    key: WelcomeUiKeys.newProjectDialogBrowseButton,
                    style: OutlinedButton.styleFrom(
                      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
                      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(6)),
                    ),
                    icon: const Icon(Icons.folder_open, size: 16),
                    label: const Text('浏览'),
                    onPressed: _isBuilding ? null : _onBrowse,
                  ),
                ],
              ),
              const SizedBox(height: 8),

              // 快捷填充 Chip
              Row(
                children: [
                  const Text('快捷示例:', style: TextStyle(fontSize: 12, color: AppTheme.textDim)),
                  const SizedBox(width: 8),
                  ActionChip(
                    key: WelcomeUiKeys.newProjectDialogSampleChip,
                    avatar: const Icon(Icons.auto_awesome, size: 14, color: AppTheme.primaryBlue),
                    label: const Text('sample-points.csv', style: TextStyle(fontSize: 12)),
                    backgroundColor: AppTheme.surfaceMuted,
                    side: const BorderSide(color: AppTheme.border),
                    onPressed: _isBuilding
                        ? null
                        : () {
                            _pathController.text = 'examples/sample-points.csv';
                            _onPathChanged(_pathController.text);
                          },
                  ),
                ],
              ),
              const SizedBox(height: 14),

              // 工程名称
              const Text('工程名称',
                  style: TextStyle(fontSize: 13, fontWeight: FontWeight.w600, color: AppTheme.textBody)),
              const SizedBox(height: 6),
              TextField(
                key: WelcomeUiKeys.newProjectDialogNameInput,
                controller: _nameController,
                onChanged: (v) => setState(() {}),
                enabled: !_isBuilding,
                style: const TextStyle(fontSize: 13),
                decoration: InputDecoration(
                  contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
                  border: OutlineInputBorder(borderRadius: BorderRadius.circular(6)),
                  isDense: true,
                ),
              ),
              if (hasConflict && _nameController.text.trim().isNotEmpty)
                Padding(
                  padding: const EdgeInsets.only(top: 6),
                  child: Row(
                    children: const [
                      Icon(Icons.warning_amber_rounded, size: 15, color: Color(0xFFD97706)),
                      SizedBox(width: 6),
                      Text(
                        '工作空间已存在同名工程，构建将会覆盖已有产物',
                        style: TextStyle(fontSize: 12, color: Color(0xFFD97706)),
                      ),
                    ],
                  ),
                ),
              const SizedBox(height: 16),

              // 预处理线程数
              Row(
                mainAxisAlignment: MainAxisAlignment.spaceBetween,
                children: [
                  const Text('预处理构建线程数',
                      style: TextStyle(fontSize: 13, fontWeight: FontWeight.w600, color: AppTheme.textBody)),
                  Text('${_threads.toInt()} 线程',
                      style: const TextStyle(fontSize: 13, fontWeight: FontWeight.w600, color: AppTheme.primaryBlue)),
                ],
              ),
              Slider(
                key: WelcomeUiKeys.newProjectDialogThreadsSlider,
                value: _threads,
                min: 1,
                max: 32,
                divisions: 31,
                activeColor: AppTheme.primaryBlue,
                onChanged: _isBuilding ? null : (v) => setState(() => _threads = v),
              ),
              const SizedBox(height: 6),

              // 高级参数折叠面板
              InkWell(
                key: WelcomeUiKeys.newProjectDialogAdvancedToggle,
                onTap: _isBuilding ? null : () => setState(() => _showAdvanced = !_showAdvanced),
                borderRadius: BorderRadius.circular(6),
                child: Padding(
                  padding: const EdgeInsets.symmetric(vertical: 4),
                  child: Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      Icon(
                        _showAdvanced ? Icons.keyboard_arrow_down : Icons.keyboard_arrow_right,
                        size: 18,
                        color: AppTheme.textDim,
                      ),
                      const SizedBox(width: 4),
                      Text(
                        _showAdvanced ? '收起高级预处理参数' : '展开高级预处理参数 (体素模式/分块上限)',
                        style: const TextStyle(fontSize: 12, color: AppTheme.textDim, fontWeight: FontWeight.w500),
                      ),
                    ],
                  ),
                ),
              ),
              if (_showAdvanced) ...[
                const SizedBox(height: 8),
                Container(
                  padding: const EdgeInsets.all(12),
                  decoration: BoxDecoration(
                    color: AppTheme.surfaceMuted,
                    borderRadius: BorderRadius.circular(8),
                    border: Border.all(color: AppTheme.border),
                  ),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Row(
                        children: [
                          const Expanded(
                            child: Text(
                              '空间体素划分算法',
                              style: TextStyle(fontSize: 12, fontWeight: FontWeight.w600, color: AppTheme.textBody),
                            ),
                          ),
                          DropdownButton<String>(
                            key: WelcomeUiKeys.newProjectDialogVoxelModeDropdown,
                            value: _voxelMode,
                            isDense: true,
                            style: const TextStyle(fontSize: 12, color: AppTheme.textBody),
                            underline: const SizedBox(),
                            items: const [
                              DropdownMenuItem(value: 'xyz', child: Text('标准网格 (XYZ)')),
                              DropdownMenuItem(value: 'morton', child: Text('Morton 空间编码')),
                              DropdownMenuItem(value: 'octree', child: Text('自适应八叉树 (Octree)')),
                            ],
                            onChanged: _isBuilding
                                ? null
                                : (v) {
                                    if (v != null) setState(() => _voxelMode = v);
                                  },
                          ),
                        ],
                      ),
                      const SizedBox(height: 10),
                      Row(
                        mainAxisAlignment: MainAxisAlignment.spaceBetween,
                        children: [
                          const Text(
                            '单瓦片目标点数上限 (LOD Tile)',
                            style: TextStyle(fontSize: 12, fontWeight: FontWeight.w600, color: AppTheme.textBody),
                          ),
                          Text(
                            '${_maxPointsPerTile.toInt()} 点',
                            style: const TextStyle(fontSize: 12, fontWeight: FontWeight.w600, color: AppTheme.primaryBlue),
                          ),
                        ],
                      ),
                      Slider(
                        key: WelcomeUiKeys.newProjectDialogPointsPerTileSlider,
                        value: _maxPointsPerTile,
                        min: 10000,
                        max: 200000,
                        divisions: 19,
                        activeColor: AppTheme.primaryBlue,
                        onChanged: _isBuilding ? null : (v) => setState(() => _maxPointsPerTile = v),
                      ),
                    ],
                  ),
                ),
              ],

              // 流式进度卡片
              if (_isBuilding && _progressInfo != null) ...[
                const SizedBox(height: 14),
                Container(
                  key: WelcomeUiKeys.newProjectDialogProgressIndicator,
                  padding: const EdgeInsets.all(12),
                  decoration: BoxDecoration(
                    color: AppTheme.primaryBlueBg.withValues(alpha: 0.5),
                    borderRadius: BorderRadius.circular(8),
                    border: Border.all(color: AppTheme.primaryBlue.withValues(alpha: 0.2)),
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
                                child: CircularProgressIndicator(strokeWidth: 2, color: AppTheme.primaryBlue),
                              ),
                              const SizedBox(width: 8),
                              Text(
                                _progressInfo!.stage,
                                style: const TextStyle(fontSize: 13, fontWeight: FontWeight.w600, color: AppTheme.primaryBlue),
                              ),
                            ],
                          ),
                          Text(
                            '${((_progressInfo!.progress) * 100).toInt()}%',
                            style: const TextStyle(fontSize: 13, fontWeight: FontWeight.w700, color: AppTheme.primaryBlue),
                          ),
                        ],
                      ),
                      const SizedBox(height: 8),
                      ClipRRect(
                        borderRadius: BorderRadius.circular(4),
                        child: LinearProgressIndicator(
                          value: _progressInfo!.progress > 0 ? _progressInfo!.progress : null,
                          backgroundColor: AppTheme.border,
                          valueColor: const AlwaysStoppedAnimation<Color>(AppTheme.primaryBlue),
                          minHeight: 6,
                        ),
                      ),
                      const SizedBox(height: 8),
                      Row(
                        mainAxisAlignment: MainAxisAlignment.spaceBetween,
                        children: [
                          Expanded(
                            child: Text(
                              _progressInfo!.detail,
                              style: const TextStyle(fontSize: 11, color: AppTheme.textDim),
                              maxLines: 1,
                              overflow: TextOverflow.ellipsis,
                            ),
                          ),
                          const SizedBox(width: 8),
                          Text(
                            '已耗时: ${_progressInfo!.elapsedSeconds}s',
                            style: const TextStyle(fontSize: 11, color: AppTheme.textDim),
                          ),
                        ],
                      ),
                    ],
                  ),
                ),
              ],
              const SizedBox(height: 14),

              if (_errorMessage != null)
                Container(
                  margin: const EdgeInsets.only(bottom: 12),
                  padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                  decoration: BoxDecoration(
                    color: const Color(0xFFFEF2F2),
                    borderRadius: BorderRadius.circular(6),
                    border: Border.all(color: const Color(0xFFFCA5A5)),
                  ),
                  child: Row(
                    children: [
                      const Icon(Icons.error_outline, size: 16, color: Color(0xFFDC2626)),
                      const SizedBox(width: 8),
                      Expanded(child: Text(_errorMessage!, style: const TextStyle(fontSize: 12, color: Color(0xFFDC2626)))),
                    ],
                  ),
                ),

              // 底部操作按钮
              Row(
                mainAxisAlignment: MainAxisAlignment.end,
                children: [
                  if (_isBuilding)
                    OutlinedButton.icon(
                      key: WelcomeUiKeys.newProjectDialogCancelBuildButton,
                      style: OutlinedButton.styleFrom(
                        foregroundColor: const Color(0xFFDC2626),
                        side: const BorderSide(color: Color(0xFFFCA5A5)),
                        padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 10),
                        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(6)),
                      ),
                      icon: const Icon(Icons.stop_circle_outlined, size: 16),
                      label: const Text('取消构建'),
                      onPressed: _onCancelBuild,
                    )
                  else
                    TextButton(
                      key: WelcomeUiKeys.newProjectDialogCancelButton,
                      onPressed: () => Navigator.of(context).pop(),
                      child: const Text('取消', style: TextStyle(color: AppTheme.textDim)),
                    ),
                  const SizedBox(width: 10),
                  ElevatedButton.icon(
                    key: WelcomeUiKeys.newProjectDialogSubmitButton,
                    style: ElevatedButton.styleFrom(
                      backgroundColor: AppTheme.primaryBlue,
                      foregroundColor: Colors.white,
                      padding: const EdgeInsets.symmetric(horizontal: 18, vertical: 10),
                      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(6)),
                    ),
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
    );
  }
}
