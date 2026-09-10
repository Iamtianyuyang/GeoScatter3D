import 'package:flutter/material.dart';

import '../../ffi/geoscatter3d_service.dart';
import '../../theme/gs3d_tokens.dart';
import '../../widgets/fluid.dart';

/// 平板底部拇指动作条（高 ≥ [Gs3dTokens.bottomBarTouchHeight]）。
///
/// 架构红线 R2：全部按钮**只**经 `service.executeAction` 走控制面动作路由
/// （与 AI / MCP 入口同构），禁止直接调内部 setter 绕过动作路由。
/// 引擎依赖类动作（相机 / 渲染 / 截图等）在引擎不可用时保持可达——
/// 执行后由 [Gs3dService] 返回显式错误并弹 SnackBar（R3，不置灰）。
class TabletActionBar extends StatelessWidget {
  const TabletActionBar({super.key, required this.service});

  final GeoScatter3dService service;

  /// 执行动作并统一反馈：失败（含 R3 引擎降级）弹显式 SnackBar。
  void _run(BuildContext context, String actionId,
      [Map<String, dynamic>? params]) {
    final result = service.executeAction(actionId, params);
    if (result['success'] == true) return;
    final message = result['message'] as String? ?? '动作执行失败';
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(
        content: Text(message),
        duration: const Duration(seconds: 3),
        behavior: SnackBarBehavior.floating,
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Container(
      key: const ValueKey('tablet.action_bar'),
      height: Gs3dTokens.bottomBarTouchHeight,
      decoration: const BoxDecoration(
        color: Gs3dTokens.surfaceHeader,
        border: Border(top: BorderSide(color: Gs3dTokens.hairline)),
      ),
      child: ListenableBuilder(
        listenable: service,
        builder: (context, _) {
          return SingleChildScrollView(
            scrollDirection: Axis.horizontal,
            padding: const EdgeInsets.symmetric(
              horizontal: Gs3dTokens.space8,
            ),
            child: Row(
              children: [
                _ActionBarButton(
                  key: const ValueKey('tablet.actionbar.reset_camera'),
                  icon: Icons.refresh_rounded,
                  label: '复位视角',
                  onTap: () => _run(context, 'workbench.camera.reset'),
                ),
                _ActionBarButton(
                  key: const ValueKey('tablet.actionbar.zoom_in'),
                  icon: Icons.zoom_in_rounded,
                  label: '放大',
                  onTap: () => _run(context, 'workbench.camera.set_view',
                      {'zoom': (service.cameraZoom * 1.2).clamp(0.05, 50.0)}),
                ),
                _ActionBarButton(
                  key: const ValueKey('tablet.actionbar.zoom_out'),
                  icon: Icons.zoom_out_rounded,
                  label: '缩小',
                  onTap: () => _run(context, 'workbench.camera.set_view',
                      {'zoom': (service.cameraZoom / 1.2).clamp(0.05, 50.0)}),
                ),
                _ActionBarButton(
                  key: const ValueKey('tablet.actionbar.pan_toggle'),
                  icon: Icons.open_with_rounded,
                  label: '平移模式',
                  active: service.isPanning,
                  onTap: () => _run(context, 'workbench.camera.set_panning',
                      {'panning': !service.isPanning}),
                ),
                _divider,
                _ActionBarButton(
                  key: const ValueKey('tablet.actionbar.measure_mode'),
                  icon: Icons.straighten_rounded,
                  label: '标尺测距',
                  active: service.isMeasurementMode,
                  onTap: () => _run(context, 'workbench.measure.mode'),
                ),
                _ActionBarButton(
                  key: const ValueKey('tablet.actionbar.stats'),
                  icon: Icons.bar_chart_rounded,
                  label: '统计',
                  onTap: () => _run(context, 'workbench.stats.get'),
                ),
                _divider,
                _ActionBarButton(
                  key: const ValueKey('tablet.actionbar.theme_cycle'),
                  icon: Icons.palette_outlined,
                  label: '主题',
                  onTap: () => _run(context, 'workbench.theme.cycle'),
                ),
                _ActionBarButton(
                  key: const ValueKey('tablet.actionbar.screenshot'),
                  icon: Icons.camera_alt_outlined,
                  label: '截图',
                  onTap: () => _run(context, 'workbench.screenshot'),
                ),
                _ActionBarButton(
                  key: const ValueKey('tablet.actionbar.layout'),
                  icon: Icons.view_quilt_outlined,
                  label: '布局',
                  onTap: () => _run(context, 'workbench.layout.set',
                      {'mode': service.layoutMode.name}),
                ),
                _divider,
                _ActionBarButton(
                  key: const ValueKey('tablet.actionbar.command'),
                  icon: Icons.terminal,
                  label: '命令',
                  accent: true,
                  onTap: () => _run(context, 'workbench.overlay.command_palette',
                      {'open': true}),
                ),
                _ActionBarButton(
                  key: const ValueKey('tablet.actionbar.shortcuts'),
                  icon: Icons.keyboard_alt_outlined,
                  label: '快捷键',
                  onTap: () =>
                      _run(context, 'workbench.overlay.shortcut_help',
                          {'open': true}),
                ),
              ],
            ),
          );
        },
      ),
    );
  }

  Widget get _divider => Container(
        width: 1,
        height: Gs3dTokens.space24 + 4,
        margin: const EdgeInsets.symmetric(horizontal: Gs3dTokens.space4),
        color: Gs3dTokens.hairline,
      );
}

/// 动作条按钮：图标 + 文字，视觉紧凑、命中区 ≥48（Gs3dTouchTarget）。
class _ActionBarButton extends StatelessWidget {
  const _ActionBarButton({
    super.key,
    required this.icon,
    required this.label,
    required this.onTap,
    this.active = false,
    this.accent = false,
  });

  final IconData icon;
  final String label;
  final VoidCallback onTap;
  final bool active;

  /// 强调态（如「命令」入口）。
  final bool accent;

  @override
  Widget build(BuildContext context) {
    final fg = accent
        ? Gs3dTokens.accent
        : (active ? Gs3dTokens.accent : Gs3dTokens.textBody);
    return Gs3dPressable(
      onTap: onTap,
      hoverScale: 0,
      minTouchSize: Gs3dTokens.hitTargetTouch,
      builder: (context, state) {
        final bg = switch (state) {
          Gs3dInteractionState.pressed => Gs3dTokens.surfaceSunken,
          Gs3dInteractionState.hovered => Gs3dTokens.surfaceHover,
          Gs3dInteractionState.idle =>
            active ? Gs3dTokens.accentSubtle : Colors.transparent,
        };
        return AnimatedContainer(
          duration: Gs3dMotion.micro,
          curve: Gs3dMotion.standardCurve,
          margin: const EdgeInsets.symmetric(
            horizontal: Gs3dTokens.space4,
            vertical: Gs3dTokens.space8,
          ),
          padding: const EdgeInsets.symmetric(horizontal: Gs3dTokens.space8),
          decoration: BoxDecoration(
            color: bg,
            borderRadius: BorderRadius.circular(Gs3dTokens.radiusMd),
            border: Border.all(
              color: active ? Gs3dTokens.accentBorder : Colors.transparent,
            ),
          ),
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(icon, size: 20, color: fg),
              const SizedBox(height: 2),
              Text(
                label,
                style: TextStyle(
                  fontSize: 10.5,
                  fontWeight: FontWeight.w600,
                  color: fg,
                  fontFamily: Gs3dType.fontFamily,
                ),
              ),
            ],
          ),
        );
      },
    );
  }
}
