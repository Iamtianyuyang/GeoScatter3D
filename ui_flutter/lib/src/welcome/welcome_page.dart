import 'dart:io' show Platform;

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../ffi/geoscatter3d_service.dart';
import '../theme/gs3d_form_factor.dart';
import '../theme/gs3d_tokens.dart';
import '../widgets/command_palette.dart';
import '../widgets/fluid.dart';
import 'new_project_dialog.dart';
import 'open_project_dialog.dart';
import 'recent_projects_card.dart';
import 'welcome_action_cards.dart';
import 'welcome_hero.dart';
import 'welcome_sidebar.dart';
import 'welcome_ui_keys.dart';

/// 欢迎页主视图：顶栏 + 抽屉侧栏 + 多视图内容区。
class WelcomePage extends StatefulWidget {
  const WelcomePage({super.key, required this.service});

  final GeoScatter3dService service;

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
          backgroundColor: Gs3dTokens.danger,
        ),
      );
    } else {
      Gs3dResponsiveDialog.show(
        context,
        title: '新建散点工程',
        builder: (_) =>
            NewProjectDialog(service: widget.service, initialPath: path),
      );
    }
  }

  void _openNewProjectDialog() {
    Gs3dResponsiveDialog.show(
      context,
      title: '新建散点工程',
      builder: (_) => NewProjectDialog(service: widget.service),
    );
  }

  void _openProjectDialog() {
    Gs3dResponsiveDialog.show(
      context,
      title: '打开工程数据包',
      builder: (_) => OpenProjectDialog(service: widget.service),
    );
  }

  /// 唤起命令面板（等价 Ctrl+K/P 入口；无键盘设备可达，R2 走 executeAction）。
  void _openCommandPalette() {
    widget.service.executeAction('workbench.overlay.command_palette', {
      'open': true,
    });
  }

  @override
  Widget build(BuildContext context) {
    final touchPreferred = context.isTouchPreferred;
    // 触控档：欢迎页侧栏常显 64 图标栏（命中 ≥48），不做抽屉化
    // （架构假设声明：工程量收益比最优，PRD 未强制欢迎页抽屉化）。
    final collapsed = _isDrawerCollapsed || touchPreferred;
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
          backgroundColor: Gs3dTokens.windowBg,
          body: Stack(
            children: [
              Column(
                children: [
                  // 1. 统一顶栏（居中标题，左右对称，永不溢出）。
                  Container(
                    height: Gs3dTokens.titleBarHeight,
                    color: Gs3dMaterial.bar.tint,
                    padding: const EdgeInsets.symmetric(
                      horizontal: Gs3dTokens.space12 + 2,
                    ),
                    child: Row(
                      children: [
                        const MacTrafficLights(),
                        const Spacer(),
                        // 品牌区：长按唤起命令面板（触屏等价快捷键入口）。
                        // KB-02：超窄视口下用 Flexible 收缩 + 省略号，避免顶栏
                        // 横向溢出；桌面宽度充裕时视觉零变化。
                        Flexible(
                          child: GestureDetector(
                            onLongPress: _openCommandPalette,
                            child: Text(
                              'GeoScatter3D · v0.1.0',
                              style: Gs3dType.bodyStrong,
                              maxLines: 1,
                              overflow: TextOverflow.ellipsis,
                            ),
                          ),
                        ),
                        const Spacer(),
                        // 触控档常驻「命令」按钮（等价 Ctrl+K/P）。
                        if (touchPreferred)
                          IconButton(
                            key: const ValueKey('welcome.topbar.command'),
                            tooltip: '命令面板',
                            icon: const Icon(
                              Icons.terminal,
                              size: 18,
                              color: Gs3dTokens.textSecondary,
                            ),
                            constraints: const BoxConstraints(
                              minWidth: Gs3dTokens.hitTargetTouch,
                              minHeight: Gs3dTokens.hitTargetTouch,
                            ),
                            onPressed: _openCommandPalette,
                          ),
                        const SizedBox(width: 47),
                      ],
                    ),
                  ),
                  const Divider(
                    height: 1,
                    thickness: 1,
                    color: Gs3dTokens.hairline,
                  ),

                  // 2. 主工作区：左侧抽屉 + 右侧内容。
                  Expanded(
                    child: Row(
                      children: [
                        WelcomeSidebar(
                          service: widget.service,
                          selectedIndex: _selectedNavIndex,
                          onSelectIndex: (idx) {
                            setState(() => _selectedNavIndex = idx);
                          },
                          isCollapsed: collapsed,
                          onToggleCollapse: () {
                            setState(
                              () => _isDrawerCollapsed = !_isDrawerCollapsed,
                            );
                          },
                        ),

                        const VerticalDivider(
                          width: 1,
                          thickness: 1,
                          color: Gs3dTokens.hairline,
                        ),

                        Expanded(
                          child: Column(
                            children: [
                              // 异常 / 忙碌横幅。
                              ListenableBuilder(
                                listenable: widget.service,
                                builder: (context, _) {
                                  final service = widget.service;
                                  if (!service.welcomeBusy &&
                                      service.welcomeError == null) {
                                    return const SizedBox.shrink();
                                  }
                                  final hasError = service.welcomeError != null;
                                  return AnimatedContainer(
                                    duration: Gs3dMotion.micro,
                                    curve: Gs3dMotion.standardCurve,
                                    width: double.infinity,
                                    padding: const EdgeInsets.symmetric(
                                      horizontal: Gs3dTokens.space16,
                                      vertical: Gs3dTokens.space8,
                                    ),
                                    color: hasError
                                        ? Gs3dTokens.dangerSubtle
                                        : Gs3dTokens.infoSubtle,
                                    child: Row(
                                      children: [
                                        if (service.welcomeBusy)
                                          const SizedBox(
                                            width: 16,
                                            height: 16,
                                            child: CircularProgressIndicator(
                                              strokeWidth: 2,
                                            ),
                                          ),
                                        const SizedBox(
                                          width: Gs3dTokens.space8,
                                        ),
                                        Expanded(
                                          child: Text(
                                            service.welcomeError ??
                                                service.backendStatus,
                                            style: Gs3dType.caption.copyWith(
                                              color: hasError
                                                  ? Gs3dTokens.danger
                                                  : Gs3dTokens.textBody,
                                            ),
                                          ),
                                        ),
                                      ],
                                    ),
                                  );
                                },
                              ),

                              // 内容滚动区（最大宽度 1080 并居中，杜绝超宽屏拉伸）。
                              Expanded(
                                child: SingleChildScrollView(
                                  padding: const EdgeInsets.symmetric(
                                    horizontal: 48,
                                    vertical: 40,
                                  ),
                                  child: Align(
                                    alignment: Alignment.topCenter,
                                    child: ConstrainedBox(
                                      constraints: const BoxConstraints(
                                        maxWidth: 1080,
                                      ),
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

              // 全局命令面板覆盖层（触控入口按钮化后，欢迎页同样可达）。
              ListenableBuilder(
                listenable: widget.service,
                builder: (context, _) {
                  if (!widget.service.isCommandPaletteOpen) {
                    return const SizedBox.shrink();
                  }
                  return CommandPalette(service: widget.service);
                },
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

  // 1. 文件主启动视图。
  Widget _buildFilesView() {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const WelcomeHero(),
        const SizedBox(height: Gs3dTokens.space24),
        WelcomeActionCards(service: widget.service),
        const SizedBox(height: Gs3dTokens.space24 + 8),
        RecentProjectsCard(service: widget.service),
      ],
    );
  }

  // 2. 最近项目管理视图。
  Widget _buildRecentsView() {
    return ListenableBuilder(
      listenable: widget.service,
      builder: (context, _) {
        final filter = widget.service.recentProjectFilter;
        return Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const _ViewHeader(
              icon: Icons.access_time_rounded,
              title: '最近项目管理',
              subtitle: '管理本地历史加载的全部空间数据包与散点工程，支持快速定位与清理',
            ),
            const SizedBox(height: Gs3dTokens.space20),

            Wrap(
              spacing: Gs3dTokens.space8,
              runSpacing: Gs3dTokens.space8,
              children: [
                Gs3dPillButton(
                  label: '全部工程',
                  onPressed: () => widget.service.setRecentProjectFilter('all'),
                  active: filter == 'all',
                ),
                Gs3dPillButton(
                  label: '分层工区包 (.bundle)',
                  onPressed: () =>
                      widget.service.setRecentProjectFilter('bundle'),
                  active: filter == 'bundle',
                ),
                Gs3dPillButton(
                  label: '单体点云 (.gs3d)',
                  onPressed: () =>
                      widget.service.setRecentProjectFilter('gs3d'),
                  active: filter == 'gs3d',
                ),
              ],
            ),
            const SizedBox(height: Gs3dTokens.space16),

            RecentProjectsCard(
              service: widget.service,
              projects: widget.service.filteredRecentProjects,
              emptyMessage: filter == 'all' ? '暂无最近打开的项目记录' : '该格式暂无最近打开的项目记录',
              showEmptyDemoButton: filter == 'all',
            ),
          ],
        );
      },
    );
  }

  // 3. 示例与基准库视图。
  Widget _buildSamplesView() {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const _ViewHeader(
          icon: Icons.view_in_ar_rounded,
          title: '示例工程库',
          subtitle: '安装包内置的可验证点云示例，用于检查数据加载、八叉树 LOD 与 Vulkan 视口',
        ),
        const SizedBox(height: Gs3dTokens.space24),

        _buildSampleCard(
          icon: Icons.scatter_plot_rounded,
          iconColor: Gs3dTokens.accent,
          title: '内置点云示例工程 (GS3D Bundle)',
          badge: '可直接载入',
          badgeColor: Gs3dTokens.accent,
          desc: '安装包内置 sample-points.csv，可在首次体验时自动构建并载入工程。',
          specs: const [
            ('25 个示例点', Icons.circle_outlined),
            ('真实 CSV 资源', Icons.table_chart_outlined),
            ('自动生成 LOD', Icons.account_tree_outlined),
          ],
          onLaunch: () =>
              widget.service.executeActionAsync('welcome.quick_demo'),
        ),
      ],
    );
  }

  Widget _buildSampleCard({
    required IconData icon,
    required Color iconColor,
    required String title,
    required String badge,
    required Color badgeColor,
    required String desc,
    required List<(String, IconData)> specs,
    required VoidCallback onLaunch,
  }) {
    return Gs3dCard(
      padding: const EdgeInsets.all(Gs3dTokens.space16 + 2),
      child: Row(
        children: [
          Container(
            width: 46,
            height: 46,
            decoration: BoxDecoration(
              color: iconColor.withValues(alpha: 0.1),
              borderRadius: BorderRadius.circular(Gs3dTokens.radiusMd + 1),
            ),
            child: Icon(icon, color: iconColor, size: 24),
          ),
          const SizedBox(width: Gs3dTokens.space16),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    Text(
                      title,
                      style: Gs3dType.headline.copyWith(
                        fontSize: 14.5,
                        fontWeight: FontWeight.w700,
                      ),
                    ),
                    const SizedBox(width: Gs3dTokens.space8),
                    Container(
                      padding: const EdgeInsets.symmetric(
                        horizontal: 7,
                        vertical: 2,
                      ),
                      decoration: BoxDecoration(
                        color: badgeColor.withValues(alpha: 0.1),
                        borderRadius: BorderRadius.circular(
                          Gs3dTokens.radiusXs,
                        ),
                      ),
                      child: Text(
                        badge,
                        style: Gs3dType.micro.copyWith(
                          color: badgeColor,
                          letterSpacing: 0,
                        ),
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: Gs3dTokens.space4),
                Text(desc, style: Gs3dType.body),
                const SizedBox(height: Gs3dTokens.space8),
                Wrap(
                  spacing: Gs3dTokens.space8,
                  runSpacing: Gs3dTokens.space4,
                  children: specs
                      .map(
                        (s) => Container(
                          padding: const EdgeInsets.symmetric(
                            horizontal: Gs3dTokens.space8,
                            vertical: 3,
                          ),
                          decoration: BoxDecoration(
                            color: Gs3dTokens.surfaceMuted,
                            borderRadius: BorderRadius.circular(
                              Gs3dTokens.radiusSm,
                            ),
                            border: Border.all(color: Gs3dTokens.border),
                          ),
                          child: Row(
                            mainAxisSize: MainAxisSize.min,
                            children: [
                              Icon(
                                s.$2,
                                size: 11,
                                color: Gs3dTokens.textTertiary,
                              ),
                              const SizedBox(width: Gs3dTokens.space4),
                              Text(
                                s.$1,
                                style: Gs3dType.caption.copyWith(
                                  fontSize: 11,
                                  color: Gs3dTokens.textBody,
                                  fontWeight: FontWeight.w500,
                                ),
                              ),
                            ],
                          ),
                        ),
                      )
                      .toList(),
                ),
              ],
            ),
          ),
          const SizedBox(width: Gs3dTokens.space20),
          FilledButton.icon(
            onPressed: onLaunch,
            icon: const Icon(Icons.play_arrow_rounded, size: 16),
            label: const Text('载入视口'),
          ),
        ],
      ),
    );
  }

  // 4. 帮助与技术规范视图。
  Widget _buildHelpView() {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const _ViewHeader(
          icon: Icons.help_outline_rounded,
          title: '帮助与技术规范',
          subtitle: '熟悉三维视口交互手势、GS3D 格式规格与系统渲染管线',
        ),
        const SizedBox(height: Gs3dTokens.space24),

        const Row(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Expanded(
              child: _InfoBox(
                title: '视口交互快捷键指南',
                icon: Icons.mouse_rounded,
                items: [
                  {'label': '旋转视角', 'val': '鼠标左键拖拽'},
                  {'label': '平移视口', 'val': '右键/中键拖拽 或 Shift+左键'},
                  {'label': '缩放视野', 'val': '滚轮滚动'},
                  {'label': '复位视角', 'val': 'Ctrl+R / 顶栏视角复位'},
                  {'label': '新建工程', 'val': 'Ctrl+N'},
                  {'label': '打开工程', 'val': 'Ctrl+O'},
                  {'label': '抽屉收放', 'val': 'Ctrl+B'},
                ],
              ),
            ),
            SizedBox(width: Gs3dTokens.space16),
            Expanded(
              child: _InfoBox(
                title: 'GS3D v2 数据格式标准',
                icon: Icons.architecture_rounded,
                items: [
                  {'label': '编码格式', 'val': '显式小端 (Little-Endian)'},
                  {'label': '步长对齐', 'val': 'point_stride 严格对齐校验'},
                  {'label': '空间索引', 'val': '动态多级八叉树 LOD 金字塔'},
                  {'label': '流式加载', 'val': '多线程后台预取，按需解压'},
                  {'label': '单一渲染核心', 'val': 'C-ABI 单进程 Vulkan 引擎'},
                ],
              ),
            ),
          ],
        ),
      ],
    );
  }

  // 5. 关于视图。
  Widget _buildAboutView() {
    return ListenableBuilder(
      listenable: widget.service,
      builder: (context, _) {
        final gpuLabel = widget.service.activeGpuDisplayName;
        final platformLabel = Platform.isWindows
            ? 'Flutter Desktop (Windows)'
            : Platform.isAndroid
            ? 'Flutter (Android)'
            : Platform.isIOS
            ? 'Flutter (iOS)'
            : 'Flutter Desktop';
        final platformEdition =
            Platform.isWindows || Platform.isLinux || Platform.isMacOS
            ? 'Multiplatform Desktop'
            : 'Mobile';

        return Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text('关于', style: Gs3dType.title1),
            const SizedBox(height: Gs3dTokens.space4),
            Text(
              'GeoScatter3D 现代化工业级图形工作台软件信息与系统环境',
              style: Gs3dType.body.copyWith(fontSize: 13),
            ),
            const SizedBox(height: Gs3dTokens.space24),

            ConstrainedBox(
              constraints: const BoxConstraints(maxWidth: 680),
              child: Gs3dCard(
                padding: const EdgeInsets.all(Gs3dTokens.space24),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Row(
                      children: [
                        Container(
                          width: 52,
                          height: 52,
                          decoration: BoxDecoration(
                            gradient: const LinearGradient(
                              colors: [
                                Gs3dTokens.accent,
                                Gs3dPalette.systemIndigo,
                              ],
                              begin: Alignment.topLeft,
                              end: Alignment.bottomRight,
                            ),
                            borderRadius: BorderRadius.circular(
                              Gs3dTokens.radiusLg,
                            ),
                            boxShadow: Gs3dTokens.accentGlow(
                              alpha: 0.3,
                              blur: 8,
                            ),
                          ),
                          alignment: Alignment.center,
                          child: const Text(
                            'G',
                            style: TextStyle(
                              fontSize: 26,
                              fontWeight: FontWeight.w800,
                              color: Gs3dTokens.textOnAccent,
                            ),
                          ),
                        ),
                        const SizedBox(width: Gs3dTokens.space16),
                        Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            const Text(
                              'GeoScatter3D Studio',
                              style: TextStyle(
                                fontSize: 18,
                                fontWeight: FontWeight.w700,
                                color: Gs3dTokens.textPrimary,
                                letterSpacing: -0.3,
                              ),
                            ),
                            Text(
                              '版本 0.1.0 · $platformEdition',
                              style: TextStyle(
                                fontSize: 11.5,
                                color: Gs3dTokens.textDisabled,
                              ),
                            ),
                          ],
                        ),
                      ],
                    ),
                    const SizedBox(height: Gs3dTokens.space20),
                    const Gs3dDivider(height: 1),
                    const SizedBox(height: Gs3dTokens.space12),
                    _SpecRow(label: '用户界面框架', value: platformLabel),
                    _SpecRow(
                      label: '渲染计算内核',
                      value: Platform.isAndroid
                          ? 'Vulkan 1.3 / Native C-ABI FFI'
                          : Platform.isIOS
                          ? 'iOS 原生渲染后端待接入'
                          : 'Vulkan 1.3 / Pure C-ABI FFI',
                    ),
                    const _SpecRow(
                      label: '空间组织引擎',
                      value: 'GS3D v2 Dynamic Octree LOD',
                    ),
                    _SpecRow(label: '当前物理显卡', value: gpuLabel),
                    _SpecRow(
                      label: '控制面协议',
                      value:
                          'JSON-RPC 2.0 (${widget.service.getAvailableActions().length} 项原子 UI 动作引擎)',
                    ),
                  ],
                ),
              ),
            ),
          ],
        );
      },
    );
  }
}

/// 视图头部：图标底衬 + 标题 + 副标题。
class _ViewHeader extends StatelessWidget {
  const _ViewHeader({
    required this.icon,
    required this.title,
    required this.subtitle,
  });

  final IconData icon;
  final String title;
  final String subtitle;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Container(
          width: 36,
          height: 36,
          decoration: BoxDecoration(
            color: Gs3dTokens.accentSubtle,
            borderRadius: BorderRadius.circular(Gs3dTokens.radiusMd),
          ),
          child: Icon(icon, color: Gs3dTokens.accent, size: 20),
        ),
        const SizedBox(width: Gs3dTokens.space12),
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(title, style: Gs3dType.title2),
              const SizedBox(height: Gs3dTokens.space2),
              Text(
                subtitle,
                style: Gs3dType.caption,
                maxLines: 2,
                overflow: TextOverflow.ellipsis,
              ),
            ],
          ),
        ),
      ],
    );
  }
}

class _InfoBox extends StatelessWidget {
  const _InfoBox({
    required this.title,
    required this.icon,
    required this.items,
  });

  final String title;
  final IconData icon;
  final List<Map<String, String>> items;

  @override
  Widget build(BuildContext context) {
    return Gs3dCard(
      padding: const EdgeInsets.all(Gs3dTokens.space16 + 2),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Icon(icon, size: 18, color: Gs3dTokens.accent),
              const SizedBox(width: Gs3dTokens.space8),
              Text(
                title,
                style: Gs3dType.headline.copyWith(
                  fontSize: 14.5,
                  fontWeight: FontWeight.w700,
                ),
              ),
            ],
          ),
          const SizedBox(height: Gs3dTokens.space12),
          ...items.map(
            (it) => Padding(
              padding: const EdgeInsets.symmetric(vertical: 6),
              child: Row(
                mainAxisAlignment: MainAxisAlignment.spaceBetween,
                children: [
                  Expanded(
                    child: Text(
                      it['label']!,
                      style: Gs3dType.body,
                      overflow: TextOverflow.ellipsis,
                    ),
                  ),
                  const SizedBox(width: Gs3dTokens.space8),
                  Container(
                    padding: const EdgeInsets.symmetric(
                      horizontal: 6,
                      vertical: 2,
                    ),
                    decoration: BoxDecoration(
                      color: Gs3dTokens.surfaceSunken,
                      borderRadius: BorderRadius.circular(Gs3dTokens.radiusXs),
                    ),
                    child: Text(
                      it['val']!,
                      style: Gs3dType.mono.copyWith(fontSize: 11),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _SpecRow extends StatelessWidget {
  const _SpecRow({required this.label, required this.value});

  final String label;
  final String value;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: Gs3dTokens.space8),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          Text(label, style: Gs3dType.body),
          const SizedBox(width: Gs3dTokens.space16),
          Flexible(
            child: Text(
              value,
              style: Gs3dType.bodyStrong,
              textAlign: TextAlign.right,
              overflow: TextOverflow.ellipsis,
            ),
          ),
        ],
      ),
    );
  }
}
