import 'dart:math' as math;
import 'dart:ui' as ui;
import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import '../ffi/geoscatter3d_service.dart';
import '../ffi/gs3d_engine.dart';
import '../theme/app_tokens.dart';

/// 视口只投影服务状态并转发输入；不直接调度原生引擎。
class ViewportCanvas extends StatefulWidget {
  final Gs3dEngine? engine;
  final GeoScatter3dService? service;
  final bool autoLoadSample;
  final bool embedded;

  const ViewportCanvas({
    super.key,
    this.engine,
    this.service,
    this.autoLoadSample = true,
    this.embedded = false,
  }) : assert(engine == null || service == null);

  @override
  State<ViewportCanvas> createState() => _ViewportCanvasState();
}

class _ViewportCanvasState extends State<ViewportCanvas>
    with SingleTickerProviderStateMixin, WidgetsBindingObserver {
  late final GeoScatter3dService _service;
  late final Ticker _ticker;
  double _lastScale = 1;

  @override
  void initState() {
    super.initState();
    _service = widget.service ?? GeoScatter3dService(engine: widget.engine);
    WidgetsBinding.instance.addObserver(this);
    _ticker = createTicker((_) => _service.renderNextFrame())..start();
    _service.initialize(loadSample: widget.autoLoadSample);
  }

  void _action(String action, [Map<String, double> values = const {}]) {
    if (!_service.ready) return;
    try {
      _service.executeAction(action, values);
    } catch (error) {
      ScaffoldMessenger.of(
        context,
      ).showSnackBar(SnackBar(content: Text(error.toString())));
    }
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    _service.setPaused(state != AppLifecycleState.resumed);
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    _ticker.dispose();
    if (widget.service == null) _service.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => ListenableBuilder(
    listenable: _service,
    builder: (context, child) => _buildViewport(context),
  );

  /// 零拷贝通路优先：有 Flutter 纹理时直接合成引擎交换链（无回读、无上传）；
  /// 否则回退到 RGBA 帧的 RawImage。
  Widget _buildFrameSurface() {
    final textureId = _service.textureId;
    if (textureId != null) {
      return Texture(
        key: const ValueKey('viewport_texture'),
        textureId: textureId,
        filterQuality: FilterQuality.none,
      );
    }
    if (_service.frame != null) {
      return _OwnedFrame(image: _service.frame!);
    }
    return Center(
      child: CircularProgressIndicator(
        color: context.tokens.colors.accent,
        strokeWidth: 2,
      ),
    );
  }

  Widget _buildViewport(BuildContext context) {
    if (_service.error != null) {
      return Container(
        key: const ValueKey('vulkan_engine_error'),
        color: const Color(0xFF0F172A),
        padding: const EdgeInsets.all(24),
        alignment: Alignment.center,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const Icon(Icons.error_outline, color: Color(0xFFEF4444), size: 48),
            const SizedBox(height: 16),
            const Text(
              'Vulkan 渲染引擎启动失败',
              style: TextStyle(
                color: Colors.white,
                fontSize: 18,
                fontWeight: FontWeight.bold,
              ),
            ),
            const SizedBox(height: 8),
            Text(
              _service.error!,
              textAlign: TextAlign.center,
              style: const TextStyle(color: Color(0xFF94A3B8), fontSize: 13),
            ),
          ],
        ),
      );
    }

    return LayoutBuilder(
      builder: (context, constraints) {
        // 只上报几何事实（逻辑尺寸 + 设备 dpr）；纹理像素密度策略在服务里决定。
        _service.requestSize(
          constraints.biggest,
          pixelRatio: MediaQuery.devicePixelRatioOf(context),
        );

        return Stack(
          children: [
            // 主视口画布
            Positioned.fill(
              child: Listener(
                onPointerSignal: (event) {
                  if (event is PointerScrollEvent) {
                    _action('camera.input', {
                      'scrollY': -event.scrollDelta.dy / 40,
                    });
                  }
                },
                child: GestureDetector(
                  key: const ValueKey('viewport_gestures'),
                  behavior: HitTestBehavior.opaque,
                  onScaleStart: (_) => _lastScale = 1,
                  onScaleUpdate: (details) {
                    if (details.pointerCount == 1) {
                      _action('camera.input', {
                        'rotDx': details.focalPointDelta.dx,
                        'rotDy': details.focalPointDelta.dy,
                      });
                    } else {
                      _action('camera.input', {
                        'panDx': details.focalPointDelta.dx,
                        'panDy': details.focalPointDelta.dy,
                        'scrollY': math.log(details.scale / _lastScale) * 10,
                      });
                    }
                    _lastScale = details.scale;
                  },
                  child: Container(
                    color: context.tokens.colors.viewport,
                    child: _buildFrameSurface(),
                  ),
                ),
              ),
            ),

            _buildViewportControls(context),
          ],
        );
      },
    );
  }

  /// 相机控制与调试读数共用一层面板材质，避免在内容之上叠加多种浮层风格。
  Widget _buildViewportControls(BuildContext context) {
    final tokens = context.tokens;
    final canControl = _service.ready;

    Widget actionButton({
      required Key key,
      required String tooltip,
      required IconData icon,
      required VoidCallback? onPressed,
    }) {
      return Semantics(
        button: true,
        enabled: onPressed != null,
        label: tooltip,
        child: Tooltip(
          message: tooltip,
          excludeFromSemantics: true,
          child: SizedBox(
            width: tokens.sizes.tapTarget,
            height: tokens.sizes.tapTarget,
            child: Center(
              child: DecoratedBox(
                decoration: BoxDecoration(
                  color: tokens.colors.hudSurfaceRaised,
                  shape: BoxShape.circle,
                  border: Border.all(
                    color: tokens.colors.hudBorder,
                    width: tokens.effects.hairline,
                  ),
                  boxShadow: [tokens.effects.controlShadow],
                ),
                child: Material(
                  type: MaterialType.transparency,
                  child: InkWell(
                    key: key,
                    onTap: onPressed,
                    customBorder: const CircleBorder(),
                    highlightColor: tokens.colors.accentSoft,
                    splashColor: tokens.colors.accentSoft,
                    child: SizedBox(
                      width: tokens.sizes.controlButtonSize,
                      height: tokens.sizes.controlButtonSize,
                      child: Icon(
                        icon,
                        size: tokens.sizes.controlIconSize,
                        color: onPressed == null
                            ? tokens.colors.textMuted
                            : tokens.colors.textPrimary,
                      ),
                    ),
                  ),
                ),
              ),
            ),
          ),
        ),
      );
    }

    final controls = Align(
      alignment: Alignment.bottomRight,
      child: Padding(
        padding: EdgeInsets.all(tokens.spacing.lg),
        child: ConstrainedBox(
          constraints: BoxConstraints(
            maxWidth: tokens.sizes.controlBarMaxWidth,
          ),
          child: ClipRRect(
            borderRadius: tokens.radii.panelRadius,
            child: BackdropFilter(
              filter: ui.ImageFilter.blur(
                sigmaX: tokens.effects.hudBlur,
                sigmaY: tokens.effects.hudBlur,
              ),
              child: DecoratedBox(
                decoration: BoxDecoration(
                  gradient: LinearGradient(
                    begin: Alignment.topLeft,
                    end: Alignment.bottomRight,
                    colors: [
                      tokens.colors.hudSurfaceTop,
                      tokens.colors.hudSurfaceBottom,
                    ],
                  ),
                  borderRadius: tokens.radii.panelRadius,
                  border: Border.all(
                    color: tokens.colors.hudBorder,
                    width: tokens.effects.hairline,
                  ),
                  boxShadow: [tokens.effects.panelShadow],
                ),
                child: Padding(
                  padding: EdgeInsets.symmetric(
                    horizontal: tokens.spacing.md,
                    vertical: tokens.spacing.xs,
                  ),
                  child: Row(
                    mainAxisSize: MainAxisSize.min,
                    children: [
                      if (kDebugControls) ...[
                        Flexible(
                          child: ConstrainedBox(
                            constraints: BoxConstraints(
                              maxWidth: tokens.sizes.debugStatsMaxWidth,
                            ),
                            child: Padding(
                              padding: EdgeInsets.symmetric(
                                horizontal: tokens.spacing.md,
                              ),
                              child: Text(
                                _service.debugStats.isEmpty
                                    ? 'debug: 等待遥测'
                                    : _service.debugStats,
                                maxLines: 1,
                                overflow: TextOverflow.ellipsis,
                                style: tokens.typography.meta.copyWith(
                                  color: tokens.colors.textPrimary,
                                ),
                              ),
                            ),
                          ),
                        ),
                        SizedBox(width: tokens.spacing.md),
                        Container(
                          width: tokens.effects.hairline,
                          height: tokens.sizes.controlDividerHeight,
                          color: tokens.colors.hudDivider,
                        ),
                        SizedBox(width: tokens.spacing.md),
                        actionButton(
                          key: const ValueKey('camera_zoom_out'),
                          tooltip: '缩小',
                          icon: Icons.remove,
                          onPressed: canControl
                              ? () => _action('camera.input', {
                                  'scrollY': -kDebugZoomStep,
                                })
                              : null,
                        ),
                        SizedBox(width: tokens.spacing.sm),
                        actionButton(
                          key: const ValueKey('camera_zoom_in'),
                          tooltip: '放大',
                          icon: Icons.add,
                          onPressed: canControl
                              ? () => _action('camera.input', {
                                  'scrollY': kDebugZoomStep,
                                })
                              : null,
                        ),
                        SizedBox(width: tokens.spacing.sm),
                      ],
                      actionButton(
                        key: const ValueKey('camera_reset'),
                        tooltip: '复位相机',
                        icon: Icons.center_focus_strong,
                        onPressed: canControl
                            ? () => _action('camera.reset')
                            : null,
                      ),
                    ],
                  ),
                ),
              ),
            ),
          ),
        ),
      ),
    );

    return widget.embedded ? controls : SafeArea(child: controls);
  }
}

/// 显示引用单独持有，避免服务更换帧到下一次 Widget 重建之间引用失效。
class _OwnedFrame extends StatefulWidget {
  const _OwnedFrame({required this.image});
  final ui.Image image;
  @override
  State<_OwnedFrame> createState() => _OwnedFrameState();
}

class _OwnedFrameState extends State<_OwnedFrame> {
  late ui.Image _image;
  @override
  void initState() {
    super.initState();
    _image = widget.image.clone();
  }

  @override
  void didUpdateWidget(_OwnedFrame oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (!identical(oldWidget.image, widget.image)) {
      final previous = _image;
      _image = widget.image.clone();
      previous.dispose();
    }
  }

  @override
  void dispose() {
    _image.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => RawImage(
    key: const ValueKey('viewport_frame'),
    image: _image,
    fit: BoxFit.fill,
  );
}
