import 'dart:ui' as ui;
import 'package:flutter/foundation.dart';
import 'gs3d_engine.dart';

/// 唯一视口状态中枢；拥有引擎、帧和控制动作，Widget 不直接执行 FFI。
class GeoScatter3dService extends ChangeNotifier {
  GeoScatter3dService({
    Gs3dEngine? engine,
    Future<Gs3dEngine> Function()? createEngine,
  }) : _engine = engine,
       _ownsEngine = engine == null,
       _createEngine =
           createEngine ??
           (() => Gs3dEngine.createForPlatform(width: 800, height: 600));

  final Future<Gs3dEngine> Function() _createEngine;
  final bool _ownsEngine;
  Gs3dEngine? _engine;
  bool _disposed = false;
  bool _started = false;
  bool _ready = false;
  bool _rendering = false;
  bool _paused = false;
  ui.Size? _pendingSize;
  ui.Image? _frame;
  String? _error;
  int _pointCount = 0;
  int _frameNumber = 0;
  int _windowFrames = 0;
  final Stopwatch _fpsClock = Stopwatch();
  double _fps = 0;
  double _frameTimeMs = 0;
  String _gpuName = 'Vulkan GPU';

  ui.Image? get frame => _frame;
  String? get error => _error;
  bool get ready => _ready && !_disposed && _error == null;
  int get pointCount => _pointCount;
  int get frameNumber => _frameNumber;
  double get fps => _fps;
  double get frameTimeMs => _frameTimeMs;
  String get gpuName => _gpuName;
  String get status => _error != null ? '引擎失败' : (_ready ? '就绪' : '正在初始化...');

  Future<void> initialize({bool loadSample = true}) async {
    if (_started || _disposed) return;
    _started = true;
    try {
      final engine = _engine ?? await _createEngine();
      if (_disposed) {
        if (_ownsEngine) engine.dispose();
        return;
      }
      _engine = engine;
      final size = _pendingSize;
      _pendingSize = null;
      if (size != null) {
        engine.resize(size.width.toInt(), size.height.toInt());
      }
      if (loadSample && engine.loadSample() != 0) {
        throw StateError('样例点云加载失败: ${engine.lastError}');
      }
      _pointCount = engine.pointCount;
      _gpuName = engine.diagnostics['gpu']?.toString() ?? 'Vulkan GPU';
      _ready = true;
      _fpsClock.start();
      notifyListeners();
    } catch (error) {
      _fail(error);
    }
  }

  void _fail(Object error) {
    if (_disposed) return;
    _error = error.toString();
    notifyListeners();
  }

  /// build 仅提交几何信息；原生 resize 合并到下一次渲染，避免同步 FFI。
  void requestSize(ui.Size size) {
    if (!_disposed &&
        size.width.isFinite &&
        size.height.isFinite &&
        size.width >= 1 &&
        size.height >= 1) {
      _pendingSize = size;
    }
  }

  void setPaused(bool paused) {
    _paused = paused;
    _windowFrames = 0;
    _fpsClock.reset();
  }

  Future<void> renderNextFrame() async {
    if (!ready || _rendering || _paused) return;
    _rendering = true;
    final stopwatch = Stopwatch()..start();
    try {
      final size = _pendingSize;
      _pendingSize = null;
      if (size != null) {
        _engine!.resize(size.width.toInt(), size.height.toInt());
      }
      final next = await _engine!.renderFrame();
      if (_disposed) {
        next?.dispose();
        return;
      }
      if (next == null) throw StateError('原生引擎没有返回帧');
      final previous = _frame;
      _frame = next;
      _frameTimeMs = stopwatch.elapsedMicroseconds / 1000;
      _frameNumber++;
      _windowFrames++;
      if (_fpsClock.elapsedMilliseconds >= 500) {
        _fps = _windowFrames * 1000000 / _fpsClock.elapsedMicroseconds;
        _windowFrames = 0;
        _fpsClock.reset();
      }
      notifyListeners();
      previous?.dispose();
    } catch (error) {
      _fail(error);
    } finally {
      _rendering = false;
    }
  }

  /// 当前最小控制面只开放相机输入与复位；未实现的动作必须明确拒绝。
  void executeAction(String action, [Map<String, double> values = const {}]) {
    if (!ready) throw StateError('引擎尚未就绪');
    double value(String key) {
      final result = values[key] ?? 0;
      if (!result.isFinite) throw ArgumentError.value(result, key, '必须为有限值');
      return result;
    }

    switch (action) {
      case 'camera.input':
        _engine!.cameraInput(
          rotDx: value('rotDx'),
          rotDy: value('rotDy'),
          panDx: value('panDx'),
          panDy: value('panDy'),
          scrollY: value('scrollY'),
        );
      case 'camera.reset':
        _engine!.resetCamera();
      default:
        throw UnsupportedError('尚未实现的动作: $action');
    }
  }

  void shutdown() {
    if (_disposed) return;
    _disposed = true;
    _fpsClock.stop();
    if (_ownsEngine) _engine?.dispose();
    _engine = null;
    _frame?.dispose();
    _frame = null;
  }

  @override
  void dispose() {
    shutdown();
    super.dispose();
  }
}
