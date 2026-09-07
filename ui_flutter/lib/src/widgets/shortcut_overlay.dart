import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import '../ffi/geoscatter3d_service.dart';
import '../theme/app_theme.dart';

/// 全局键盘与鼠标快捷操作速查指南 (Shortcut Overlay - F1)
class ShortcutOverlay extends StatelessWidget {
  final GeoScatter3dService service;

  const ShortcutOverlay({
    super.key,
    required this.service,
  });

  @override
  Widget build(BuildContext context) {
    return KeyboardListener(
      focusNode: FocusNode()..requestFocus(),
      onKeyEvent: (event) {
        if (event is KeyDownEvent && (event.logicalKey == LogicalKeyboardKey.escape || event.logicalKey == LogicalKeyboardKey.f1)) {
          service.toggleShortcutOverlay(false);
        }
      },
      child: Stack(
        children: [
          // 1. 半透明遮罩
          Positioned.fill(
            child: GestureDetector(
              onTap: () => service.toggleShortcutOverlay(false),
              child: Container(
                color: Colors.black.withAlpha(140),
              ),
            ),
          ),

          // 2. 居中速查卡片
          Center(
            child: Container(
              key: const ValueKey('shortcut_overlay_modal'),
              width: 760,
              constraints: const BoxConstraints(maxHeight: 620),
              margin: const EdgeInsets.symmetric(horizontal: 24, vertical: 24),
              decoration: BoxDecoration(
                color: AppTheme.surface,
                borderRadius: BorderRadius.circular(12),
                border: Border.all(color: AppTheme.border, width: 1),
                boxShadow: const [
                  BoxShadow(
                    color: Colors.black54,
                    blurRadius: 30,
                    offset: Offset(0, 12),
                  ),
                ],
              ),
              child: ClipRRect(
                borderRadius: BorderRadius.circular(12),
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    // 顶部标题栏
                    Container(
                      padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 14),
                      decoration: const BoxDecoration(
                        color: AppTheme.surfaceMuted,
                        border: Border(bottom: BorderSide(color: AppTheme.border, width: 1)),
                      ),
                      child: Row(
                        children: [
                          Container(
                            padding: const EdgeInsets.all(6),
                            decoration: BoxDecoration(
                              color: AppTheme.primaryBlue.withAlpha(30),
                              borderRadius: BorderRadius.circular(6),
                            ),
                            child: const Icon(Icons.keyboard_outlined, size: 20, color: AppTheme.primaryBlue),
                          ),
                          const SizedBox(width: 12),
                          Column(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            children: const [
                              Text(
                                '键盘与鼠标快捷操作指南',
                                style: TextStyle(
                                  fontSize: 16,
                                  fontWeight: FontWeight.w700,
                                  color: AppTheme.textTitle,
                                ),
                              ),
                              SizedBox(height: 2),
                              Text(
                                'Keyboard & Mouse Shortcuts Reference',
                                style: TextStyle(fontSize: 11, color: AppTheme.textDim),
                              ),
                            ],
                          ),
                          const Spacer(),
                          IconButton(
                            icon: const Icon(Icons.close_rounded, size: 20, color: AppTheme.textDim),
                            onPressed: () => service.toggleShortcutOverlay(false),
                            tooltip: '关闭 (ESC)',
                            splashRadius: 18,
                          ),
                        ],
                      ),
                    ),

                    // 中部快捷键分类网格内容
                    Flexible(
                      child: ListView(
                        padding: const EdgeInsets.all(20),
                        shrinkWrap: true,
                        children: [
                          _buildSectionTitle('视口漫游与交互观察', Icons.threed_rotation_rounded),
                          const SizedBox(height: 10),
                          _buildShortcutGrid([
                            _ShortcutItem(keys: ['鼠标左键拖拽'], desc: '绕观察焦点轨道自由旋转视口'),
                            _ShortcutItem(keys: ['滚轮滑动'], desc: '平滑放大 / 缩小视口视野'),
                            _ShortcutItem(keys: ['平移模式', '左键拖拽'], desc: '视口平面沿当前观察轴平移'),
                            _ShortcutItem(keys: ['双击数据点'], desc: '将点设为旋转对焦中心 (Orbit Pivot)'),
                            _ShortcutItem(keys: ['R 键', 'Home'], desc: '一键复位标准轴测视角 (-45°, 30°)'),
                            _ShortcutItem(keys: ['C 键'], desc: '复制悬停数据点或相机物理坐标至剪贴板'),
                          ]),
                          const SizedBox(height: 20),

                          _buildSectionTitle('几何测量与选区分析', Icons.straighten_rounded),
                          const SizedBox(height: 10),
                          _buildShortcutGrid([
                            _ShortcutItem(keys: ['Shift', '左键拖拽'], desc: '在视口内矩形框选，自动计算属性直方图'),
                            _ShortcutItem(keys: ['M 键'], desc: '开启或关闭三维测距标尺拾取模式'),
                            _ShortcutItem(keys: ['单击数据点'], desc: '测距模式下依次拾取起点与终点生成测量线'),
                            _ShortcutItem(keys: ['左侧面板', '测量'], desc: '查看全部测量线段、固定图钉或一键清空'),
                          ]),
                          const SizedBox(height: 20),

                          _buildSectionTitle('效率工具与诊断检查', Icons.tune_rounded),
                          const SizedBox(height: 10),
                          _buildShortcutGrid([
                            _ShortcutItem(keys: ['Ctrl', 'P'], desc: '唤出全局交互式命令面板 (Command Palette)'),
                            _ShortcutItem(keys: ['F1 键'], desc: '打开或关闭本快捷键速查窗口'),
                            _ShortcutItem(keys: ['状态栏 FPS'], desc: '点击底部状态栏打开实时性能与显存诊断'),
                            _ShortcutItem(keys: ['状态栏 瓦片'], desc: '点击打开八叉树瓦片与 LOD 流式视察器'),
                            _ShortcutItem(keys: ['工具栏 相机'], desc: '触发视口 2.0x 高清 PNG 显存回读截图'),
                            _ShortcutItem(keys: ['文件菜单', '导出'], desc: '将当前点云导出为标准 PLY / CSV 格式'),
                          ]),
                        ],
                      ),
                    ),

                    // 底部操作栏
                    Container(
                      padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 12),
                      decoration: const BoxDecoration(
                        color: AppTheme.surfaceMuted,
                        border: Border(top: BorderSide(color: AppTheme.border, width: 1)),
                      ),
                      child: Row(
                        mainAxisAlignment: MainAxisAlignment.spaceBetween,
                        children: [
                          const Text(
                            '按 ESC 键或点击卡片外部任意处即可关闭',
                            style: TextStyle(fontSize: 12, color: AppTheme.textDim),
                          ),
                          ElevatedButton(
                            style: ElevatedButton.styleFrom(
                              backgroundColor: AppTheme.primaryBlue,
                              foregroundColor: Colors.white,
                              elevation: 0,
                              padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
                            ),
                            onPressed: () => service.toggleShortcutOverlay(false),
                            child: const Text('我知道了', style: TextStyle(fontSize: 12.5)),
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

  Widget _buildSectionTitle(String title, IconData icon) {
    return Row(
      children: [
        Icon(icon, size: 16, color: AppTheme.primaryBlue),
        const SizedBox(width: 8),
        Text(
          title,
          style: const TextStyle(
            fontSize: 13.5,
            fontWeight: FontWeight.w700,
            color: AppTheme.textTitle,
          ),
        ),
      ],
    );
  }

  Widget _buildShortcutGrid(List<_ShortcutItem> items) {
    return GridView.builder(
      shrinkWrap: true,
      physics: const NeverScrollableScrollPhysics(),
      gridDelegate: const SliverGridDelegateWithFixedCrossAxisCount(
        crossAxisCount: 2,
        crossAxisSpacing: 12,
        mainAxisSpacing: 8,
        mainAxisExtent: 44,
      ),
      itemCount: items.length,
      itemBuilder: (context, index) {
        final item = items[index];
        return Container(
          padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
          decoration: BoxDecoration(
            color: AppTheme.surfaceMuted,
            borderRadius: BorderRadius.circular(6),
            border: Border.all(color: AppTheme.border.withAlpha(100)),
          ),
          child: Row(
            children: [
              Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  for (int i = 0; i < item.keys.length; i++) ...[
                    Container(
                      padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
                      decoration: BoxDecoration(
                        color: AppTheme.surface,
                        borderRadius: BorderRadius.circular(4),
                        border: Border.all(color: AppTheme.border),
                        boxShadow: const [
                          BoxShadow(color: Colors.black12, blurRadius: 2, offset: Offset(0, 1)),
                        ],
                      ),
                      child: Text(
                        item.keys[i],
                        style: const TextStyle(
                          fontSize: 11,
                          fontWeight: FontWeight.w700,
                          color: AppTheme.textTitle,
                          fontFamily: 'monospace',
                        ),
                      ),
                    ),
                    if (i < item.keys.length - 1)
                      const Padding(
                        padding: EdgeInsets.symmetric(horizontal: 3),
                        child: Text('+', style: TextStyle(fontSize: 11, color: AppTheme.textDim)),
                      ),
                  ],
                ],
              ),
              const SizedBox(width: 10),
              Expanded(
                child: Text(
                  item.desc,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: const TextStyle(
                    fontSize: 11.5,
                    color: AppTheme.textBody,
                  ),
                ),
              ),
            ],
          ),
        );
      },
    );
  }
}

class _ShortcutItem {
  final List<String> keys;
  final String desc;

  const _ShortcutItem({required this.keys, required this.desc});
}
