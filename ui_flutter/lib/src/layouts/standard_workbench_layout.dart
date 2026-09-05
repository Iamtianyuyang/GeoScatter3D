import 'package:flutter/material.dart';
import '../ffi/geoscatter3d_service.dart';
import '../panels/center_viewport.dart';
import '../panels/left_dock_panel.dart';
import '../panels/right_dock_panel.dart';

/// 方案 A · 标准三栏工作台布局 (Standard Workbench Layout)
class StandardWorkbenchLayout extends StatelessWidget {
  final GeoScatter3dService service;

  const StandardWorkbenchLayout({
    super.key,
    required this.service,
  });

  @override
  Widget build(BuildContext context) {
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
