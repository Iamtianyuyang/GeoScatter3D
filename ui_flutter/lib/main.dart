import 'dart:async';
import 'dart:io';
import 'dart:ui' show AppExitResponse;

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'src/ffi/geoscatter3d_service.dart';
import 'src/layouts/analysis_rail_layout.dart';
import 'src/layouts/floating_dock_layout.dart';
import 'src/layouts/standard_workbench_layout.dart';
import 'src/panels/bottom_status_bar.dart';
import 'src/panels/performance_panel.dart';
import 'src/panels/tablet/tablet_action_bar.dart';
import 'src/panels/tile_inspector_dialog.dart';
import 'src/panels/top_menu_bar.dart';
import 'src/theme/gs3d_form_factor.dart';
import 'src/theme/gs3d_theme.dart';
import 'src/theme/gs3d_tokens.dart';
import 'src/welcome/welcome_page.dart';
import 'src/widgets/command_palette.dart';
import 'src/widgets/shortcut_overlay.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();

  // 先同步设置样式；窗口模式切换放到首帧之后，避免 Android 冷启动等待系统窗口回调。
  if (Platform.isAndroid || Platform.isIOS) {
    SystemChrome.setSystemUIOverlayStyle(
      const SystemUiOverlayStyle(
        statusBarColor: Colors.transparent,
        systemNavigationBarColor: Colors.transparent,
        statusBarIconBrightness: Brightness.dark,
        systemNavigationBarIconBrightness: Brightness.dark,
        systemStatusBarContrastEnforced: false,
        systemNavigationBarContrastEnforced: false,
      ),
    );
  }

  // 先显示欢迎页，再异步准备持久化沙盒和初始化单进程 FFI 引擎。
  // 原生库初始化可能在冷启动时阻塞较久，不能阻塞 Flutter 首帧。
  final service = GeoScatter3dService();
  runApp(GeoScatter3dApp(service: service));
  // 只有首帧绘制完成后，才启动可能阻塞主 isolate 的窗口模式切换与 FFI 初始化。
  WidgetsBinding.instance.addPostFrameCallback((_) {
    unawaited(_configureSystemUi());
    unawaited(service.bootstrap());
  });
}

Future<void> _configureSystemUi() async {
  // Android 平板采用透明系统栏，由 Flutter 安全区统一给内容让位。
  // 这样顶部状态栏和底部手势区域不会再覆盖菜单栏或底部状态栏。
  if (!Platform.isAndroid && !Platform.isIOS) return;
  await SystemChrome.setEnabledSystemUIMode(SystemUiMode.edgeToEdge);
}

class GeoScatter3dApp extends StatefulWidget {
  final GeoScatter3dService service;

  const GeoScatter3dApp({super.key, required this.service});

  @override
  State<GeoScatter3dApp> createState() => _GeoScatter3dAppState();
}

class _GeoScatter3dAppState extends State<GeoScatter3dApp> {
  late final AppLifecycleListener _lifecycle;
  GeoScatter3dService get service => widget.service;

  bool get _hasOpenWorkbenchOverlay =>
      service.isCommandPaletteOpen ||
      service.isShortcutOverlayOpen ||
      service.isPerformancePanelOpen ||
      service.isTileInspectorOpen;

  /// Android 返回键采用移动端常见的逐层返回语义：先关闭当前覆盖层，
  /// 再离开工作台回到欢迎页；欢迎页根路由才允许系统退出应用。
  void _handleSystemBack() {
    if (service.isCommandPaletteOpen) {
      service.toggleCommandPalette(false);
    } else if (service.isShortcutOverlayOpen) {
      service.toggleShortcutOverlay(false);
    } else if (service.isPerformancePanelOpen) {
      service.togglePerformancePanel(false);
    } else if (service.isTileInspectorOpen) {
      service.toggleTileInspector(false);
    } else if (service.isWorkbenchActive) {
      service.closeWorkbench();
    }
  }

  @override
  void initState() {
    super.initState();
    _lifecycle = AppLifecycleListener(
      onExitRequested: () async {
        await service.shutdownAsync();
        return AppExitResponse.exit;
      },
    );
  }

  @override
  void dispose() {
    _lifecycle.dispose();
    service.shutdown();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'GeoScatter3D 三维散点系统 (Flutter)',
      debugShowCheckedModeBanner: false,
      theme: Gs3dTheme.light(),
      // 注入无障碍偏好与形态因子档位（二者并排，供全应用通过
      // context.gs3dA11y / context.gs3dFormFactor 统一读取）。
      builder: (context, child) => Gs3dFormFactorScope(
        slot: Gs3dFormFactorScope.resolve(
          MediaQuery.maybeOf(context) ?? const MediaQueryData(),
        ),
        child: Gs3dA11yScope(
          a11y: Gs3dA11y.fromContext(context),
          child: ColoredBox(
            // 系统栏采用透明 edge-to-edge；安全区外必须延续工具栏表面，
            // 否则底部手势区域会变成突兀的白色横条，与底部状态栏断层。
            color: Gs3dTokens.surfaceHeader,
            child: SafeArea(
              top: true,
              bottom: true,
              left: false,
              right: false,
              maintainBottomViewPadding: true,
              child: child ?? const SizedBox.shrink(),
            ),
          ),
        ),
      ),
      home: ListenableBuilder(
        listenable: service,
        builder: (context, _) {
          return PopScope<void>(
            canPop: !service.isWorkbenchActive && !_hasOpenWorkbenchOverlay,
            onPopInvokedWithResult: (didPop, _) {
              if (!didPop) _handleSystemBack();
            },
            child: AnimatedSwitcher(
              duration: const Duration(milliseconds: 260),
              switchInCurve: Curves.easeOutCubic,
              switchOutCurve: Curves.easeInCubic,
              child: service.isWorkbenchActive
                  ? GeoScatter3dWorkbench(
                      key: const ValueKey('workbench'),
                      service: service,
                    )
                  : WelcomePage(
                      key: const ValueKey('welcome'),
                      service: service,
                    ),
            ),
          );
        },
      ),
    );
  }
}

class GeoScatter3dWorkbench extends StatelessWidget {
  final GeoScatter3dService service;

  const GeoScatter3dWorkbench({super.key, required this.service});

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: service,
      builder: (context, _) {
        return Scaffold(
          body: Stack(
            children: [
              Column(
                children: [
                  // 1. 顶部菜单栏 (含布局切换器、工区状态、返回欢迎页)
                  TopMenuBar(service: service),

                  // 2. 动态主工作区布局：标准三栏 / 悬浮胶囊 Dock / 暗色分析舱
                  Expanded(
                    child: AnimatedSwitcher(
                      duration: const Duration(milliseconds: 220),
                      switchInCurve: Curves.easeOutCubic,
                      switchOutCurve: Curves.easeInCubic,
                      child: _buildActiveLayout(),
                    ),
                  ),

                  // 3. 平板拇指动作条（仅触控档；位于状态栏之上让位）
                  if (context.isTouchPreferred)
                    TabletActionBar(service: service),

                  // 4. 底部状态栏（只读遥测保留）
                  BottomStatusBar(service: service),
                ],
              ),

              // 全局模态覆盖层与诊断视察器
              if (service.isCommandPaletteOpen)
                CommandPalette(service: service),

              if (service.isShortcutOverlayOpen)
                ShortcutOverlay(service: service),

              if (service.isPerformancePanelOpen)
                PerformancePanel(service: service),

              if (service.isTileInspectorOpen)
                TileInspectorDialog(service: service),
            ],
          ),
        );
      },
    );
  }

  Widget _buildActiveLayout() {
    switch (service.layoutMode) {
      case WorkbenchLayoutMode.standard:
        return StandardWorkbenchLayout(
          key: const ValueKey('layout_standard'),
          service: service,
        );
      case WorkbenchLayoutMode.floatingDock:
        return FloatingDockLayout(
          key: const ValueKey('layout_floating_dock'),
          service: service,
        );
      case WorkbenchLayoutMode.analysisRail:
        return AnalysisRailLayout(
          key: const ValueKey('layout_analysis_rail'),
          service: service,
        );
    }
  }
}
