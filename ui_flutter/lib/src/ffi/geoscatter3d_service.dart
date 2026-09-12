import 'dart:async';
import 'dart:ui' as ui;
import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'gs3d_engine.dart';
import 'present_surface.dart';

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

/// 零拷贝呈现通路的遥测刷新周期（单位：tick）。
/// present 模式下不再逐帧回读像素，HUD 的 fps/绘制点数/LOD 摘要改为周期性
/// 从引擎诊断读取（每 30 tick ≈ 0.5s@60Hz，一次 FFI JSON 往返）。
const int kPresentTelemetryTicks = 30;

/// 唯一视口状态中枢；拥有引擎、帧和控制动作，Widget 不直接执行 FFI。
class GeoScatter3dService extends ChangeNotifier {
  GeoScatter3dService({
    Gs3dEngine? engine,
    Future<Gs3dEngine> Function()? createEngine,
    PresentSurfaceBinder? presentBinder,
  }) : _engine = engine,
       _ownsEngine = engine == null,
       _presentBinder = presentBinder ?? defaultPresentSurfaceBinder(),
       _createEngine =
           createEngine ??
           (() => Gs3dEngine.createForPlatform(width: 800, height: 600));

  final Future<Gs3dEngine> Function() _createEngine;
  final PresentSurfaceBinder _presentBinder;
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
  int? _textureId;
  int _presentTicks = 0;
  int _presentWidth = 0;
  int _presentHeight = 0;
  bool _presentAttachTried = false;
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
  /// Flutter 纹理 id；null 表示没有零拷贝通路（视图层回退到 RawImage）。
  /// 注意 0 是合法纹理 id，不可用 0 当哨兵值。
  int? get textureId => _textureId;
  /// 是否已接管为零拷贝呈现（引擎直接 present 到 Flutter 纹理）。
  bool get zeroCopyPresent => _textureId != null;
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
      // 已知视口尺寸时立即尝试接管零拷贝通路；否则等首个 tick 拿到尺寸再试。
      if (size != null) {
        unawaited(_tryAttachPresent(engine, size));
      }
    } catch (error) {
      _fail(error);
    }
  }

  /// 尝试把引擎接管到 Flutter 纹理通路（零拷贝 present）。
  ///
  /// 不可用时只记录一次并保持回读路径——不能静默地假装纹理通路可用。
  Future<void> _tryAttachPresent(Gs3dEngine engine, ui.Size size) async {
    if (_presentAttachTried || _disposed) return;
    _presentAttachTried = true;
    final width = size.width.toInt();
    final height = size.height.toInt();
    if (width < 1 || height < 1) return;
    try {
      final id = await _presentBinder.attach(
        engineHandle: engine.nativeHandle,
        width: width,
        height: height,
      );
      if (_disposed || id == null) return;
      _textureId = id;
      _presentWidth = width;
      _presentHeight = height;
      _presentTicks = 0;
      debugPrint('[GS3D] 零拷贝呈现已接管: texture=$id ${width}x$height');
      notifyListeners();
    } catch (error) {
      debugPrint('[GS3D] 零拷贝纹理不可用，保持 RGBA 回读: $error');
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
    _applyLodSummary(engine.diagnostics);
  }

  /// 从一份已取回的诊断 JSON 刷新遥测（present 模式下每 30 tick 调一次）。
  void _applyDiagnostics(Map<String, dynamic> diagnostics) {
    if (diagnostics.isEmpty) return;
    _applyLodSummary(diagnostics);
    final lod = diagnostics['lod'];
    if (lod is Map) {
      _pointCount = (lod['drawn_points'] as num?)?.toInt() ?? _pointCount;
      _sourcePointCount =
          (lod['source_points'] as num?)?.toInt() ?? _sourcePointCount;
    }
    final fps = (diagnostics['fps'] as num?)?.toDouble() ?? 0;
    if (fps > 0) {
      _fps = fps;
      _frameTimeMs = 1000 / fps;
    }
    final frameMs = (diagnostics['frame_time_ms'] as num?)?.toDouble() ?? 0;
    if (frameMs > 0) _renderTimeMs = frameMs;
  }

  void _applyLodSummary(Map diagnostics) {
    final lod = diagnostics['lod'];
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

  /// 每帧 ticker：零拷贝通路下只推帧 + 周期刷遥测；否则走序号驱动的回读。
  ///
  /// 旧实现把整个 Vulkan 提交 + 回读（~20ms）按死在 UI isolate 上，
  /// 手势与动画都要等它；回读路径仍只做一次 memcpy + 解码，
  /// present 路径则完全不做像素回读。
  Future<void> renderNextFrame() async {
    if (!ready || _rendering || _paused) return;
    _rendering = true;
    try {
      final size = _pendingSize;
      _pendingSize = null;

      // ① 零拷贝通路：引擎直接 present 到 Flutter 纹理，不碰像素。
      //    此时 frameSequence 不再递增（present 模式不发布回读帧），
      //    若沿用序号驱动会导致本函数永不取帧、HUD 僵死。
      final textureId = _textureId;
      if (textureId != null) {
        if (size != null) {
          await _resizePresentSurface(size);
        }
        _engine!.requestFrame();
        _presentTicks++;
        if (_presentTicks % kPresentTelemetryTicks == 0) {
          _refreshPresentTelemetry();
        }
        return;
      }

      if (size != null) {
        _engine!.resize(size.width.toInt(), size.height.toInt());
        // 拿到真实视口尺寸后才尝试接管纹理通路（成功后下一 tick 走 ①）。
        if (!_presentAttachTried) {
          await _tryAttachPresent(_engine!, size);
          if (_textureId != null) {
            _engine!.requestFrame();
            return;
          }
        }
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

  /// present 模式下的遥测刷新：一次诊断读取同时得到 fps / 帧耗时 / 绘制点数
  /// 与 LOD 摘要；只在真正变化时才 notifyListeners（纹理层不需要重建）。
  void _refreshPresentTelemetry() {
    final engine = _engine;
    if (engine == null || _disposed) return;
    _applyDiagnostics(engine.diagnostics);
    _frameNumber++;
    notifyListeners();
  }

  /// 视口尺寸变化（旋转/分屏）：纹理 Surface 与交换链需要一起改。
  Future<void> _resizePresentSurface(ui.Size size) async {
    final width = size.width.toInt();
    final height = size.height.toInt();
    if (width < 1 || height < 1) return;
    if (width == _presentWidth && height == _presentHeight) return;
    _presentWidth = width;
    _presentHeight = height;
    try {
      // 引擎侧只需同步相机宽高比与离屏尺寸；交换链由平台层重建。
      _engine!.resize(width, height);
      await _presentBinder.resize(
        textureId: _textureId!,
        width: width,
        height: height,
      );
    } catch (error) {
      debugPrint('[GS3D] 纹理尺寸更新失败: $error');
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
    final engine = _engine;
    _engine = null;
    _frame?.dispose();
    _frame = null;
    final textureId = _textureId;
    _textureId = null;
    // 必须在释放引擎之前解除纹理通路：nativeClearSurface 需要有效句柄。
    unawaited(_releasePresent(engine, textureId));
  }

  Future<void> _releasePresent(Gs3dEngine? engine, int? textureId) async {
    if (textureId != null) {
      try {
        await _presentBinder.detach(textureId: textureId);
      } catch (error) {
        debugPrint('[GS3D] 释放零拷贝纹理失败: $error');
      }
    }
    if (_ownsEngine) engine?.dispose();
  }

  @override
  void dispose() {
    shutdown();
    super.dispose();
  }
}
