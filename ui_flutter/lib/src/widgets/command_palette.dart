import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import '../ffi/geoscatter3d_service.dart';
import '../theme/gs3d_form_factor.dart';
import '../theme/gs3d_tokens.dart';

/// 全局交互式命令面板 (Command Palette - Ctrl+P)
class CommandPalette extends StatefulWidget {
  final GeoScatter3dService service;

  const CommandPalette({super.key, required this.service});

  @override
  State<CommandPalette> createState() => _CommandPaletteState();
}

class _CommandPaletteState extends State<CommandPalette> {
  final TextEditingController _searchController = TextEditingController();
  final FocusNode _inputFocusNode = FocusNode();
  final ScrollController _scrollController = ScrollController();
  int _selectedIndex = 0;
  List<UiActionDescriptor> _filteredActions = [];

  @override
  void initState() {
    super.initState();
    _filteredActions = widget.service.getAvailableActions();
    _searchController.addListener(_onSearchChanged);
  }

  @override
  void dispose() {
    _searchController.removeListener(_onSearchChanged);
    _searchController.dispose();
    _inputFocusNode.dispose();
    _scrollController.dispose();
    super.dispose();
  }

  void _onSearchChanged() {
    final query = _searchController.text.trim().toLowerCase();
    final all = widget.service.getAvailableActions();
    setState(() {
      if (query.isEmpty) {
        _filteredActions = all;
      } else {
        _filteredActions = all.where((action) {
          final idMatch = action.id.toLowerCase().contains(query);
          final nameMatch = action.name.toLowerCase().contains(query);
          final descMatch = action.description.toLowerCase().contains(query);
          final catMatch = action.category.toLowerCase().contains(query);
          return idMatch || nameMatch || descMatch || catMatch;
        }).toList();
      }
      _selectedIndex = 0;
    });
  }

  void _executeSelected() {
    if (_filteredActions.isEmpty) return;
    final action =
        _filteredActions[_selectedIndex.clamp(0, _filteredActions.length - 1)];
    _executeAction(action);
  }

  Future<void> _executeAction(UiActionDescriptor action) async {
    widget.service.toggleCommandPalette(false);

    // 特殊动作交互：如果需要参数则提供快捷默认调用
    Map<String, dynamic>? params;
    if (action.id == 'workbench.export') {
      params = {
        'path': 'export_${DateTime.now().millisecondsSinceEpoch}.ply',
        'format': 'ply',
      };
    }

    final res = await widget.service.executeActionAsync(
      action.id,
      params ?? {},
    );
    final msg = (res['message'] as String?) ?? '${action.name} 执行完成';

    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Row(
            children: [
              Icon(
                res['success'] == true
                    ? Icons.check_circle_rounded
                    : Icons.info_outline,
                size: 16,
                color: res['success'] == true
                    ? Gs3dTokens.success
                    : Colors.orangeAccent,
              ),
              const SizedBox(width: 8),
              Expanded(child: Text(msg)),
            ],
          ),
          duration: const Duration(seconds: 2),
          behavior: SnackBarBehavior.floating,
        ),
      );
    }
  }

  void _handleKeyDown(KeyEvent event) {
    if (event is KeyDownEvent) {
      if (event.logicalKey == LogicalKeyboardKey.arrowDown) {
        if (_filteredActions.isNotEmpty) {
          setState(() {
            _selectedIndex = (_selectedIndex + 1) % _filteredActions.length;
          });
          _scrollToSelected();
        }
      } else if (event.logicalKey == LogicalKeyboardKey.arrowUp) {
        if (_filteredActions.isNotEmpty) {
          setState(() {
            _selectedIndex =
                (_selectedIndex - 1 + _filteredActions.length) %
                _filteredActions.length;
          });
          _scrollToSelected();
        }
      } else if (event.logicalKey == LogicalKeyboardKey.enter) {
        _executeSelected();
      } else if (event.logicalKey == LogicalKeyboardKey.escape) {
        widget.service.toggleCommandPalette(false);
      }
    }
  }

  void _scrollToSelected() {
    if (!_scrollController.hasClients) return;
    final itemHeight = 54.0;
    final targetOffset = _selectedIndex * itemHeight;
    _scrollController.animateTo(
      targetOffset.clamp(0.0, _scrollController.position.maxScrollExtent),
      duration: const Duration(milliseconds: 100),
      curve: Curves.easeOut,
    );
  }

  @override
  Widget build(BuildContext context) {
    final isCompact = context.gs3dFormFactor == Gs3dFormFactorSlot.compact;

    return KeyboardListener(
      focusNode: FocusNode(),
      onKeyEvent: _handleKeyDown,
      child: Stack(
        children: [
          // 1. 半透明暗色遮罩，点击关闭
          Positioned.fill(
            child: GestureDetector(
              onTap: () => widget.service.toggleCommandPalette(false),
              child: Container(color: Gs3dMaterial.scrimStrong),
            ),
          ),

          // 2. 悬浮居中命令面板卡片
          Align(
            alignment: const Alignment(0, -0.65),
            child: Container(
              key: const ValueKey('command_palette_modal'),
              width: isCompact ? double.infinity : 620,
              constraints: const BoxConstraints(maxHeight: 460),
              margin: EdgeInsets.symmetric(
                horizontal: isCompact ? Gs3dTokens.space8 : Gs3dTokens.space20,
              ),
              decoration: BoxDecoration(
                color: Gs3dTokens.surface,
                borderRadius: BorderRadius.circular(10),
                border: Border.all(color: Gs3dTokens.border, width: 1),
                boxShadow: const [
                  BoxShadow(
                    color: Gs3dMaterial.scrimStrong,
                    blurRadius: 24,
                    offset: Offset(0, 10),
                  ),
                ],
              ),
              child: ClipRRect(
                borderRadius: BorderRadius.circular(10),
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    // 搜索输入框
                    Container(
                      padding: const EdgeInsets.symmetric(
                        horizontal: 14,
                        vertical: 10,
                      ),
                      decoration: const BoxDecoration(
                        border: Border(
                          bottom: BorderSide(
                            color: Gs3dTokens.border,
                            width: 1,
                          ),
                        ),
                        color: Gs3dTokens.surfaceSunken,
                      ),
                      child: Row(
                        children: [
                          const Icon(
                            Icons.search_rounded,
                            size: 20,
                            color: Gs3dTokens.accent,
                          ),
                          const SizedBox(width: 10),
                          Expanded(
                            child: TextField(
                              key: const ValueKey('command_palette_input'),
                              controller: _searchController,
                              focusNode: _inputFocusNode,
                              autofocus: true,
                              style: const TextStyle(
                                fontSize: 14,
                                fontWeight: FontWeight.w500,
                                color: Gs3dTokens.textPrimary,
                              ),
                              decoration: const InputDecoration(
                                hintText:
                                    '输入指令名称、动作 ID 或关键词 (如: 测距, 截图, 视角, 统计)...',
                                hintStyle: TextStyle(
                                  fontSize: 13,
                                  color: Gs3dTokens.textSecondary,
                                ),
                                border: InputBorder.none,
                                isDense: true,
                                contentPadding: EdgeInsets.zero,
                              ),
                            ),
                          ),
                          if (_searchController.text.isNotEmpty)
                            IconButton(
                              icon: const Icon(
                                Icons.close,
                                size: 16,
                                color: Gs3dTokens.textSecondary,
                              ),
                              onPressed: () => _searchController.clear(),
                              splashRadius: 14,
                              padding: EdgeInsets.zero,
                              constraints: const BoxConstraints(),
                            ),
                          const SizedBox(width: 6),
                          Container(
                            padding: const EdgeInsets.symmetric(
                              horizontal: 6,
                              vertical: 2,
                            ),
                            decoration: BoxDecoration(
                              color: Gs3dTokens.borderStrong.withAlpha(60),
                              borderRadius: BorderRadius.circular(4),
                            ),
                            child: const Text(
                              'ESC 退出',
                              style: TextStyle(
                                fontSize: 10,
                                color: Gs3dTokens.textSecondary,
                                fontWeight: FontWeight.w600,
                              ),
                            ),
                          ),
                        ],
                      ),
                    ),

                    // 指令匹配列表
                    Flexible(
                      child: _filteredActions.isEmpty
                          ? Container(
                              padding: const EdgeInsets.symmetric(vertical: 36),
                              alignment: Alignment.center,
                              child: Column(
                                mainAxisSize: MainAxisSize.min,
                                children: const [
                                  Icon(
                                    Icons.search_off_rounded,
                                    size: 32,
                                    color: Gs3dTokens.textSecondary,
                                  ),
                                  SizedBox(height: 8),
                                  Text(
                                    '未找到匹配的指令',
                                    style: TextStyle(
                                      fontSize: 13,
                                      color: Gs3dTokens.textSecondary,
                                    ),
                                  ),
                                ],
                              ),
                            )
                          : ListView.builder(
                              controller: _scrollController,
                              shrinkWrap: true,
                              padding: const EdgeInsets.symmetric(vertical: 4),
                              itemCount: _filteredActions.length,
                              itemBuilder: (context, index) {
                                final action = _filteredActions[index];
                                final isSelected = index == _selectedIndex;

                                return InkWell(
                                  key: ValueKey('command_item_${action.id}'),
                                  onTap: () => _executeAction(action),
                                  onHover: (hovered) {
                                    if (hovered && _selectedIndex != index) {
                                      setState(() {
                                        _selectedIndex = index;
                                      });
                                    }
                                  },
                                  child: Container(
                                    padding: const EdgeInsets.symmetric(
                                      horizontal: 14,
                                      vertical: 8,
                                    ),
                                    decoration: BoxDecoration(
                                      color: isSelected
                                          ? Gs3dTokens.accent.withAlpha(30)
                                          : Colors.transparent,
                                      border: Border(
                                        left: BorderSide(
                                          color: isSelected
                                              ? Gs3dTokens.accent
                                              : Colors.transparent,
                                          width: 3,
                                        ),
                                      ),
                                    ),
                                    child: Row(
                                      children: [
                                        _buildCategoryBadge(action.category),
                                        const SizedBox(width: 10),
                                        Expanded(
                                          child: Column(
                                            crossAxisAlignment:
                                                CrossAxisAlignment.start,
                                            children: [
                                              Row(
                                                children: [
                                                  Expanded(
                                                    child: Text(
                                                      action.name,
                                                      maxLines: 1,
                                                      overflow:
                                                          TextOverflow.ellipsis,
                                                      style: TextStyle(
                                                        fontSize: 13,
                                                        fontWeight: isSelected
                                                            ? FontWeight.w700
                                                            : FontWeight.w600,
                                                        color: isSelected
                                                            ? Gs3dTokens.accent
                                                            : Gs3dTokens
                                                                  .textPrimary,
                                                      ),
                                                    ),
                                                  ),
                                                  if (!isCompact) ...[
                                                    const SizedBox(
                                                      width: Gs3dTokens.space8,
                                                    ),
                                                    Flexible(
                                                      child: Text(
                                                        action.id,
                                                        maxLines: 1,
                                                        overflow: TextOverflow
                                                            .ellipsis,
                                                        style: const TextStyle(
                                                          fontSize: 10.5,
                                                          color: Gs3dTokens
                                                              .textSecondary,
                                                          fontFamily:
                                                              'monospace',
                                                        ),
                                                      ),
                                                    ),
                                                  ],
                                                ],
                                              ),
                                              const SizedBox(height: 2),
                                              Text(
                                                action.description,
                                                maxLines: 1,
                                                overflow: TextOverflow.ellipsis,
                                                style: const TextStyle(
                                                  fontSize: 11.5,
                                                  color: Gs3dTokens.textBody,
                                                ),
                                              ),
                                            ],
                                          ),
                                        ),
                                        if (isSelected) ...[
                                          const SizedBox(width: 8),
                                          const Icon(
                                            Icons.keyboard_return,
                                            size: 14,
                                            color: Gs3dTokens.accent,
                                          ),
                                        ],
                                      ],
                                    ),
                                  ),
                                );
                              },
                            ),
                    ),

                    // 底部快捷提示栏
                    Container(
                      padding: const EdgeInsets.symmetric(
                        horizontal: 14,
                        vertical: 6,
                      ),
                      decoration: const BoxDecoration(
                        color: Gs3dTokens.surfaceSunken,
                        border: Border(
                          top: BorderSide(color: Gs3dTokens.border, width: 1),
                        ),
                      ),
                      child: isCompact
                          ? Row(
                              children: [
                                Expanded(
                                  child: Text(
                                    '点按外部关闭',
                                    maxLines: 1,
                                    overflow: TextOverflow.ellipsis,
                                    style: const TextStyle(
                                      fontSize: 10.5,
                                      color: Gs3dTokens.textSecondary,
                                    ),
                                  ),
                                ),
                                const SizedBox(width: Gs3dTokens.space8),
                                Text(
                                  '共 ${_filteredActions.length} 项',
                                  style: const TextStyle(
                                    fontSize: 10.5,
                                    color: Gs3dTokens.textSecondary,
                                  ),
                                ),
                              ],
                            )
                          : Row(
                              children: [
                                _buildFooterKeyHint('↑ / ↓', '导航选择'),
                                const SizedBox(width: Gs3dTokens.space12),
                                _buildFooterKeyHint('Enter', '执行指令'),
                                const SizedBox(width: Gs3dTokens.space12),
                                _buildFooterKeyHint('Esc', '关闭面板'),
                                const Spacer(),
                                Text(
                                  '共 ${_filteredActions.length} 项可用动作',
                                  style: const TextStyle(
                                    fontSize: 10.5,
                                    color: Gs3dTokens.textSecondary,
                                  ),
                                ),
                              ],
                            ),
                    ),
                  ],
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildCategoryBadge(String category) {
    Color color = Gs3dTokens.accent;
    String label = '全局';

    if (category.contains('camera') || category.contains('viewport')) {
      color = Gs3dTokens.categoryViewport;
      label = '视口';
    } else if (category.contains('point_cloud') ||
        category.contains('render')) {
      color = Gs3dTokens.categoryRender;
      label = '渲染';
    } else if (category.contains('measure')) {
      color = Gs3dTokens.measure;
      label = '测量';
    } else if (category.contains('stats')) {
      color = Gs3dTokens.categoryStats;
      label = '统计';
    } else if (category.contains('dock') || category.contains('layout')) {
      color = Gs3dTokens.categoryUi;
      label = '界面';
    } else if (category.contains('project') || category.contains('welcome')) {
      color = Gs3dTokens.categoryProject;
      label = '工程';
    }

    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.14),
        borderRadius: BorderRadius.circular(4),
        border: Border.all(color: color.withValues(alpha: 0.47), width: 0.8),
      ),
      child: Text(
        label,
        style: TextStyle(
          fontSize: 10,
          fontWeight: FontWeight.bold,
          color: color,
        ),
      ),
    );
  }

  Widget _buildFooterKeyHint(String keyLabel, String actionLabel) {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Container(
          padding: const EdgeInsets.symmetric(horizontal: 5, vertical: 1.5),
          decoration: BoxDecoration(
            color: Gs3dTokens.surface,
            borderRadius: BorderRadius.circular(3),
            border: Border.all(color: Gs3dTokens.border),
          ),
          child: Text(
            keyLabel,
            style: const TextStyle(
              fontSize: 9.5,
              fontWeight: FontWeight.bold,
              color: Gs3dTokens.textPrimary,
              fontFamily: 'monospace',
            ),
          ),
        ),
        const SizedBox(width: 4),
        Text(
          actionLabel,
          style: const TextStyle(
            fontSize: 10.5,
            color: Gs3dTokens.textSecondary,
          ),
        ),
      ],
    );
  }
}
