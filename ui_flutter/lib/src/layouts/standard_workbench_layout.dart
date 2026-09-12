import 'package:flutter/material.dart';
import '../ffi/geoscatter3d_service.dart';
import '../panels/center_viewport.dart';

/// 极简全屏视口工作台布局 (Fullscreen Viewport Workbench)
///
/// 移除左右停靠面板与侧滑抽屉，中央 Vulkan 3D 视口最大化铺满全幅。
class StandardWorkbenchLayout extends StatelessWidget {
  final GeoScatter3dService service;

  const StandardWorkbenchLayout({
    super.key,
    required this.service,
  });

  @override
  Widget build(BuildContext context) {
    return CenterViewport(service: service);
  }
}

