import 'package:flutter/material.dart';
import 'src/ffi/geoscatter3d_service.dart';
import 'src/layouts/analysis_rail_layout.dart';
import 'src/layouts/floating_dock_layout.dart';
import 'src/layouts/standard_workbench_layout.dart';
import 'src/panels/bottom_status_bar.dart';
import 'src/panels/top_menu_bar.dart';
import 'src/theme/app_theme.dart';
import 'src/welcome/welcome_page.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();

  // 初始化 C++ FFI 引擎
  final service = GeoScatter3dService();
  service.initialize();

  runApp(GeoScatter3dApp(service: service));
}

class GeoScatter3dApp extends StatelessWidget {
  final GeoScatter3dService service;

  const GeoScatter3dApp({
    super.key,
    required this.service,
  });

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'GeoScatter3D 三维散点系统 (Flutter)',
      debugShowCheckedModeBanner: false,
      theme: AppTheme.lightTheme(),
      home: ListenableBuilder(
        listenable: service,
        builder: (context, _) {
          return AnimatedSwitcher(
            duration: const Duration(milliseconds: 260),
            switchInCurve: Curves.easeOutCubic,
            switchOutCurve: Curves.easeInCubic,
            child: service.isWorkbenchActive
                ? GeoScatter3dWorkbench(key: const ValueKey('workbench'), service: service)
                : WelcomePage(key: const ValueKey('welcome'), service: service),
          );
        },
      ),
    );
  }
}

class GeoScatter3dWorkbench extends StatelessWidget {
  final GeoScatter3dService service;

  const GeoScatter3dWorkbench({
    super.key,
    required this.service,
  });

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Column(
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

          // 3. 底部状态栏
          BottomStatusBar(service: service),
        ],
      ),
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
