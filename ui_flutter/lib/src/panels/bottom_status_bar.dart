import 'package:flutter/material.dart';

import '../ffi/geoscatter3d_service.dart';
import '../theme/gs3d_tokens.dart';
import '../welcome/welcome_ui_keys.dart';
import '../widgets/fluid.dart';

/// 只读元信息文本样式（等宽、次级色）。
const TextStyle _meta = TextStyle(
  fontSize: 11.5,
  height: 1.2,
  color: Gs3dTokens.textTertiary,
);

/// 数值文本样式（等宽、表格数字对齐）。
const TextStyle _mono = TextStyle(
  fontSize: 11.5,
  height: 1.2,
  color: Gs3dTokens.textSecondary,
  fontFamily: 'Consolas',
  fontFeatures: [FontFeature.tabularFigures()],
);

/// 分组间隔。
const SizedBox _sep = SizedBox(width: Gs3dTokens.space12);

/// 底部状态栏。
///
/// 实时遥测：帧率 / 可见点数 / 瓦片缓存 / 活动 GPU / 相机坐标。
/// 可点击项带悬停高亮与按压反馈，纯读数项保持静态；整体为毛玻璃条，
/// 顶部单边发丝线，与顶栏呼应。
class BottomStatusBar extends StatelessWidget {
  final GeoScatter3dService service;

  const BottomStatusBar({super.key, required this.service});

  @override
  Widget build(BuildContext context) {
    final a11y = context.gs3dA11y;

    return Container(
      key: WorkbenchUiKeys.bottomStatusBar,
      height: 30,
      padding: const EdgeInsets.symmetric(horizontal: Gs3dTokens.space8),
      decoration: BoxDecoration(
        color: Gs3dTokens.surfaceHeader,
        border: Border(
          top: BorderSide(
            color: a11y.highContrast
                ? Gs3dTokens.borderStrong
                : Gs3dTokens.hairline,
          ),
        ),
      ),
      child: ListenableBuilder(
        listenable: service,
        builder: (context, _) {
          final gpuName = service.activeGpuDisplayName;

          final statusRow = Row(
            children: [
              _StatusItem(
                icon: Icons.speed_rounded,
                label: '${service.fps.toStringAsFixed(1)} FPS',
                value: '${service.frameTimeMs.toStringAsFixed(2)} ms',
                accent: true,
                onTap: service.togglePerformancePanel,
              ),
              _sep,
              _StatusItem(
                icon: Icons.grain_rounded,
                label: '${service.summary.pointCount} 可见点',
              ),
              _sep,
              _StatusItem(
                icon: Icons.grid_view_rounded,
                label: '瓦片 ${service.loadedTiles}',
                value: '命中 ${service.cacheHitRate.toStringAsFixed(1)}%',
              ),
              _sep,
              _StatusItem(icon: Icons.memory_rounded, label: gpuName),
              _sep,
              const Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Gs3dStatusDot(color: Gs3dTokens.success, size: 7),
                  SizedBox(width: Gs3dTokens.space4 + 2),
                  Text('就绪', style: _meta),
                ],
              ),
              const SizedBox(width: Gs3dTokens.space16),
              Text(service.cameraCoordsString, style: _mono),
              const SizedBox(width: Gs3dTokens.space8),
              const Text('本地坐标 / 空间参考系', style: _meta),
            ],
          );
          // 状态栏内容包含可变长度的 GPU 名称与坐标；保持横向滚动，
          // 避免窄窗口 / 独立组件测试中把遥测文本挤出 RenderFlex。
          return SingleChildScrollView(
            scrollDirection: Axis.horizontal,
            child: statusRow,
          );
        },
      ),
    );
  }
}

/// 单个遥测项：图标 + 主值（+ 可选副值）。
///
/// 提供 [onTap] 时包裹 [Gs3dPressable]，具备悬停底色与按下收缩。
class _StatusItem extends StatelessWidget {
  const _StatusItem({
    required this.icon,
    required this.label,
    this.value,
    this.accent = false,
    this.onTap,
  });

  final IconData icon;
  final String label;
  final String? value;

  /// 强调项（如帧率）使用强调色图标与加粗正文。
  final bool accent;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    final a11y = context.gs3dA11y;

    final content = Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Icon(
          icon,
          size: 13,
          color: accent ? Gs3dTokens.accent : Gs3dTokens.textTertiary,
        ),
        const SizedBox(width: Gs3dTokens.space4 + 1),
        Text(
          label,
          style: TextStyle(
            fontSize: 11.5,
            height: 1.2,
            color: accent ? Gs3dTokens.textBody : Gs3dTokens.textSecondary,
            fontWeight: accent ? FontWeight.w600 : FontWeight.w400,
            fontFeatures: const [FontFeature.tabularFigures()],
          ),
        ),
        if (value != null) ...[
          const SizedBox(width: Gs3dTokens.space8),
          Text(value!, style: _mono),
        ],
      ],
    );

    if (onTap == null) {
      return Padding(
        padding: const EdgeInsets.symmetric(horizontal: Gs3dTokens.space8),
        child: content,
      );
    }

    return Gs3dPressable(
      onTap: onTap,
      hoverScale: 0,
      builder: (context, state) => AnimatedContainer(
        duration: a11y.resolve(Gs3dMotion.micro),
        curve: Gs3dMotion.standardCurve,
        margin: const EdgeInsets.symmetric(vertical: Gs3dTokens.space4),
        padding: const EdgeInsets.symmetric(horizontal: Gs3dTokens.space8),
        decoration: BoxDecoration(
          color: state == Gs3dInteractionState.idle
              ? Colors.transparent
              : Gs3dTokens.surfaceHover,
          borderRadius: BorderRadius.circular(Gs3dTokens.radiusXs),
        ),
        child: content,
      ),
    );
  }
}
