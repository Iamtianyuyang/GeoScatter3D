import 'package:flutter/material.dart';
import '../ffi/geoscatter3d_service.dart';
import '../panels/center_viewport.dart';
import '../panels/left_dock_panel.dart';
import '../panels/right_dock_panel.dart';
import '../panels/tablet/tablet_drawer_scaffold.dart';
import '../theme/gs3d_form_factor.dart';

/// 方案 A · 标准三栏工作台布局 (Standard Workbench Layout)
///
/// 按形态因子分支：
///   * expanded（桌面 / 大屏横向平板）：既有三栏布局，像素零变化；
///   * tablet / compact（触控）：左右停靠面板改为 [TabletDrawerScaffold]
///     的滑出抽屉（默认收起、边缘把手唤出），中央视口充满全幅。
class StandardWorkbenchLayout extends StatelessWidget {
  final GeoScatter3dService service;

  const StandardWorkbenchLayout({
    super.key,
    required this.service,
  });

  @override
  Widget build(BuildContext context) {
    // 触控档：抽屉脚手架（面板内容复用既有 LeftDockPanel/RightDockPanel）。
    // 注意：档位判定必须经 context.gs3dFormFactor（单一事实源），禁止散落
    // 宽度阈值判断。
    if (context.isTouchPreferred) {
      return TabletDrawerScaffold(
        center: CenterViewport(service: service),
        leftPanel: LeftDockPanel(service: service),
        rightPanel: RightDockPanel(service: service),
      );
    }
    return ListenableBuilder(
      listenable: service,
      builder: (context, _) {
        return Row(
          children: [
            // 【区域 2】左侧卡片流停靠区 (工区、测量、区域统计)
            if (service.leftDockVisible) LeftDockPanel(service: service),

            // 【区域 3】中央 3D 渲染主视口与快捷工具条
            Expanded(
              child: CenterViewport(service: service),
            ),

            // 【区域 4】右侧属性与渲染设置停靠区
            if (service.rightDockVisible) RightDockPanel(service: service),
          ],
        );
      },
    );
  }
}
