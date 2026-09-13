import 'dart:ui';
import 'package:flutter/material.dart';
import '../theme/app_tokens.dart';

/// 浅色玻璃测绘仪器风格的实时遥测浮层。
class TelemetryHud extends StatelessWidget {
  final double fps;
  final double frameTimeMs;
  final double renderTimeMs;
  final int visiblePoints;
  final String lodSummary;
  final String gpuName;
  final String status;
  final String renderPath;

  const TelemetryHud({
    super.key,
    required this.fps,
    required this.frameTimeMs,
    this.renderTimeMs = 0,
    required this.visiblePoints,
    this.lodSummary = '',
    this.gpuName = 'Vulkan GPU',
    this.status = '就绪',
    this.renderPath = '',
  });

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    final colors = tokens.colors;
    final typography = tokens.typography;
    final isReady = status.contains('就绪');
    final transport = renderPath.isEmpty ? 'VULKAN' : 'VULKAN · $renderPath';

    return ConstrainedBox(
      constraints: BoxConstraints(maxWidth: tokens.sizes.hudMaxWidth),
      child: ClipRRect(
        key: const Key('telemetry_hud'),
        borderRadius: tokens.radii.panelRadius,
        child: BackdropFilter(
          filter: ImageFilter.blur(
            sigmaX: tokens.effects.hudBlur,
            sigmaY: tokens.effects.hudBlur,
          ),
          child: DecoratedBox(
            decoration: BoxDecoration(
              gradient: LinearGradient(
                begin: Alignment.topLeft,
                end: Alignment.bottomRight,
                colors: [colors.hudSurfaceTop, colors.hudSurfaceBottom],
              ),
              borderRadius: tokens.radii.panelRadius,
              border: Border.all(
                color: colors.hudBorder,
                width: tokens.effects.hairline,
              ),
              boxShadow: [tokens.effects.panelShadow],
            ),
            child: Padding(
              padding: EdgeInsets.all(tokens.spacing.lg),
              child: Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Row(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Expanded(
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Text(
                              'GEOSCATTER 3D',
                              maxLines: 1,
                              overflow: TextOverflow.ellipsis,
                              style: typography.brand.copyWith(
                                color: colors.textPrimary,
                              ),
                            ),
                            SizedBox(height: tokens.spacing.sm),
                            Text(
                              transport,
                              key: const ValueKey('viewport_render_path'),
                              maxLines: 1,
                              overflow: TextOverflow.ellipsis,
                              style: typography.meta.copyWith(
                                color: colors.textSecondary,
                              ),
                            ),
                          ],
                        ),
                      ),
                      SizedBox(width: tokens.spacing.md),
                      _StatusPill(label: status, isReady: isReady),
                    ],
                  ),
                  SizedBox(height: tokens.spacing.md),
                  _PrimaryMetrics(fps: fps, frameTimeMs: frameTimeMs),
                  SizedBox(height: tokens.spacing.md),
                  Container(
                    height: tokens.effects.hairline,
                    color: colors.hudDivider,
                  ),
                  SizedBox(height: tokens.spacing.md),
                  _SecondaryMetrics(
                    visiblePoints: visiblePoints,
                    renderTimeMs: renderTimeMs,
                  ),
                  SizedBox(height: tokens.spacing.md),
                  Container(
                    height: tokens.effects.hairline,
                    color: colors.hudDivider,
                  ),
                  SizedBox(height: tokens.spacing.sm),
                  if (lodSummary.isNotEmpty) ...[
                    Text(
                      lodSummary,
                      key: const ValueKey('viewport_lod'),
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: typography.meta.copyWith(
                        color: colors.textSecondary,
                      ),
                    ),
                    SizedBox(height: tokens.spacing.xs),
                  ],
                  Text(
                    gpuName,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: typography.meta.copyWith(color: colors.textMuted),
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }

  static String _formatPointCount(int value) {
    final digits = value.abs().toString();
    final buffer = StringBuffer();
    for (var index = 0; index < digits.length; index++) {
      if (index > 0 && (digits.length - index) % 3 == 0) {
        buffer.write(',');
      }
      buffer.write(digits[index]);
    }
    return value < 0 ? '-$buffer' : buffer.toString();
  }
}

class _StatusPill extends StatelessWidget {
  const _StatusPill({required this.label, required this.isReady});

  final String label;
  final bool isReady;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    final colors = tokens.colors;
    final surface = isReady
        ? colors.statusReadySurface
        : colors.hudSurfaceRaised;
    final border = isReady ? colors.statusReadyBorder : colors.hudBorder;
    final foreground = isReady ? colors.statusReadyText : colors.textSecondary;
    final dot = isReady ? colors.statusReady : colors.textMuted;

    return Container(
      key: const ValueKey('viewport_status'),
      padding: EdgeInsets.symmetric(
        horizontal: tokens.spacing.md,
        vertical: tokens.spacing.sm,
      ),
      decoration: BoxDecoration(
        color: surface,
        borderRadius: tokens.radii.pillRadius,
        border: Border.all(color: border, width: tokens.effects.hairline),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Container(
            width: tokens.sizes.statusDot,
            height: tokens.sizes.statusDot,
            decoration: BoxDecoration(color: dot, shape: BoxShape.circle),
          ),
          SizedBox(width: tokens.spacing.sm),
          Text(
            label,
            maxLines: 1,
            overflow: TextOverflow.ellipsis,
            style: tokens.typography.status.copyWith(color: foreground),
          ),
        ],
      ),
    );
  }
}

class _PrimaryMetrics extends StatelessWidget {
  const _PrimaryMetrics({required this.fps, required this.frameTimeMs});

  final double fps;
  final double frameTimeMs;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    final colors = tokens.colors;
    final typography = tokens.typography;
    final fpsBlock = FittedBox(
      fit: BoxFit.scaleDown,
      alignment: Alignment.centerLeft,
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.end,
        children: [
          Text(
            fps.toStringAsFixed(1),
            key: const ValueKey('viewport_metrics'),
            style: typography.metricHero.copyWith(color: colors.accent),
          ),
          Padding(
            padding: EdgeInsets.only(
              left: tokens.spacing.sm,
              bottom: tokens.spacing.xs,
            ),
            child: Text(
              'FPS',
              style: typography.metricUnit.copyWith(
                color: colors.textSecondary,
              ),
            ),
          ),
        ],
      ),
    );
    final frameBlock = Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        FittedBox(
          fit: BoxFit.scaleDown,
          alignment: Alignment.centerLeft,
          child: Row(
            crossAxisAlignment: CrossAxisAlignment.end,
            children: [
              Text(
                frameTimeMs.toStringAsFixed(2),
                style: typography.metric.copyWith(color: colors.textPrimary),
              ),
              Padding(
                padding: EdgeInsets.only(
                  left: tokens.spacing.xs,
                  bottom: tokens.spacing.xs,
                ),
                child: Text(
                  'ms',
                  style: typography.metricUnit.copyWith(
                    color: colors.textSecondary,
                  ),
                ),
              ),
            ],
          ),
        ),
        Text('帧耗时', style: typography.label.copyWith(color: colors.textMuted)),
      ],
    );

    return LayoutBuilder(
      builder: (context, constraints) {
        final textScale = MediaQuery.textScalerOf(context).scale(1);
        final stack =
            textScale > tokens.sizes.metricStackScaleThreshold ||
            constraints.maxWidth < 180;
        if (stack) {
          return Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              fpsBlock,
              SizedBox(height: tokens.spacing.md),
              Container(
                height: tokens.effects.hairline,
                color: colors.hudDivider,
              ),
              SizedBox(height: tokens.spacing.md),
              frameBlock,
            ],
          );
        }
        return Row(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Expanded(child: fpsBlock),
            SizedBox(width: tokens.spacing.xl),
            Container(
              width: tokens.effects.hairline,
              height: tokens.sizes.metricDividerHeight,
              color: colors.hudDivider,
            ),
            SizedBox(width: tokens.spacing.xl),
            Expanded(child: frameBlock),
          ],
        );
      },
    );
  }
}

class _SecondaryMetrics extends StatelessWidget {
  const _SecondaryMetrics({
    required this.visiblePoints,
    required this.renderTimeMs,
  });

  final int visiblePoints;
  final double renderTimeMs;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    final colors = tokens.colors;
    final points = _TelemetryMetric(
      label: '可见点',
      value: TelemetryHud._formatPointCount(visiblePoints),
    );
    final engine = renderTimeMs > 0
        ? _TelemetryMetric(
            key: const ValueKey('viewport_render_cost'),
            label: '引擎',
            value: '${renderTimeMs.toStringAsFixed(2)} ms',
          )
        : null;

    return LayoutBuilder(
      builder: (context, constraints) {
        final textScale = MediaQuery.textScalerOf(context).scale(1);
        final stack =
            engine == null ||
            textScale > tokens.sizes.metricStackScaleThreshold ||
            constraints.maxWidth < 180;
        if (stack) {
          return Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              points,
              if (engine != null) ...[
                SizedBox(height: tokens.spacing.md),
                Container(
                  height: tokens.effects.hairline,
                  color: colors.hudDivider,
                ),
                SizedBox(height: tokens.spacing.md),
                engine,
              ],
            ],
          );
        }
        return Row(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Expanded(child: points),
            SizedBox(width: tokens.spacing.xl),
            Container(
              width: tokens.effects.hairline,
              height: tokens.sizes.metricDividerHeight,
              color: colors.hudDivider,
            ),
            SizedBox(width: tokens.spacing.xl),
            Expanded(child: engine),
          ],
        );
      },
    );
  }
}

class _TelemetryMetric extends StatelessWidget {
  const _TelemetryMetric({super.key, required this.label, required this.value});

  final String label;
  final String value;

  @override
  Widget build(BuildContext context) {
    final tokens = context.tokens;
    final colors = tokens.colors;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          label,
          style: tokens.typography.label.copyWith(color: colors.textSecondary),
        ),
        SizedBox(height: tokens.spacing.xs),
        Text(
          value,
          maxLines: 1,
          overflow: TextOverflow.ellipsis,
          style: tokens.typography.value.copyWith(color: colors.textPrimary),
        ),
      ],
    );
  }
}
