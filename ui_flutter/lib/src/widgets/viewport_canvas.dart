import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';

import '../ffi/geoscatter3d_service.dart';
import '../ffi/gs3d_engine.dart';
import '../theme/app_tokens.dart';

/// 视口：只投影服务状态并转发输入，自身不含任何控件。
///
/// 控件都放到视口之上的浮层（shell 负责），这样视口在缩放/重建时不会
/// 连带重排按钮，也能保证 3D 内容始终占满可用区域。
class ViewportCanvas extends StatefulWidget {
  const ViewportCanvas({
    super.key,
    this.engine,
    this.service,
    this.autoLoadSample = true,
  }) : assert(engine == null || service == null);

  final Gs3dEngine? engine;
  final GeoScatter3dService? service;
  final bool autoLoadSample;

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
    final tokens = context.tokens;
    return Center(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          SizedBox(
            width: 120,
            child: LinearProgressIndicator(
              minHeight: 1,
              backgroundColor: tokens.colors.line,
              valueColor: AlwaysStoppedAnimation(tokens.colors.signal),
            ),
          ),
          const SizedBox(height: 10),
          Text(
            '正在准备渲染通道',
            style: tokens.type.meta.copyWith(color: tokens.colors.textLow),
          ),
        ],
      ),
    );
  }

  Widget _buildError(BuildContext context) {
    final tokens = context.tokens;
    return Container(
      key: const ValueKey('vulkan_engine_error'),
      color: tokens.colors.viewport,
      alignment: Alignment.center,
      padding: const EdgeInsets.all(32),
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 460),
        child: Container(
          padding: const EdgeInsets.all(20),
          decoration: BoxDecoration(
            color: tokens.colors.panel,
            border: Border.all(color: tokens.colors.alert.withValues(alpha: 0.4)),
            borderRadius: tokens.radii.mdAll,
          ),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  Icon(Icons.report_gmailerrorred_outlined,
                      size: 18, color: tokens.colors.alert),
                  const SizedBox(width: 8),
                  Text(
                    '渲染引擎未能启动',
                    style: tokens.type.bodyStrong.copyWith(
                      color: tokens.colors.textHigh,
                    ),
                  ),
                ],
              ),
              const SizedBox(height: 10),
              Text(
                _service.error!,
                style: tokens.type.meta.copyWith(color: tokens.colors.textMid),
              ),
              const SizedBox(height: 12),
              Text(
                '常见原因：缺少 Vulkan 1.1 驱动 / ICD，或显卡不支持本机要求。'
                'Linux 可用 vulkaninfo 检查，Windows 请更新显卡驱动。',
                style: tokens.type.meta.copyWith(color: tokens.colors.textLow),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildViewport(BuildContext context) {
    if (_service.error != null) return _buildError(context);
    final tokens = context.tokens;

    return LayoutBuilder(
      builder: (context, constraints) {
        // 只上报几何事实（逻辑尺寸 + 设备 dpr）；纹理像素密度策略在服务里决定。
        _service.requestSize(
          constraints.biggest,
          pixelRatio: MediaQuery.devicePixelRatioOf(context),
        );

        return Stack(
          children: [
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
                  onTapUp: (details) => _action('pick.point', {
                    'x': details.localPosition.dx,
                    'y': details.localPosition.dy,
                  }),
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
                    color: tokens.colors.viewport,
                    child: _buildFrameSurface(),
                  ),
                ),
              ),
            ),
            // 视口四周压一圈暗角，让点云从平面里「沉」下去，而不是浮在单色底上。
            const Positioned.fill(
              child: IgnorePointer(
                child: DecoratedBox(
                  decoration: BoxDecoration(
                    gradient: RadialGradient(
                      radius: 1.1,
                      colors: [Color(0x00000000), Color(0x66000000)],
                      stops: [0.55, 1.0],
                    ),
                  ),
                ),
              ),
            ),
          ],
        );
      },
    );
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
