import 'package:flutter/material.dart';

import '../ffi/geoscatter3d_service.dart';
import '../theme/gs3d_tokens.dart';
import 'welcome_ui_keys.dart';

/// 打开已有点云工程对话框。
class OpenProjectDialog extends StatefulWidget {
  const OpenProjectDialog({super.key, required this.service, this.initialPath});

  final GeoScatter3dService service;
  final String? initialPath;

  @override
  State<OpenProjectDialog> createState() => _OpenProjectDialogState();
}

class _OpenProjectDialogState extends State<OpenProjectDialog> {
  late final TextEditingController _pathController;
  String? _errorMessage;
  bool get _isLoading => widget.service.welcomeBusy;

  @override
  void initState() {
    super.initState();
    widget.service.addListener(_changed);
    _pathController = TextEditingController(
      text: widget.initialPath ?? 'data/sample-points.gs3d.bundle',
    );
  }

  @override
  void dispose() {
    widget.service.removeListener(_changed);
    _pathController.dispose();
    super.dispose();
  }

  void _changed() {
    if (mounted) setState(() {});
  }

  void _browseFile() {
    final picked = widget.service.pickFile('open_project');
    if (picked != null && picked.isNotEmpty) {
      setState(() {
        _pathController.text = picked;
        _errorMessage = null;
      });
    }
  }

  void _browseFolder() {
    final picked = widget.service.pickFolder();
    if (picked != null && picked.isNotEmpty) {
      setState(() {
        _pathController.text = picked;
        _errorMessage = null;
      });
    }
  }

  Future<void> _submit() async {
    final path = _pathController.text.trim();
    if (path.isEmpty) {
      setState(() => _errorMessage = '请输入有效的数据文件或目录路径');
      return;
    }

    setState(() => _errorMessage = null);
    final res = await widget.service.executeActionAsync(
      'welcome.open_project',
      {'path': path},
    );
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
      canPop: !_isLoading,
      child: Dialog(
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(Gs3dTokens.radiusXl),
        ),
        backgroundColor: Gs3dTokens.surface,
        surfaceTintColor: Colors.transparent,
        child: Container(
          width: 520,
          padding: const EdgeInsets.all(Gs3dTokens.space24),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  const _DialogIcon(icon: Icons.folder_open_rounded),
                  const SizedBox(width: Gs3dTokens.space12),
                  const Text('打开工程数据包', style: Gs3dType.title2),
                  const Spacer(),
                  IconButton(
                    key: WelcomeUiKeys.openProjectDialogCloseButton,
                    icon: const Icon(
                      Icons.close,
                      size: 18,
                      color: Gs3dTokens.textSecondary,
                    ),
                    onPressed: _isLoading
                        ? null
                        : () => Navigator.of(context).pop(),
                  ),
                ],
              ),
              const SizedBox(height: Gs3dTokens.space16),
              Text(
                '支持 GS3D v2 单体二进制文件 (.gs3d) 或金字塔多分辨率工区包 (.gs3d.bundle 目录)：',
                style: Gs3dType.body.copyWith(color: Gs3dTokens.textSecondary),
              ),
              const SizedBox(height: Gs3dTokens.space16),

              TextField(
                key: WelcomeUiKeys.openProjectDialogPathInput,
                controller: _pathController,
                style: Gs3dType.mono,
                decoration: InputDecoration(
                  labelText: '数据集路径 (文件或 Bundle 文件夹)',
                  labelStyle: const TextStyle(
                    fontSize: 13,
                    color: Gs3dTokens.textSecondary,
                  ),
                  hintText: '例如: data/sample-points.gs3d.bundle 或 sample.gs3d',
                  prefixIcon: const Icon(
                    Icons.description_outlined,
                    size: 18,
                    color: Gs3dTokens.textTertiary,
                  ),
                  filled: true,
                  fillColor: Gs3dTokens.surfaceSunken,
                  border: _inputBorder(Gs3dTokens.border),
                  enabledBorder: _inputBorder(Gs3dTokens.border),
                  focusedBorder: _inputBorder(Gs3dTokens.accent, width: 1.5),
                ),
              ),
              const SizedBox(height: Gs3dTokens.space12),

              Row(
                children: [
                  Expanded(
                    child: OutlinedButton.icon(
                      key: WelcomeUiKeys.openProjectDialogBrowseFileButton,
                      icon: const Icon(
                        Icons.file_open_outlined,
                        size: 15,
                        color: Gs3dTokens.accent,
                      ),
                      label: const Text(
                        '选择文件 (.gs3d)',
                        style: TextStyle(fontSize: 12),
                        overflow: TextOverflow.ellipsis,
                      ),
                      style: OutlinedButton.styleFrom(
                        side: const BorderSide(color: Gs3dTokens.border),
                      ),
                      onPressed: _browseFile,
                    ),
                  ),
                  const SizedBox(width: Gs3dTokens.space8),
                  Expanded(
                    child: OutlinedButton.icon(
                      key: WelcomeUiKeys.openProjectDialogBrowseFolderButton,
                      icon: const Icon(
                        Icons.folder_open_rounded,
                        size: 15,
                        color: Gs3dTokens.accent,
                      ),
                      label: const Text(
                        '选择工区包 (.gs3d.bundle)',
                        style: TextStyle(fontSize: 12),
                        overflow: TextOverflow.ellipsis,
                      ),
                      style: OutlinedButton.styleFrom(
                        side: const BorderSide(color: Gs3dTokens.border),
                      ),
                      onPressed: _browseFolder,
                    ),
                  ),
                ],
              ),
              const SizedBox(height: Gs3dTokens.space12),

              Row(
                children: [
                  Text(
                    '快捷填充:',
                    style: Gs3dType.caption,
                  ),
                  const SizedBox(width: Gs3dTokens.space8),
                  ActionChip(
                    key: WelcomeUiKeys.openProjectDialogSampleChip,
                    avatar: const Icon(
                      Icons.auto_awesome,
                      size: 14,
                      color: Gs3dTokens.accent,
                    ),
                    label: const Text(
                      'sample-points.gs3d.bundle',
                      style: TextStyle(fontSize: 12),
                    ),
                    backgroundColor: Gs3dTokens.surfaceSunken,
                    side: const BorderSide(color: Gs3dTokens.border),
                    onPressed: () {
                      _pathController.text = 'data/sample-points.gs3d.bundle';
                      setState(() => _errorMessage = null);
                    },
                  ),
                ],
              ),

              if (_errorMessage != null) ...[
                const SizedBox(height: Gs3dTokens.space12),
                _InlineError(message: _errorMessage!),
              ],

              const SizedBox(height: Gs3dTokens.space20),
              Row(
                mainAxisAlignment: MainAxisAlignment.end,
                children: [
                  OutlinedButton(
                    key: WelcomeUiKeys.openProjectDialogCancelButton,
                    onPressed: _isLoading
                        ? null
                        : () => Navigator.of(context).pop(),
                    style: OutlinedButton.styleFrom(
                      foregroundColor: Gs3dTokens.textSecondary,
                      side: const BorderSide(color: Gs3dTokens.border),
                    ),
                    child: const Text('取消'),
                  ),
                  const SizedBox(width: Gs3dTokens.space12),
                  FilledButton.icon(
                    key: WelcomeUiKeys.openProjectDialogSubmitButton,
                    onPressed: _isLoading ? null : _submit,
                    icon: _isLoading
                        ? const SizedBox(
                            width: 14,
                            height: 14,
                            child: CircularProgressIndicator(
                              strokeWidth: 2,
                              color: Colors.white,
                            ),
                          )
                        : const Icon(Icons.check, size: 16),
                    label: Text(_isLoading ? '正在转换构建...' : '立即载入'),
                  ),
                ],
              ),
            ],
          ),
        ),
      ),
    );
  }

  OutlineInputBorder _inputBorder(Color color, {double width = 1}) =>
      OutlineInputBorder(
        borderRadius: BorderRadius.circular(Gs3dTokens.radiusMd),
        borderSide: BorderSide(color: color, width: width),
      );
}

/// 对话框标题图标底衬。
class _DialogIcon extends StatelessWidget {
  const _DialogIcon({required this.icon});

  final IconData icon;

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.all(Gs3dTokens.space8),
      decoration: BoxDecoration(
        color: Gs3dTokens.accentSubtle,
        borderRadius: BorderRadius.circular(Gs3dTokens.radiusMd),
      ),
      child: Icon(icon, color: Gs3dTokens.accent, size: 20),
    );
  }
}

class _InlineError extends StatelessWidget {
  const _InlineError({required this.message});

  final String message;

  @override
  Widget build(BuildContext context) {
    return Container(
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
              message,
              style: Gs3dType.caption.copyWith(color: Gs3dTokens.danger),
            ),
          ),
        ],
      ),
    );
  }
}
