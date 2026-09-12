import 'dart:async';
import 'dart:ui' as ui;
import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'gs3d_engine.dart';

/// 基准/压力测试入口：以 --dart-define=GS3D_BENCH_POINTS=10000000 构建，
/// 启动后直接生成该点数的合成地形（用于真机验证千万级数据的 LOD 与帧预算）。
const int kBenchPointCount = int.fromEnvironment('GS3D_BENCH_POINTS');

/// 真机验证入口：`--dart-define=GS3D_BENCH_DATASET=<bundle 路径>`，
/// 直接加载磁盘上的 .gs3d.bundle（带 lod.gs3dlod / tiles.gs3dtiles* 时
/// 走“粗尺度 LOD + 细尺度 tile”路径）。
const String kBenchDatasetPath = String.fromEnvironment('GS3D_BENCH_DATASET');

/// 真机验收：以 --dart-define=GS3D_SURFACE_SMOKE=true 构建，启动后把引擎
/// 句柄交给平台层，用 ImageReader 附加真实 Surface 并 present 若干帧。
const bool kSurfaceSmoke = bool.fromEnvironment('GS3D_SURFACE_SMOKE');

/// 基准专用：启动后向前推进若干次相机缩放（正值放大），
/// 用于在没有双指手势的自动化通道里进入细尺度 tile 区间。
const int kBenchZoomSteps = int.fromEnvironment('GS3D_BENCH_ZOOM');

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
  int _sourcePointCount = 0;
  String _lodSummary = '';
  int _frameNumber = 0;
  int _windowFrames = 0;
  int _lastSequence = 0;
  final Stopwatch _fpsClock = Stopwatch();
  final Stopwatch _frameClock = Stopwatch();
  double _fps = 0;
  double _frameTimeMs = 0;
  double _renderTimeMs = 0;
  String _gpuName = 'Vulkan GPU';

  ui.Image? get frame => _frame;
  String? get error => _error;
  bool get ready => _ready && !_disposed && _error == null;
  int get pointCount => _pointCount;
  /// 数据集总量（源点）；无 LOD 时等于 [pointCount]。
  int get sourcePointCount => _sourcePointCount;
  /// LOD 摘要（源点数远大于绘制量时才非空），用于 HUD 展示缩放后的规模。
  String get lodSummary => _lodSummary;
  int get frameNumber => _frameNumber;
  double get fps => _fps;
  double get frameTimeMs => _frameTimeMs;
  /// 原生渲染线程的帧耗时（毫秒）；与 UI 端解码/绘制解耦。
  double get renderTimeMs => _renderTimeMs;
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
      if (kBenchDatasetPath.isNotEmpty) {
        // 真机/桌面基准：加载真实 bundle（可能带 LOD + tile sidecar）。
        final rc = engine.loadDataset(kBenchDatasetPath);
        if (rc != 0) {
          throw StateError('数据集加载失败 ($rc): ${engine.lastError}');
        }
      } else if (kBenchPointCount > 0) {
        // 压力测试：合成地形代替内置样例，避免依赖真机上的数据集文件。
        final rc = engine.loadSynthetic(kBenchPointCount);
        if (rc != 0) {
          throw StateError('合成基准数据加载失败: ${engine.lastError}');
        }
      } else if (loadSample && engine.loadSample() != 0) {
        throw StateError('样例点云加载失败: ${engine.lastError}');
      }
      _sourcePointCount = engine.sourcePointCount;
      _pointCount = engine.drawnPointCount;
      _refreshLodSummary(engine);
      for (var step = 0; step < kBenchZoomSteps; step++) {
        engine.cameraInput(scrollY: 3.0);
      }
      _gpuName = _resolveGpuName(engine);
      _ready = true;
      _fpsClock.start();
      _frameClock.start();
      if (kSurfaceSmoke) {
        unawaited(_runSurfaceSmokeTest(engine));
      }
      // 首帧由渲染线程异步产生；UI 线程不再阻塞在 Vulkan 提交上。
      engine.requestFrame();
      notifyListeners();
    } catch (error) {
      _fail(error);
    }
  }

  String _resolveGpuName(Gs3dEngine engine) {
    final gpu = engine.diagnostics['gpu'];
    if (gpu is Map) {
      final name = gpu['name'];
      if (name is String && name.isNotEmpty) return name;
    }
    return 'Vulkan GPU';
  }

  /// 平台层烟囱测试：ImageReader Surface → 引擎 present → 回读像素。
  Future<void> _runSurfaceSmokeTest(Gs3dEngine engine) async {
    try {
      final channel = const MethodChannel(
        'com.geoscatter3d.ui_flutter/documents',
      );
      final result = await channel.invokeMethod<Map<Object?, Object?>>(
        'surfaceSmokeTest',
        {'engineHandle': engine.nativeHandle, 'frames': 60},
      );
      debugPrint('[SURFACE_SMOKE] result=$result');
    } catch (error) {
      debugPrint('[SURFACE_SMOKE] failed: $error');
    }
  }

  /// 由引擎诊断的 LOD 状态生成规模摘要；小数据集返回空串（HUD 不显示）。
  void _refreshLodSummary(Gs3dEngine engine) {
    final lod = engine.diagnostics['lod'];
    if (lod is! Map) {
      _lodSummary = '';
      return;
    }
    final source = (lod['source_points'] as num?)?.toInt() ?? 0;
    final drawn = (lod['drawn_points'] as num?)?.toInt() ?? 0;
    final tilePoints = (lod['tile_drawn_points'] as num?)?.toInt() ?? 0;
    final tilesResident = (lod['tile_resident'] as num?)?.toInt() ?? 0;
    final tilesNeeded = (lod['tile_needed'] as num?)?.toInt() ?? 0;
    final level = (lod['level_index'] as num?)?.toInt() ?? 0;
    final levels = (lod['level_count'] as num?)?.toInt() ?? 1;

    // 细尺度 tile 生效时：显示“全精度瓦片点数 + 驻留/需求”。
    if (tilesNeeded > 0) {
      final loadCost = _formatLoadCost(lod);
      _lodSummary =
          'tile ${formatPointCount(tilePoints)} 点'
          ' · 驻留 $tilesResident/$tilesNeeded'
          ' · LOD ${level + 1}/$levels'
          '$loadCost';
      return;
    }
    if (levels <= 1 || source <= drawn) {
      _lodSummary = '';
      return;
    }
    final loadSuffix = _formatLoadCost(lod);
    _lodSummary =
        '${formatPointCount(drawn)} / ${formatPointCount(source)} 点'
        ' · LOD ${level + 1}/$levels$loadSuffix';
  }

  /// 建链 + 上传耗时（大数据的加载成本）；小数据不展示。
  static String _formatLoadCost(Map lod) {
    final buildMs = (lod['build_ms'] as num?)?.toDouble() ?? 0;
    final uploadMs = (lod['upload_ms'] as num?)?.toDouble() ?? 0;
    final totalMs = buildMs + uploadMs;
    if (totalMs < 300) return '';
    return ' · 加载 ${(totalMs / 1000).toStringAsFixed(1)}s';
  }

  /// 中文数量级压缩（HUD 宽度有限）：1000 万 / 1.2 亿。
  @visibleForTesting
  static String formatPointCount(int value) {
    if (value >= 100000000) {
      return '${(value / 100000000).toStringAsFixed(1)} 亿';
    }
    if (value >= 10000) {
      final scaled = value / 10000;
      return '${scaled.toStringAsFixed(scaled >= 100 ? 0 : 1)} 万';
    }
    return '$value';
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
    _frameClock.reset();
  }

  /// 每帧 tker：先请求渲染线程出帧，再非阻塞地取回已完成帧。
  ///
  /// 旧实现把整个 Vulkan 提交 + 回读（~20ms）按死在 UI isolate 上，
  /// 手势与动画都要等它；现在 UI 线程只做一次 memcpy + 解码。
  Future<void> renderNextFrame() async {
    if (!ready || _rendering || _paused) return;
    _rendering = true;
    try {
      final size = _pendingSize;
      _pendingSize = null;
      if (size != null) {
        _engine!.resize(size.width.toInt(), size.height.toInt());
      }

      final sequence = _engine!.frameSequence;
      if (sequence == _lastSequence) {
        // 尚无新帧：请求下一帧并让本 tick 提前返回（请求会被内核合并）。
        _engine!.requestFrame();
        return;
      }

      final next = await _engine!.readFrame();
      if (_disposed) {
        next?.dispose();
        return;
      }
      if (next == null) return;

      _lastSequence = sequence;
      final previous = _frame;
      _frame = next;
      // 帧间隔反映渲染线程的真实出帧节拍（含 GPU 提交与回读）。
      _frameTimeMs = _frameClock.elapsedMicroseconds / 1000;
      _frameClock.reset();
      _renderTimeMs = _engine!.lastRenderMs;
      _frameNumber++;
      _windowFrames++;
      // LOD 层随视口/预算变化：每 30 帧刷新一次规模读数（避免每帧 JSON 开销）。
      if (_frameNumber % 30 == 0) {
        _pointCount = _engine!.drawnPointCount;
        _sourcePointCount = _engine!.sourcePointCount;
        _refreshLodSummary(_engine!);
      }
      if (_fpsClock.elapsedMilliseconds >= 500) {
        _fps = _windowFrames * 1000000 / _fpsClock.elapsedMicroseconds;
        _windowFrames = 0;
        _fpsClock.reset();
      }
      notifyListeners();
      previous?.dispose();
      // 立即预约下一帧，避免等下一个 tick 才启动渲染。
      _engine!.requestFrame();
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
