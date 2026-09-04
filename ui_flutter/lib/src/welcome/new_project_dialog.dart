import 'package:flutter/material.dart';
import '../ffi/geoscatter3d_service.dart';
import '../theme/app_theme.dart';
import 'welcome_ui_keys.dart';

class NewProjectDialog extends StatefulWidget {
  final GeoScatter3dService service;

  const NewProjectDialog({
    super.key,
    required this.service,
  });

  @override
  State<NewProjectDialog> createState() => _NewProjectDialogState();
}

class _NewProjectDialogState extends State<NewProjectDialog> {
  final _pathController = TextEditingController(text: 'examples/sample-points.csv');
  final _nameController = TextEditingController(text: 'sample-points');
  double _threads = 16;
  bool _isBuilding = false;
  String? _errorMessage;

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

  Future<void> _submit() async {
    final path = _pathController.text.trim();
    if (path.isEmpty) {
      setState(() => _errorMessage = '请输入或选择有效的数据文件路径');
      return;
    }

    setState(() {
      _isBuilding = true;
      _errorMessage = null;
    });

    // 让出事件循环以便渲染 Loading 动画与进度反馈
    await Future.delayed(const Duration(milliseconds: 50));

    final res = widget.service.executeAction('welcome.new_project', {
      'path': path,
      'name': _nameController.text.trim(),
      'threads': _threads.toInt(),
    });

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
    return Dialog(
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
      backgroundColor: AppTheme.surface,
      surfaceTintColor: Colors.transparent,
      child: Container(
        width: 520,
        padding: const EdgeInsets.all(24),
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
              enabled: !_isBuilding,
              style: const TextStyle(fontSize: 13),
              decoration: InputDecoration(
                contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
                border: OutlineInputBorder(borderRadius: BorderRadius.circular(6)),
                isDense: true,
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
                TextButton(
                  key: WelcomeUiKeys.newProjectDialogCancelButton,
                  onPressed: _isBuilding ? null : () => Navigator.of(context).pop(),
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
    );
  }
}
