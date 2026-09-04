import 'package:flutter/material.dart';
import 'src/ffi/geoscatter3d_service.dart';
import 'src/panels/bottom_status_bar.dart';
import 'src/panels/center_viewport.dart';
import 'src/panels/left_dock_panel.dart';
import 'src/panels/right_dock_panel.dart';
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
          // 1. 顶部菜单栏
          TopMenuBar(service: service),

          // 2. 主工作区：左停靠面板 + 中心 3D 视口 + 右属性面板
          Expanded(
            child: Row(
              children: [
                // 【区域 2】左侧卡片流停靠区 (完全对齐 media_1788518817411.png)
                LeftDockPanel(service: service),

                // 【区域 3】中央 3D 渲染视口与快捷工具条
                Expanded(
                  child: CenterViewport(service: service),
                ),

                // 【区域 4】右侧属性与渲染设置停靠区
                RightDockPanel(service: service),
              ],
            ),
          ),

          // 3. 底部状态栏
          BottomStatusBar(service: service),
        ],
      ),
    );
  }
}
