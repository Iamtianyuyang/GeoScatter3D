import 'dart:math' as math;
import 'dart:ui' as ui;
import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import '../ffi/geoscatter3d_service.dart';
import '../ffi/gs3d_engine.dart';
import 'telemetry_hud.dart';

/// 视口只投影服务状态并转发输入；不直接调度原生引擎。
class ViewportCanvas extends StatefulWidget {
  final Gs3dEngine? engine;
  final GeoScatter3dService? service;
  final bool autoLoadSample;

  const ViewportCanvas({
    super.key,
    this.engine,
    this.service,
    this.autoLoadSample = true,
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
    return const Center(
      child: CircularProgressIndicator(
        color: Color(0xFF38BDF8),
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
                    color: const Color(0xFF0B0F19), // 视口暗色基底
                    child: _buildFrameSurface(),
                  ),
                ),
              ),
            ),

            // 左上角悬浮实时遥测 HUD
            Positioned(
              top: MediaQuery.of(context).padding.top + 16,
              left: 16,
              child: TelemetryHud(
                fps: _service.fps,
                frameTimeMs: _service.frameTimeMs,
                renderTimeMs: _service.renderTimeMs,
                visiblePoints: _service.pointCount,
                lodSummary: _service.lodSummary,
                gpuName: _service.gpuName,
                status: _service.status,
                renderPath: _service.renderPath,
              ),
            ),
            SafeArea(
              child: Align(
                alignment: Alignment.bottomRight,
                child: Row(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    // 自动化/调试专用（`--dart-define=GS3D_DEBUG_CONTROLS=true`）：
                    // 视口缩放是双指手势，外部自动化注入不进去，因此给出语义化按钮。
                    if (kDebugControls) ..._buildDebugControls(context),
                    IconButton(
                      key: const ValueKey('camera_reset'),
                      tooltip: '复位相机',
                      onPressed: _service.ready
                          ? () => _action('camera.reset')
                          : null,
                      icon: const Icon(Icons.center_focus_strong),
                    ),
                  ],
                ),
              ),
            ),
          ],
        );
      },
    );
  }
  /// 调试控制面：放大/缩小 + 内核算取状态一行。
  ///
  /// 每个按钮都有稳定的 tooltip（→ Android 无障碍标签），Maestro 可以直接
  /// `tapOn: "放大"`；状态行同样作为文本节点暴露，可 `assertVisible: "cover=1.*"`。
  List<Widget> _buildDebugControls(BuildContext context) => [
    Container(
      margin: const EdgeInsets.only(right: 8),
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
      decoration: BoxDecoration(
        color: const Color(0xCC0B1220),
        borderRadius: BorderRadius.circular(6),
      ),
      child: Text(
        _service.debugStats.isEmpty ? 'debug: 等待遥测' : _service.debugStats,
        style: const TextStyle(
          color: Color(0xFF7DD3FC),
          fontSize: 11,
          fontFamily: 'monospace',
        ),
      ),
    ),
    IconButton(
      key: const ValueKey('camera_zoom_out'),
      tooltip: '缩小',
      onPressed: _service.ready
          ? () => _action('camera.input', {'scrollY': -kDebugZoomStep})
          : null,
      icon: const Icon(Icons.zoom_out),
    ),
    IconButton(
      key: const ValueKey('camera_zoom_in'),
      tooltip: '放大',
      onPressed: _service.ready
          ? () => _action('camera.input', {'scrollY': kDebugZoomStep})
          : null,
      icon: const Icon(Icons.zoom_in),
    ),
  ];
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
