import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import '../ffi/geoscatter3d_service.dart';
import '../theme/macos_tokens.dart';
import 'new_project_dialog.dart';
import 'open_project_dialog.dart';
import 'recent_projects_card.dart';
import 'welcome_action_cards.dart';
import 'welcome_sidebar.dart';
import 'welcome_ui_keys.dart';

class WelcomePage extends StatefulWidget {
  final GeoScatter3dService service;

  const WelcomePage({super.key, required this.service});

  @override
  State<WelcomePage> createState() => _WelcomePageState();
}

class _WelcomePageState extends State<WelcomePage> {
  int _selectedNavIndex = 0;
  bool _isDrawerCollapsed = false;

  @override
  void initState() {
    super.initState();
    widget.service.onFileDropped = _handleFileDropped;
  }

  @override
  void dispose() {
    if (widget.service.onFileDropped == _handleFileDropped) {
      widget.service.onFileDropped = null;
    }
    super.dispose();
  }

  Future<void> _handleFileDropped(String path) async {
    if (!mounted) return;
    final lower = path.toLowerCase();
    if (lower.endsWith('.gs3d') || lower.endsWith('.gs3d.bundle')) {
      final result = await widget.service.executeActionAsync(
        'welcome.open_project',
        {'path': path},
      );
      if (!mounted || result['success'] == true) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(result['message'] as String? ?? '工程加载失败'),
          backgroundColor: const Color(0xFFDC2626),
        ),
      );
    } else {
      showDialog(
        context: context,
        barrierDismissible: true,
        builder: (_) =>
            NewProjectDialog(service: widget.service, initialPath: path),
      );
    }
  }

  void _openNewProjectDialog() {
    showDialog(
      context: context,
      barrierDismissible: true,
      builder: (_) => NewProjectDialog(service: widget.service),
    );
  }

  void _openProjectDialog() {
    showDialog(
      context: context,
      barrierDismissible: true,
      builder: (_) => OpenProjectDialog(service: widget.service),
    );
  }

  @override
  Widget build(BuildContext context) {
    return CallbackShortcuts(
      bindings: {
        const SingleActivator(LogicalKeyboardKey.keyN, control: true):
            _openNewProjectDialog,
        const SingleActivator(LogicalKeyboardKey.keyO, control: true):
            _openProjectDialog,
        const SingleActivator(LogicalKeyboardKey.keyB, control: true): () {
          setState(() => _isDrawerCollapsed = !_isDrawerCollapsed);
        },
      },
      child: Focus(
        autofocus: true,
        child: Scaffold(
          key: WelcomeUiKeys.welcomeDragDropTarget,
          backgroundColor: Colors.white,
          body: Column(
            children: [
              // 1. 全局统一 macOS 顶栏 (高度 40px，居中标题，左侧交通灯与抽屉开关永远不溢出)
              Container(
                height: 40,
                color: const Color(0xFFF8F9FA),
                padding: const EdgeInsets.symmetric(horizontal: 14),
                child: Row(
                  children: [
                    // 左侧：macOS 交通灯三色控件
                    const MacTrafficLights(),

                    const Spacer(),

                    // 居中应用标题与版本
                    const Text(
                      'GeoScatter3D · v0.1.0',
                      style: TextStyle(
                        fontSize: 12.5,
                        fontWeight: FontWeight.w600,
                        color: Color(0xFF374151),
                        letterSpacing: -0.2,
                      ),
                    ),

                    const Spacer(),

                    // 右侧占位保持居中对称 (对应左侧交通灯 47px 宽度)
                    const SizedBox(width: 47),
                  ],
                ),
              ),
              const Divider(height: 1, thickness: 1, color: Color(0xFFE5E7EB)),

              // 2. 下方主工作区 (左侧抽屉 + 右侧主内容)
              Expanded(
                child: Row(
                  children: [
                    // 左侧抽屉式侧边栏 (展开 220px，折叠 64px)
                    WelcomeSidebar(
                      service: widget.service,
                      selectedIndex: _selectedNavIndex,
                      onSelectIndex: (idx) {
                        setState(() => _selectedNavIndex = idx);
                      },
                      isCollapsed: _isDrawerCollapsed,
                      onToggleCollapse: () {
                        setState(() => _isDrawerCollapsed = !_isDrawerCollapsed);
                      },
                    ),

                    // 抽屉与主内容分割线
                    const VerticalDivider(width: 1, thickness: 1, color: Color(0xFFE5E7EB)),

                    // 右侧主内容区域
                    Expanded(
                      child: Column(
                        children: [
                          // 异常 / 忙碌横幅提示
                          ListenableBuilder(
                            listenable: widget.service,
                            builder: (context, _) {
                              final service = widget.service;
                              if (!service.welcomeBusy && service.welcomeError == null) {
                                return const SizedBox.shrink();
                              }
                              return Container(
                                width: double.infinity,
                                padding: const EdgeInsets.symmetric(
                                  horizontal: 16,
                                  vertical: 8,
                                ),
                                color: service.welcomeError == null
                                    ? const Color(0xFFEBF5FF)
                                    : const Color(0xFFFEF2F2),
                                child: Row(
                                  children: [
                                    if (service.welcomeBusy)
                                      const SizedBox(
                                        width: 16,
                                        height: 16,
                                        child: CircularProgressIndicator(strokeWidth: 2),
                                      ),
                                    const SizedBox(width: 8),
                                    Expanded(
                                      child: Text(
                                        service.welcomeError ?? service.backendStatus,
                                        style: const TextStyle(fontSize: 12),
                                      ),
                                    ),
                                  ],
                                ),
                              );
                            },
                          ),

                          // 主体多视图滚动内容区 (限制最大宽度 960px，杜绝超宽屏无序拉伸死白)
                          Expanded(
                            child: SingleChildScrollView(
                              padding: const EdgeInsets.symmetric(
                                horizontal: 44,
                                vertical: 34,
                              ),
                              child: Align(
                                alignment: Alignment.topLeft,
                                child: ConstrainedBox(
                                  constraints: const BoxConstraints(maxWidth: 960),
                                  child: _buildSelectedTabView(),
                                ),
                              ),
                            ),
                          ),
                        ],
                      ),
                    ),
                  ],
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildSelectedTabView() {
    switch (_selectedNavIndex) {
      case 1:
        return _buildRecentsView();
      case 2:
        return _buildSamplesView();
      case 3:
        return _buildHelpView();
      case 4:
        return _buildAboutView();
      case 0:
      default:
        return _buildFilesView();
    }
  }

  // 1. 文件主启动视图
  Widget _buildFilesView() {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const Text(
          '文件',
          style: TextStyle(
            fontSize: 26,
            fontWeight: FontWeight.w700,
            color: Color(0xFF111827),
            letterSpacing: -0.5,
          ),
        ),
        const SizedBox(height: 20),

        // 新建或打开项目 (三联极简卡片 +「打开文件…」按钮)
        WelcomeActionCards(service: widget.service),
        const SizedBox(height: 28),

        // 最近打开的项目 (结构化表格)
        RecentProjectsCard(service: widget.service),
      ],
    );
  }

  // 2. 最近项目专属管理器视图
  Widget _buildRecentsView() {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          children: [
            Container(
              width: 36,
              height: 36,
              decoration: BoxDecoration(
                color: const Color(0xFFEBF5FF),
                borderRadius: BorderRadius.circular(9),
              ),
              child: const Icon(
                Icons.access_time_rounded,
                color: MacOsTokens.systemBlue,
                size: 20,
              ),
            ),
            const SizedBox(width: 12),
            Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: const [
                Text(
                  '最近项目管理',
                  style: TextStyle(
                    fontSize: 24,
                    fontWeight: FontWeight.w700,
                    color: Color(0xFF111827),
                    letterSpacing: -0.5,
                  ),
                ),
                SizedBox(height: 2),
                Text(
                  '管理本地历史加载的全部空间数据包与散点工程，支持快速定位与清理',
                  style: TextStyle(
                    fontSize: 12.5,
                    color: Color(0xFF6B7280),
                  ),
                ),
              ],
            ),
          ],
        ),
        const SizedBox(height: 20),

        // 分类过滤芯片
        Row(
          children: [
            _buildFilterChip('全部工程', true),
            const SizedBox(width: 8),
            _buildFilterChip('分层工区包 (.bundle)', false),
            const SizedBox(width: 8),
            _buildFilterChip('单体点云 (.gs3d)', false),
          ],
        ),
        const SizedBox(height: 16),

        RecentProjectsCard(service: widget.service),
      ],
    );
  }

  Widget _buildFilterChip(String label, bool active) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 5),
      decoration: BoxDecoration(
        color: active ? const Color(0xFFEBF5FF) : const Color(0xFFF3F4F6),
        borderRadius: BorderRadius.circular(16),
        border: Border.all(
          color: active ? MacOsTokens.systemBlue.withValues(alpha: 0.25) : Colors.transparent,
        ),
      ),
      child: Text(
        label,
        style: TextStyle(
          fontSize: 12,
          fontWeight: active ? FontWeight.w600 : FontWeight.w500,
          color: active ? MacOsTokens.systemBlue : const Color(0xFF4B5563),
        ),
      ),
    );
  }

  // 3. 示例与基准库视图 (对应用户截图标注的「示例」选项)
  Widget _buildSamplesView() {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          children: [
            Container(
              width: 36,
              height: 36,
              decoration: BoxDecoration(
                color: const Color(0xFFEBF5FF),
                borderRadius: BorderRadius.circular(9),
              ),
              child: const Icon(
                Icons.view_in_ar_rounded,
                color: MacOsTokens.systemBlue,
                size: 20,
              ),
            ),
            const SizedBox(width: 12),
            Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: const [
                Text(
                  '示例工程库',
                  style: TextStyle(
                    fontSize: 24,
                    fontWeight: FontWeight.w700,
                    color: Color(0xFF111827),
                    letterSpacing: -0.5,
                  ),
                ),
                SizedBox(height: 2),
                Text(
                  '开箱即用的官方高精度点云与地质模型，用于验证 Vulkan 密集着色管线与八叉树 LOD 性能',
                  style: TextStyle(
                    fontSize: 12.5,
                    color: Color(0xFF6B7280),
                  ),
                ),
              ],
            ),
          ],
        ),
        const SizedBox(height: 24),

        // 示例卡片 1
        _buildSampleCard(
          icon: Icons.scatter_plot_rounded,
          iconBg: const Color(0xFFEBF5FF),
          iconColor: MacOsTokens.systemBlue,
          title: '高斯散点示范包 (Gaussian Point Cloud Bundle)',
          badge: '官方示范',
          badgeColor: MacOsTokens.systemBlue,
          desc: '轻量高斯随机分布与多面体散斑数据，秒级启动工作台，用于视口交互基准测试。',
          specs: const [
            ('25 万散点', Icons.circle_outlined),
            ('8 层八叉树', Icons.account_tree_outlined),
            ('.gs3d.bundle 空间包', Icons.folder_zip_outlined),
          ],
          onLaunch: () => widget.service.executeActionAsync('welcome.quick_demo'),
        ),
        const SizedBox(height: 14),

        // 示例卡片 2
        _buildSampleCard(
          icon: Icons.location_city_rounded,
          iconBg: const Color(0xFFECFDF5),
          iconColor: const Color(0xFF059669),
          title: '城市建筑激光雷达机载点云 (Urban LiDAR Survey)',
          badge: '真实测绘',
          badgeColor: const Color(0xFF059669),
          desc: '包含地表高程分类与建筑物边界的大范围地理空间数据集，测试视锥剔除与空间索引。',
          specs: const [
            ('125 万散点', Icons.circle_outlined),
            ('12 层八叉树', Icons.account_tree_outlined),
            ('RGB 真彩色', Icons.palette_outlined),
          ],
          onLaunch: () => widget.service.executeActionAsync('welcome.quick_demo'),
        ),
        const SizedBox(height: 14),

        // 示例卡片 3
        _buildSampleCard(
          icon: Icons.terrain_rounded,
          iconBg: const Color(0xFFF5F3FF),
          iconColor: const Color(0xFF7C3AED),
          title: '深层地质断层带结构散点 (Fault Line Structural Model)',
          badge: '深地勘探',
          badgeColor: const Color(0xFF7C3AED),
          desc: '面向地质勘探场景的断层面海量散点模型，验证 Vulkan 密集着色管线与动态流式加载。',
          specs: const [
            ('480 万散点', Icons.circle_outlined),
            ('16 层八叉树', Icons.account_tree_outlined),
            ('断层属性场', Icons.view_in_ar_outlined),
          ],
          onLaunch: () => widget.service.executeActionAsync('welcome.quick_demo'),
        ),
      ],
    );
  }

  Widget _buildSampleCard({
    required IconData icon,
    required Color iconBg,
    required Color iconColor,
    required String title,
    required String badge,
    required Color badgeColor,
    required String desc,
    required List<(String, IconData)> specs,
    required VoidCallback onLaunch,
  }) {
    return Container(
      padding: const EdgeInsets.all(18),
      decoration: BoxDecoration(
        color: Colors.white,
        borderRadius: BorderRadius.circular(12),
        border: Border.all(color: const Color(0xFFE5E7EB)),
        boxShadow: const [
          BoxShadow(
            color: Color(0x06000000),
            blurRadius: 8,
            offset: Offset(0, 2),
          ),
        ],
      ),
      child: Row(
        children: [
          Container(
            width: 46,
            height: 46,
            decoration: BoxDecoration(
              color: iconBg,
              borderRadius: BorderRadius.circular(11),
            ),
            child: Icon(icon, color: iconColor, size: 24),
          ),
          const SizedBox(width: 16),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    Text(
                      title,
                      style: const TextStyle(
                        fontSize: 14.5,
                        fontWeight: FontWeight.w700,
                        color: Color(0xFF111827),
                      ),
                    ),
                    const SizedBox(width: 8),
                    Container(
                      padding: const EdgeInsets.symmetric(horizontal: 7, vertical: 2),
                      decoration: BoxDecoration(
                        color: badgeColor.withValues(alpha: 0.1),
                        borderRadius: BorderRadius.circular(4),
                      ),
                      child: Text(
                        badge,
                        style: TextStyle(
                          fontSize: 10.5,
                          fontWeight: FontWeight.w600,
                          color: badgeColor,
                        ),
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 4),
                Text(
                  desc,
                  style: const TextStyle(fontSize: 12.5, color: Color(0xFF4B5563), height: 1.3),
                ),
                const SizedBox(height: 8),
                Wrap(
                  spacing: 8,
                  children: specs.map((s) => Container(
                    padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
                    decoration: BoxDecoration(
                      color: const Color(0xFFF9FAFB),
                      borderRadius: BorderRadius.circular(6),
                      border: Border.all(color: const Color(0xFFE5E7EB)),
                    ),
                    child: Row(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Icon(s.$2, size: 11, color: const Color(0xFF6B7280)),
                        const SizedBox(width: 4),
                        Text(
                          s.$1,
                          style: const TextStyle(fontSize: 11, color: Color(0xFF4B5563), fontWeight: FontWeight.w500),
                        ),
                      ],
                    ),
                  )).toList(),
                ),
              ],
            ),
          ),
          const SizedBox(width: 20),
          ElevatedButton.icon(
            onPressed: onLaunch,
            icon: const Icon(Icons.play_arrow_rounded, size: 16),
            label: const Text('载入视口', style: TextStyle(fontWeight: FontWeight.w600, fontSize: 13)),
            style: ElevatedButton.styleFrom(
              backgroundColor: MacOsTokens.systemBlue,
              foregroundColor: Colors.white,
              elevation: 0,
              padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 10),
              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(8)),
            ),
          ),
        ],
      ),
    );
  }

  // 4. 帮助与技术规范视图
  Widget _buildHelpView() {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          children: [
            Container(
              width: 36,
              height: 36,
              decoration: BoxDecoration(
                color: const Color(0xFFEBF5FF),
                borderRadius: BorderRadius.circular(9),
              ),
              child: const Icon(
                Icons.help_outline_rounded,
                color: MacOsTokens.systemBlue,
                size: 20,
              ),
            ),
            const SizedBox(width: 12),
            Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: const [
                Text(
                  '帮助与技术规范',
                  style: TextStyle(
                    fontSize: 24,
                    fontWeight: FontWeight.w700,
                    color: Color(0xFF111827),
                    letterSpacing: -0.5,
                  ),
                ),
                SizedBox(height: 2),
                Text(
                  '熟悉三维视口交互手势、GS3D 格式规格与系统渲染管线',
                  style: TextStyle(
                    fontSize: 12.5,
                    color: Color(0xFF6B7280),
                  ),
                ),
              ],
            ),
          ],
        ),
        const SizedBox(height: 24),

        Row(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Expanded(
              child: _buildInfoBox(
                title: '视口交互快捷键指南',
                icon: Icons.mouse_rounded,
                items: const [
                  {'label': '旋转视角', 'val': '鼠标左键拖拽'},
                  {'label': '平移视口', 'val': '右键/中键拖拽 或 Shift+左键'},
                  {'label': '缩放视野', 'val': '滚轮滚动'},
                  {'label': '复位视角', 'val': 'Ctrl+R / 顶栏视角复位'},
                  {'label': '新建工程', 'val': '⌘N / Ctrl+N'},
                  {'label': '打开工程', 'val': '⌘O / Ctrl+O'},
                  {'label': '抽屉收放', 'val': '⌘B / Ctrl+B'},
                ],
              ),
            ),
            const SizedBox(width: 16),
            Expanded(
              child: _buildInfoBox(
                title: 'GS3D v2 数据格式标准',
                icon: Icons.architecture_rounded,
                items: const [
                  {'label': '编码格式', 'val': '显式小端 (Little-Endian)'},
                  {'label': '步长对齐', 'val': 'point_stride 严格对齐校验'},
                  {'label': '空间索引', 'val': '动态多级八叉树 LOD 金字塔'},
                  {'label': '流式加载', 'val': '多线程后台预取，按需解压'},
                  {'label': '双轨保障', 'val': 'C-ABI 原生引擎 + Dart 回退'},
                ],
              ),
            ),
          ],
        ),
      ],
    );
  }

  Widget _buildInfoBox({
    required String title,
    required IconData icon,
    required List<Map<String, String>> items,
  }) {
    return Container(
      padding: const EdgeInsets.all(18),
      decoration: BoxDecoration(
        color: Colors.white,
        borderRadius: BorderRadius.circular(10),
        border: Border.all(color: const Color(0xFFE5E7EB)),
        boxShadow: const [
          BoxShadow(
            color: Color(0x06000000),
            blurRadius: 8,
            offset: Offset(0, 2),
          ),
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(icon, size: 18, color: MacOsTokens.systemBlue),
              const SizedBox(width: 8),
              Text(
                title,
                style: const TextStyle(fontSize: 14.5, fontWeight: FontWeight.w700),
              ),
            ],
          ),
          const SizedBox(height: 12),
          ...items.map((it) => Padding(
            padding: const EdgeInsets.symmetric(vertical: 6),
            child: Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                Text(it['label']!, style: const TextStyle(fontSize: 12.5, color: Color(0xFF4B5563))),
                Container(
                  padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
                  decoration: BoxDecoration(
                    color: const Color(0xFFF3F4F6),
                    borderRadius: BorderRadius.circular(4),
                  ),
                  child: Text(
                    it['val']!,
                    style: const TextStyle(fontSize: 11.5, fontFamily: 'monospace', fontWeight: FontWeight.w600),
                  ),
                ),
              ],
            ),
          )),
        ],
      ),
    );
  }

  // 5. 关于视图
  Widget _buildAboutView() {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const Text(
          '关于',
          style: TextStyle(
            fontSize: 26,
            fontWeight: FontWeight.w700,
            color: Color(0xFF111827),
            letterSpacing: -0.5,
          ),
        ),
        const SizedBox(height: 4),
        const Text(
          'GeoScatter3D 现代化工业级图形工作台软件信息与系统环境',
          style: TextStyle(
            fontSize: 13,
            color: Color(0xFF6B7280),
          ),
        ),
        const SizedBox(height: 24),

        Container(
          width: double.infinity,
          constraints: const BoxConstraints(maxWidth: 680),
          padding: const EdgeInsets.all(24),
          decoration: BoxDecoration(
            color: Colors.white,
            borderRadius: BorderRadius.circular(12),
            border: Border.all(color: const Color(0xFFE5E7EB)),
            boxShadow: const [
              BoxShadow(
                color: Color(0x06000000),
                blurRadius: 8,
                offset: Offset(0, 2),
              ),
            ],
          ),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  Container(
                    width: 52,
                    height: 52,
                    decoration: BoxDecoration(
                      color: MacOsTokens.systemBlue,
                      borderRadius: BorderRadius.circular(12),
                      boxShadow: [
                        BoxShadow(
                          color: MacOsTokens.systemBlue.withValues(alpha: 0.3),
                          blurRadius: 8,
                          offset: const Offset(0, 2),
                        ),
                      ],
                    ),
                    alignment: Alignment.center,
                    child: const Text(
                      'G',
                      style: TextStyle(fontSize: 26, fontWeight: FontWeight.w800, color: Colors.white),
                    ),
                  ),
                  const SizedBox(width: 16),
                  Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: const [
                      Text(
                        'GeoScatter3D Studio',
                        style: TextStyle(fontSize: 18, fontWeight: FontWeight.w700, color: Color(0xFF111827)),
                      ),
                      Text(
                        '版本 0.1.0 · Build 20260908 · Multiplatform',
                        style: TextStyle(fontSize: 11.5, color: Color(0xFF9CA3AF)),
                      ),
                    ],
                  ),
                ],
              ),
              const SizedBox(height: 20),
              const Divider(height: 1, color: Color(0xFFF3F4F6)),
              const SizedBox(height: 12),
              _buildSpecRow('用户界面框架', 'Flutter Desktop (Windows)'),
              _buildSpecRow('渲染计算内核', 'Vulkan 1.3 / Pure C-ABI FFI'),
              _buildSpecRow('空间组织引擎', 'GS3D v2 Dynamic Octree LOD'),
              _buildSpecRow('当前物理显卡', 'NVIDIA GeForce RTX 4070 (独立显卡)'),
              _buildSpecRow('控制面协议', 'JSON-RPC 2.0 (45 项原子 UI 动作引擎)'),
            ],
          ),
        ),
      ],
    );
  }

  Widget _buildSpecRow(String label, String value) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 8),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          Text(label, style: const TextStyle(fontSize: 12.5, color: Color(0xFF4B5563))),
          Text(value, style: const TextStyle(fontSize: 12.5, fontWeight: FontWeight.w600, color: Color(0xFF111827))),
        ],
      ),
    );
  }
}
