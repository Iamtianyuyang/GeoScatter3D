import 'dart:io';
import 'dart:async';
import 'dart:math' as math;
import 'dart:ui' as ui;
import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'gs3d_engine.dart';
import 'app_state.dart';
import 'render_settings.dart';
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

/// 调试/自动化控制面：`--dart-define=GS3D_DEBUG_CONTROLS=true`。
///
/// 为什么需要它：视口的缩放/平移是 Flutter 的 `GestureDetector.onScaleUpdate`
/// （双指）与 `Listener.onPointerSignal`（鼠标滚轮）—— **外部自动化注入不进去**
/// （Maestro 只有 tap/swipe，`adb shell input` 没有 pinch，鼠标/触控板 roll 事件
/// 引擎也不消费）。于是真机排障时相机没法被脚本控制。打开这个开关后，视口右下角
/// 会出现带无障碍标签的相机按钮（放大/缩小/复位），并把内核的瓦片/LOD 选取状态
/// 以文本暴露出来，Maestro 就能通过 `tapOn: "放大"` + `assertVisible` 完整驱动与断言。
/// 默认关闭，生产 UI 不受影响。
const bool kDebugControls = bool.fromEnvironment('GS3D_DEBUG_CONTROLS');

/// 相机按钮每按一次推进的缩放量（与滚轮一步等价）。
const double kDebugZoomStep = 3.0;

/// 唯一视口状态中枢；拥有引擎、帧和控制动作，Widget 不直接执行 FFI。
class GeoScatter3dService extends ChangeNotifier {
  GeoScatter3dService({
    Gs3dEngine? engine,
    Future<Gs3dEngine> Function()? createEngine,
    PresentSurfaceBinder? presentBinder,
    Future<Directory> Function()? exportDirectory,
  }) : _exportDirectory = exportDirectory ?? exportsDirectory,
       _engine = engine,
       _ownsEngine = engine == null,
       _presentBinder = presentBinder ?? defaultPresentSurfaceBinder(),
       _createEngine =
           createEngine ??
           (() => Gs3dEngine.createForPlatform(width: 800, height: 600));

  final Future<Gs3dEngine> Function() _createEngine;
  final PresentSurfaceBinder _presentBinder;
  final bool _ownsEngine;
  final Future<Directory> Function() _exportDirectory;
  Gs3dEngine? _engine;
  bool _disposed = false;
  bool _started = false;
  bool _ready = false;
  bool _rendering = false;
  bool _paused = false;
  ui.Size? _pendingSize;

  /// 与 [_pendingSize] 一起提交的纹理像素倍率（见 [presentPixelRatioFor]）。
  double _pendingPixelRatio = 1.0;
  ui.Image? _frame;
  String? _error;
  String? _datasetPath;
  bool _openingDataset = false;
  String _datasetStage = '';
  int _pointCount = 0;
  int _sourcePointCount = 0;
  String _lodSummary = '';
  String _debugStats = '';
  int _frameNumber = 0;
  int _windowFrames = 0;
  int _lastSequence = 0;
  int? _textureId;
  int _presentTicks = 0;
  int _presentWidth = 0;
  int _presentHeight = 0;

  /// 已经尝试过零拷贝接管的纹理尺寸（**物理像素**）；尺寸变化（旋转/分屏/
  /// 显示缩放）时再给一次机会。
  (int, int)? _presentAttemptedExtent;

  /// 已下发给引擎的「渲染像素 / 逻辑像素」汇率（避免每帧重复跨 FFI）。
  double _viewScale = 1.0;
  final Stopwatch _fpsClock = Stopwatch();
  final Stopwatch _frameClock = Stopwatch();
  double _fps = 0;
  double _frameTimeMs = 0;
  double _renderTimeMs = 0;
  String _gpuName = 'Vulkan GPU';
  RenderSettings _renderSettings = RenderSettings.defaults;
  final List<PickedPoint> _pickedPoints = [];
  bool _exporting = false;
  String? _lastExportPath;
  String? _lastExportError;
  File? _renderSettingsFile;

  ui.Image? get frame => _frame;
  String? get error => _error;
  bool get ready => _ready && !_disposed && _error == null;
  int get pointCount => _pointCount;

  /// 数据集总量（源点）；无 LOD 时等于 [pointCount]。
  int get sourcePointCount => _sourcePointCount;

  /// LOD 摘要（源点数远大于绘制量时才非空），用于 HUD 展示缩放后的规模。
  String get lodSummary => _lodSummary;

  /// 调试用内核选取状态（tile/LOD 合成），`GS3D_DEBUG_CONTROLS` 打开时展示。
  /// 格式：`cover=0 below=1243 tile=357/357 clip=1 lod=2/3`
  String get debugStats => _debugStats;
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

  /// 当前取帧通路的展示名（HUD/诊断用）。
  String get renderPath => _textureId != null ? '零拷贝纹理' : 'RGBA 回读';
  String get status => _error != null ? '引擎失败' : (_ready ? '就绪' : '正在初始化...');

  /// 同步导出的点数上限：导出在渲染线程上写文件，超过这个量界面会长时间无帧。
  static const int maxSyncExportPoints = 2000000;

  bool get exporting => _exporting;
  String? get lastExportPath => _lastExportPath;
  String? get lastExportError => _lastExportError;

  /// 导出点云是否可用（引擎就绪、没有别的导出在跑、点数在同步上限内）。
  bool get canExport =>
      _ready && !_disposed && !_exporting && _sourcePointCount <= maxSyncExportPoints;

  /// 把当前数据集写到应用状态目录的 `exports/`；失败返回 null 并写
  /// [lastExportError]。
  Future<String?> exportPoints({String format = 'ply'}) async {
    return _runExport((engine) async {
      final file = await _exportFile(format, 'points');
      final rc = engine.exportDataset(file.path, format: format);
      if (rc != 0) throw StateError('导出失败 ($rc): ${engine.lastError}');
      return file.path;
    });
  }

  /// 当前帧另存 PNG；没渲染过帧时失败。
  Future<String?> saveScreenshot() async {
    return _runExport((engine) async {
      final png = engine.readFramePng();
      if (png == null) throw StateError('没有可保存的帧: ${engine.lastError}');
      final file = await _exportFile('png', 'frame');
      await file.writeAsBytes(png, flush: true);
      return file.path;
    });
  }

  Future<String?> _runExport(
    Future<String> Function(Gs3dEngine engine) work,
  ) async {
    final engine = _engine;
    if (engine == null || _disposed || _exporting) return null;
    if (!canExport) {
      _lastExportError = _ready
          ? '超过 $maxSyncExportPoints 点的导出尚未实现'
          : '引擎未就绪，无法导出';
      notifyListeners();
      return null;
    }
    _exporting = true;
    _lastExportError = null;
    notifyListeners();
    try {
      final path = await work(engine);
      _lastExportPath = path;
      return path;
    } catch (error) {
      _lastExportError = error.toString();
      return null;
    } finally {
      _exporting = false;
      if (!_disposed) notifyListeners();
    }
  }

  Future<File> _exportFile(String format, String prefix) async {
    final directory = await _exportDirectory();
    await directory.create(recursive: true);
    final stamp = DateTime.now().toUtc().millisecondsSinceEpoch;
    return File(
      '${directory.path}${Platform.pathSeparator}$prefix-$stamp.$format',
    );
  }

  /// 已拾取的点（最多两个：起点、终点）；第三次点击开始新的一组。
  List<PickedPoint> get pickedPoints => List.unmodifiable(_pickedPoints);

  /// 两点空间距离；不足两点返回 null。
  double? get measurementDistance {
    if (_pickedPoints.length < 2) return null;
    final a = _pickedPoints[0];
    final b = _pickedPoints[1];
    final dx = b.x - a.x;
    final dy = b.y - a.y;
    final dz = b.z - a.z;
    return math.sqrt(dx * dx + dy * dy + dz * dz);
  }

  /// 终点相对起点的高差（终点减起点，带符号）。
  double? get measurementHeightDelta {
    if (_pickedPoints.length < 2) return null;
    return _pickedPoints[1].z - _pickedPoints[0].z;
  }

  /// 在视口逻辑坐标处拾取；未命中时保持现有测量。
  void pickPointAt(double screenX, double screenY) {
    final engine = _engine;
    if (engine == null || _disposed || !_ready) return;
    final hit = engine.pickPoint(screenX, screenY);
    if (hit == null) return;
    if (_pickedPoints.length >= 2) _pickedPoints.clear();
    _pickedPoints.add(hit);
    notifyListeners();
  }

  void clearMeasurement() {
    if (_pickedPoints.isEmpty) return;
    _pickedPoints.clear();
    notifyListeners();
  }

  /// 当前显示设置（配色与点大小）。
  RenderSettings get renderSettings => _renderSettings;

  /// 读上次保存的显示设置；文件不存在就保留默认值。
  Future<void> loadRenderSettings(File file) async {
    _renderSettingsFile = file;
    final settings = await readRenderSettings(file);
    if (settings == null || _disposed) return;
    _renderSettings = settings;
    if (_started) _applyRenderSettings();
    notifyListeners();
  }

  /// 落盘失败会抛出，由界面决定是否提示；没有计数文件时静默跳过。
  Future<void> saveRenderSettings() async {
    final file = _renderSettingsFile;
    if (file == null) return;
    await writeRenderSettings(file, _renderSettings);
  }

  void setColormapIndex(int index) {
    _renderSettings = _renderSettings.copyWith(colormapIndex: index);
    if (_started) _applyRenderSettings();
    notifyListeners();
  }

  void setPointSize(double size) {
    _renderSettings = _renderSettings.copyWith(pointSize: size);
    if (_started) _applyRenderSettings();
    notifyListeners();
  }

  /// 只入队：渲染线程按顺序执行，记录值已先更新。
  void _applyRenderSettings() {
    final engine = _engine;
    if (engine == null || _disposed) return;
    engine.submitColormap(_renderSettings.colormapIndex);
    engine.submitPointSize(_renderSettings.pointSize);
  }

  /// 最近一次成功打开的工程包（`null` = 仍是内置样例/合成数据）。
  String? get datasetPath => _datasetPath;

  /// 正在后台读取或装载工程包。
  bool get openingDataset => _openingDataset;

  /// 装载阶段名（`''` = 尚未开始）。
  String get datasetStage => _datasetStage;

  /// 打开 v3 工程包：读盘在引擎工作线程，这里只轮询状态。
  ///
  /// 同一时间只允许一个开包任务；失败写入 [error] 并返回 false。
  Future<bool> openBundle(String path) async {
    final engine = _engine;
    if (_disposed || engine == null || path.isEmpty) return false;
    if (_openingDataset) return false;
    _openingDataset = true;
    _datasetStage = '排队';
    _error = null;
    notifyListeners();
    try {
      await engine.loadDatasetAsync(
        path,
        onProgress: (progress, stage) {
          if (_disposed) return;
          _datasetStage = stage;
          notifyListeners();
        },
      );
      if (_disposed) return false;
      _datasetPath = path;
      _sourcePointCount = engine.sourcePointCount;
      _pointCount = engine.drawnPointCount;
      _refreshLodSummary(engine);
      return true;
    } catch (error) {
      _fail(error);
      return false;
    } finally {
      _openingDataset = false;
      _datasetStage = '';
      if (!_disposed) notifyListeners();
    }
  }

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
      engine.setBackground(8 / 255, 19 / 255, 31 / 255);
      // Black backgrounds need a bright, cheap control-point colormap:
      // Turbo avoids both the dark low end of seismic and rainbow256's
      // 256-entry fragment branch.
      engine.setColormap(_renderSettings.colormapIndex);
      engine.setPointSize(_renderSettings.pointSize);
      final size = _pendingSize;
      final pixelRatio = _pendingPixelRatio;
      _pendingSize = null;
      final logical = size == null ? null : _logicalExtentOf(size);
      if (logical != null) {
        engine.resize(logical.$1, logical.$2);
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
      if (logical != null) {
        unawaited(
          _tryAttachPresent(
            engine,
            logical: logical,
            physical: _physicalExtentOf(size!, pixelRatio),
            pixelRatio: pixelRatio,
          ),
        );
      }
    } catch (error) {
      _fail(error);
    }
  }

  /// 尝试把引擎接管到 Flutter 纹理通路（零拷贝 present）。
  ///
  /// [logical] 是回读通路使用的逻辑像素尺寸，[physical] 是纹理/交换链的物理像素
  /// 尺寸（两者等比例，只差 dpr 倍率）：零拷贝通路必须按物理像素建纹理，否则
  /// 合成器会把逻辑尺寸的纹理放大到屏幕（Tab S7 上 1204×704 → 2560×1600，点阵发软，
  /// 且失去全屏硬件叠加直通：真机同位姿 13.9 ms → 8.3 ms）。
  ///
  /// 接管失败时必须把引擎视口与像素汇率都回退到逻辑像素：否则会留下
  /// 「引擎按物理像素渲染、UI 按逻辑像素回读」的错配（内存与填充率无谓×4.5）。
  ///
  /// 不可用时只记录一次并保持回读路径——不能静默地假装纹理通路可用。
  Future<void> _tryAttachPresent(
    Gs3dEngine engine, {
    required (int, int) logical,
    required (int, int) physical,
    required double pixelRatio,
  }) async {
    if (_disposed) return;
    final (width, height) = physical;
    if (width < 1 || height < 1) return;
    // 同一尺寸只试一次（避免每帧重复往返平台层）；尺寸变化时允许重试。
    if (_presentAttemptedExtent == physical) return;
    _presentAttemptedExtent = physical;
    // 纹理像素与回读像素一致（dpr=1）时无需切换，避免多余的原生调用。
    final switchViewport = physical != logical;
    if (switchViewport) _applyViewport(engine, physical, pixelRatio);
    try {
      final id = await _presentBinder.attach(
        engineHandle: engine.nativeHandle,
        width: width,
        height: height,
      );
      if (_disposed || id == null) {
        if (switchViewport && !_disposed) {
          _applyViewport(engine, logical, 1.0);
        }
        return;
      }
      _textureId = id;
      _presentWidth = width;
      _presentHeight = height;
      _presentTicks = 0;
      debugPrint('[GS3D] 零拷贝呈现已接管: texture=$id ${width}x$height');
      notifyListeners();
    } catch (error) {
      if (switchViewport && !_disposed) {
        _applyViewport(engine, logical, 1.0);
      }
      debugPrint('[GS3D] 零拷贝纹理不可用，保持 RGBA 回读: $error');
    }
  }

  /// 引擎视口 = 尺寸 + 「渲染像素 / 逻辑像素」汇率，两者必须一起改。
  ///
  /// 内核的像素门槛（瓦片最小投影尺寸 / 底图目标点距 / 自动点预算）按逻辑像素
  /// 标定（见 native `Config::view_scale`）：物理像素交换链不告知汇率的话，视口
  /// 高度 ×dpr 会让投影像素整体放大，选取的瓦片翻几倍 → 撞上预算上限 →
  /// 「全有或全无」把整层瓦片剔掉（真机 10M fov=1.4° 实测整屏空白）。
  ///
  /// 汇率变化时才跨 FFI（这个函数在每次拿到新尺寸时都会调用）。
  void _applyViewport(Gs3dEngine engine, (int, int) extent, double viewScale) {
    engine.resize(extent.$1, extent.$2);
    if (_viewScale == viewScale) return;
    _viewScale = viewScale;
    engine.setViewScale(viewScale);
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

  /// 调试行：把内核的选取状态压成一行可断言文本（Maestro 靠它判断
  /// 「瓦片该不该出现」与「底图为什么变密」）。
  String _formatDebugStats(Object? lod) {
    if (lod is! Map) return '';
    int intOf(String key) => (lod[key] as num?)?.toInt() ?? 0;
    int boolOf(String key) => (lod[key] == true) ? 1 : 0;
    return 'cover=${boolOf('tile_cover_view')}'
        ' below=${intOf('tile_below_threshold')}'
        ' clip=${boolOf('tile_clip_active')}'
        ' fully=${boolOf('tiles_fully_resident')}'
        ' skipcov=${boolOf('tile_skipped_by_coverage')}'
        ' tile=${intOf('tile_resident')}/${intOf('tile_needed')}'
        ' lod=${intOf('level_index') + 1}/${intOf('level_count')}'
        ' drawn=${intOf('drawn_points')}';
  }

  /// 由引擎诊断的 LOD 状态生成规模摘要；小数据集返回空串（HUD 不显示）。
  void _refreshLodSummary(Gs3dEngine engine) {
    _applyLodSummary(engine.diagnostics);
  }

  /// 从一份已取回的诊断 JSON 刷新遥测（present 模式下每 30 tick 调一次）。
  void _applyDiagnostics(Map<String, dynamic> diagnostics) {
    if (diagnostics.isEmpty) return;
    _applyLodSummary(diagnostics);
    _debugStats = _formatDebugStats(diagnostics['lod']);
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
  ///
  /// [pixelRatio] 是设备 dpr（`MediaQuery.devicePixelRatioOf`）：零拷贝通路的纹理
  /// 按 `逻辑尺寸 × dpr` 建，与物理屏幕 1:1；回读通路仍按逻辑像素渲染。
  void requestSize(ui.Size size, {double pixelRatio = 1.0}) {
    if (!_disposed &&
        size.width.isFinite &&
        size.height.isFinite &&
        size.width >= 1 &&
        size.height >= 1) {
      _pendingSize = size;
      _pendingPixelRatio = presentPixelRatioFor(pixelRatio);
    }
  }

  /// 逻辑视口尺寸 → 整数像素尺寸（至少 1×1）。
  static (int, int) _logicalExtentOf(ui.Size size) =>
      (math.max(1, size.width.toInt()), math.max(1, size.height.toInt()));

  /// 逻辑视口尺寸 → 纹理/交换链的物理像素尺寸。
  static (int, int) _physicalExtentOf(ui.Size size, double pixelRatio) {
    final ratio = pixelRatio.isFinite && pixelRatio > 0 ? pixelRatio : 1.0;
    return (
      math.max(1, (size.width * ratio).round()),
      math.max(1, (size.height * ratio).round()),
    );
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
      final pixelRatio = _pendingPixelRatio;
      _pendingSize = null;

      // ① 零拷贝通路：引擎直接 present 到 Flutter 纹理，不碰像素。
      //    此时 frameSequence 不再递增（present 模式不发布回读帧），
      //    若沿用序号驱动会导致本函数永不取帧、HUD 僵死。
      final textureId = _textureId;
      if (textureId != null) {
        if (size != null) {
          final (width, height) = _physicalExtentOf(size, pixelRatio);
          await _resizePresentSurface(width, height, pixelRatio);
        }
        _engine!.requestFrame();
        _presentTicks++;
        if (_presentTicks % kPresentTelemetryTicks == 0) {
          _refreshPresentTelemetry();
        }
        return;
      }

      if (size != null) {
        final logical = _logicalExtentOf(size);
        _applyViewport(_engine!, logical, 1.0);
        // 拿到真实视口尺寸后才尝试接管纹理通路（成功后下一 tick 走 ①）。
        if (_textureId == null) {
          await _tryAttachPresent(
            _engine!,
            logical: logical,
            physical: _physicalExtentOf(size, pixelRatio),
            pixelRatio: pixelRatio,
          );
          if (_textureId != null) {
            _engine!.requestFrame();
            return;
          }
          // 到这里说明未接管：[_tryAttachPresent] 已把引擎视口回退到逻辑像素
          // （换进物理像素再失败时会留下“引擎物理、UI 逻辑”的 4.5× 错配）。
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
  ///
  /// 入参是**物理像素**尺寸（见 [PresentSurfaceBinder.attach]）与对应的像素汇率：
  /// 引擎的相机 aspect 与瓦片/LOD 的像素门槛都以纹理像素为基准，两者必须同坐标系。
  Future<void> _resizePresentSurface(
    int width,
    int height,
    double pixelRatio,
  ) async {
    if (width < 1 || height < 1) return;
    final unchanged = width == _presentWidth && height == _presentHeight;
    // 汇率变了（显示缩放）即使尺寸没变也要下发一次。
    if (unchanged && _viewScale == pixelRatio) return;
    _presentWidth = width;
    _presentHeight = height;
    try {
      // 引擎侧只需同步相机宽高比、像素汇率与离屏尺寸；交换链由平台层重建。
      _applyViewport(_engine!, (width, height), pixelRatio);
      if (unchanged) return;
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
      case 'pick.point':
        pickPointAt(value('x'), value('y'));
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
