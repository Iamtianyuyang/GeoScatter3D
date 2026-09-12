import 'dart:async';
import 'dart:convert';
import 'dart:ffi' as ffi;
import 'dart:ffi';
import 'dart:io';
import 'dart:isolate';
import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;
import 'package:ffi/ffi.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart' show rootBundle;
import '../models/dataset_model.dart';
import '../models/gpu_info_model.dart';
import '../models/point_cloud_model.dart';
import '../models/recent_project_model.dart';
import '../models/measurement_line_model.dart';
import '../platform/mobile_document_picker.dart';
import 'geoscatter3d_bindings.dart';

part 'welcome_backend.dart';

/// 预处理离线构建实时进度状态
class PreprocessProgressInfo {
  final String stage;
  final double progress; // 0.0 ~ 1.0
  final String detail;
  final double elapsedSeconds;
  final bool isRunning;
  final bool isFinished;
  final bool isFailed;
  final String? errorMessage;

  const PreprocessProgressInfo({
    required this.stage,
    required this.progress,
    required this.detail,
    required this.elapsedSeconds,
    this.isRunning = true,
    this.isFinished = false,
    this.isFailed = false,
    this.errorMessage,
  });

  Map<String, dynamic> toJson() => {
    'stage': stage,
    'progress': progress,
    'detail': detail,
    'elapsed_seconds': elapsedSeconds,
    'is_running': isRunning,
    'is_finished': isFinished,
    'is_failed': isFailed,
    'error_message': errorMessage,
  };
}

/// 动作元数据描述符，对齐 MCP 工具定义规范 (JSON-Schema)
class UiActionDescriptor {
  final String id;
  final String name;
  final String description;
  final String category;
  final Map<String, dynamic> parameterSchema;

  const UiActionDescriptor({
    required this.id,
    required this.name,
    required this.description,
    required this.category,
    this.parameterSchema = const {},
  });

  Map<String, dynamic> toJson() => {
    'id': id,
    'name': name,
    'description': description,
    'category': category,
    'parameters': parameterSchema,
  };
}

enum WorkbenchLayoutMode {
  standard,
  floatingDock,
  analysisRail;

  String get id {
    switch (this) {
      case WorkbenchLayoutMode.standard:
        return 'workbench';
      case WorkbenchLayoutMode.floatingDock:
        return 'floating-dock';
      case WorkbenchLayoutMode.analysisRail:
        return 'analysis-rail';
    }
  }

  String get displayName {
    switch (this) {
      case WorkbenchLayoutMode.standard:
        return '标准工作台';
      case WorkbenchLayoutMode.floatingDock:
        return '悬浮胶囊 Dock';
      case WorkbenchLayoutMode.analysisRail:
        return '暗色分析舱';
    }
  }
}

/// 渲染内核可用性状态（R3：无引擎必须显式外显，不得静默假装可用）。
enum EngineAvailability {
  /// 内核已随平台部署且加载成功。
  available,

  /// 内核在本平台不可用——依赖引擎的动作保持可达，但执行时返回显式错误反馈。
  unavailable,
}

class GeoScatter3dService extends ChangeNotifier {
  static final GeoScatter3dService _instance = GeoScatter3dService._internal();
  factory GeoScatter3dService() => _instance;
  GeoScatter3dService._internal();
  GeoScatter3dService.detached();

  static Future<void>? _storagePreparation;
  static String? _mobileProjectsDirectory;

  /// 准备移动端持久化工程目录。
  ///
  /// Android/iOS 的临时目录可能被系统清理，不能承载用户工程；应用支持
  /// 目录位于沙盒持久区，不需要申请外部存储权限。桌面端继续使用原有
  /// 环境变量 / 仓库目录解析逻辑。
  static Future<void> prepareStorage() {
    return _storagePreparation ??= _prepareStorage();
  }

  /// 在首帧之后准备移动端存储并初始化原生引擎。
  ///
  /// 启动阶段不能同步阻塞 [runApp]，否则 Android 冷启动期间只会看到系统栏。
  Future<void> bootstrap() async {
    await prepareStorage();
    // 给 Flutter 首帧一个调度机会，随后再执行可能较慢的动态库初始化。
    await Future<void>.delayed(Duration.zero);
    initialize();
  }

  static Future<void> _prepareStorage() async {
    if (!Platform.isAndroid && !Platform.isIOS) return;
    try {
      var persistentRoot = _mobilePersistentRoot();
      if (Platform.isAndroid) {
        // Android 原生返回准确的 filesDir；旧系统/嵌入式运行时失败时，
        // _mobilePersistentRoot() 仍能从 cache/code_cache 同级推断 filesDir。
        final nativeRoot = await MobileDocumentPicker.persistentDirectory();
        if (nativeRoot != null && nativeRoot.trim().isNotEmpty) {
          persistentRoot = nativeRoot.trim();
        }
      }
      final projects = Directory('$persistentRoot/GeoScatter3D/projects');
      await projects.create(recursive: true);
      _mobileProjectsDirectory = projects.absolute.path;
    } catch (error) {
      // 目录准备失败时仍保留可诊断的临时回退，初始化阶段会显式报告
      // 引擎错误；下次启动会再次尝试持久目录准备。
      _storagePreparation = null;
      debugPrint(
        '[GeoScatter3D storage] persistent directory unavailable: $error',
      );
    }
  }

  /// 在不引入原生插件的前提下定位应用持久沙盒。
  ///
  /// Android 的 `Directory.systemTemp` 对应应用私有 `cache`，其同级
  /// `files` 是持久区；iOS 的临时目录位于容器根下 `tmp`，应用支持目录
  /// 位于同一容器的 `Library/Application Support`。这两个路径都不需要
  /// 外部存储权限，且不会把数据写入 APK 或系统共享目录。
  static String _mobilePersistentRoot() {
    final temp = Directory.systemTemp.absolute.path.replaceAll('\\', '/');
    if (Platform.isAndroid) {
      for (final cacheSuffix in const ['/cache', '/code_cache']) {
        if (temp.endsWith(cacheSuffix)) {
          return '${temp.substring(0, temp.length - cacheSuffix.length)}/files';
        }
      }
    }
    if (Platform.isIOS && temp.endsWith('/tmp')) {
      return '${temp.substring(0, temp.length - '/tmp'.length)}/Library/Application Support';
    }
    // 极少数嵌入式运行时可能不遵循标准容器命名；保留应用临时目录
    // 作为最后回退，并让上层日志明确记录目录准备失败。
    return '$temp/GeoScatter3D';
  }

  GeoScatter3dBindings? _bindings;
  bool _initialized = false;
  // 引擎可用性显式状态机（R3）。默认 available：Windows 有 DLL 时行为零变化；
  // initialize() 失败（含 Android .so 加载失败）时写入 unavailable 并外显。
  EngineAvailability _engineAvailability = EngineAvailability.available;
  String? _initializationError;
  // 单进程离屏引擎：唯一渲染路径；无引擎则工作台不可用（无 Dart 软预览）
  Pointer<Void>? _engineHandle;
  int _engineW = 0;
  int _engineH = 0;
  ui.Image? _nativeFrameImage;
  Timer? _nativeFrameTimer;
  bool _nativeFrameBusy = false;
  Map<String, dynamic> _nativeSnapshot = const {};
  Timer? _camFlushTimer;
  bool _nativeInteracting = false;
  bool _engineRenderQueued = false;
  double _lastEngineFrameMs = 0;
  Map<String, dynamic> get nativeSnapshot => _nativeSnapshot;
  bool get isRegionComputing => _nativeSnapshot['region_computing'] == true;
  List<String> get nativeTileDetails => List<String>.from(
    (_nativeSnapshot['dataset'] as Map?)?['tile_details'] ?? const [],
  );
  List<String> get nativeLodDetails => List<String>.from(
    (_nativeSnapshot['dataset'] as Map?)?['lod_details'] ?? _summary.lodDetails,
  );
  Uint8List? _nativeViewportPng;
  String? _nativeViewerError;
  String? _nativeShaderDirectory;
  String? _loadedDatasetPath;
  DatasetSummary _summary = const DatasetSummary();
  List<RecentProjectItem> _recentProjects = [];
  String _recentProjectFilter = 'all';
  List<GpuDeviceInfo> _gpus = [];
  int _activeGpuIndex = 0;
  bool _isWorkbenchActive = false;
  WorkbenchLayoutMode _layoutMode = WorkbenchLayoutMode.standard;

  List<Point3D> _points = [];
  double _pointSize = 1.5;
  String _colormap = 'viridis';
  double _scalarMin = 0.0;
  double _scalarMax = 100.0;

  // 视口相机状态
  double _cameraAzimuth = -45.0;
  double _cameraElevation = 30.0;
  double _cameraZoom = 1.0;
  double _cameraPanX = 0.0;
  double _cameraPanY = 0.0;
  bool _isPanning = false;

  // 视口叠加层状态
  bool _showMapAxis = true;
  bool _showWorldAxis = false;
  bool _showCrosshair = true;

  // 渲染外观与材质状态
  String _pointShape = '方形';
  double _heightScale = 1.0;
  String _heightSource = 'elevation';
  String _colorAttribute = 'field_statics';
  Color _viewportBackgroundColor = const Color(0xFF161A22);

  // 面板停靠显隐状态
  bool _leftDockVisible = true;
  bool _rightDockVisible = true;

  // 主题 / 相机联动 / 原生面板（对齐 C++ ComponentRegistry）
  int _themeIndex = 0;
  bool _cameraLinked = false;
  final Map<String, bool> _nativePanelVisible = {
    'panel.dataset': true,
    'panel.render_settings': true,
    'panel.performance': false,
    'panel.navigation_map': true,
    'panel.measurement': true,
    'panel.region_stats': true,
    'panel.tile_inspector': false,
    'panel.lod_view': false,
  };

  static const List<String> kColormapOptions = [
    'geo',
    'viridis',
    'jet',
    'gray',
    'thermal',
    'coolwarm',
    'turbo',
    'plasma',
    'rainbow256',
  ];
  static const List<String> kPointShapeOptions = ['方形', '圆形', '菱形', '三角形'];
  static const List<String> kThemeNames = [
    'Carbon Blue Dark',
    'Carbon Blue Light',
    'High Contrast',
    'Midnight',
    'Paper',
  ];

  // 截图通知回调
  VoidCallback? onScreenshotRequested;

  /// 引擎帧截图落盘完成回调（携带绝对路径），用于视口弹出提示。
  void Function(String path)? onScreenshotSaved;

  bool get isInitialized => _initialized;

  /// 引擎可用性状态（视口据此渲染显式降级错误卡片）。
  EngineAvailability get engineAvailability => _engineAvailability;

  /// 初始化失败原因（engineAvailability == unavailable 时非空）。
  String? get initializationError => _initializationError;

  /// 显式标记引擎不可用（供 initialize 失败路径与测试预设使用）。
  void markEngineUnavailable(String reason) {
    _engineAvailability = EngineAvailability.unavailable;
    _initializationError = reason;
    notifyListeners();
  }

  /// 依赖渲染内核的动作前缀集合。
  ///
  /// 命中这些前缀的动作在 `engineAvailability == unavailable` 时保持入口
  /// 可达（不置灰、不隐藏），但 `executeAction` 前置 guard 返回显式错误
  /// （R3：引擎依赖类「点击 → 显式报错」，与纯 UI 类动作区分）。
  static const Set<String> engineDependentActionPrefixes = <String>{
    'workbench.camera',
    'workbench.gizmo',
    'workbench.render',
    'workbench.point_cloud',
    'workbench.pick',
    'workbench.minimap',
    'workbench.screenshot',
    'workbench.export',
    'workbench.cache',
    'workbench.diagnostics',
    'workbench.stats',
    'workbench.measure',
    'workbench.file',
  };

  bool _isEngineDependentAction(String actionId) =>
      engineDependentActionPrefixes.any(actionId.startsWith);

  /// R3 统一降级反馈文案。
  static String get engineUnavailableMessage =>
      '渲染内核在本平台不可用，无法执行该动作。请检查设备 Vulkan 支持与应用原生库部署。';
  bool _welcomeBusy = false;
  String? _welcomeError;
  bool get welcomeBusy => _welcomeBusy;
  String? get welcomeError => _welcomeError;
  void _notifyWelcome() => notifyListeners();
  Future<Map<String, dynamic>>? _welcomeTask;
  Future<Map<String, dynamic>> executeActionAsync(
    String action, [
    Map<String, dynamic> params = const {},
  ]) {
    // Android 的导出需要等待 SAF 保存面板返回 Uri；命令面板走异步
    // 控制面时也必须使用同一条移动端保存链路，不能退回桌面路径写盘。
    if (Platform.isAndroid && action == 'workbench.export') {
      return _executeAndroidExportAction(params);
    }
    if (_welcomeBusy) return _executeWelcomeAction(action, params);
    return _welcomeTask = _executeWelcomeAction(action, params);
  }

  Future<Map<String, dynamic>> _executeAndroidExportAction(
    Map<String, dynamic> params,
  ) async {
    final format = (params['format'] as String?)?.toLowerCase() == 'csv'
        ? 'csv'
        : 'ply';
    final requestedPath =
        (params['path'] as String?) ?? 'export_points.$format';
    final success = await exportPointCloudThroughNativeViewer(
      requestedPath,
      format: format,
    );
    return {
      'success': success,
      'message': success ? '点云已保存到系统选择的位置' : '点云导出失败或已取消',
      'data': {'format': format, 'suggested_name': requestedPath},
    };
  }

  static String? get runtimeDirectory =>
      Platform.environment['GS3D_RUNTIME_DIR'];
  static String get projectsDirectory {
    final configured = Platform.environment['GS3D_PROJECTS_DIR'];
    if (configured != null && configured.isNotEmpty) {
      return Directory(configured).absolute.path;
    }
    // 移动端没有仓库目录或 LOCALAPPDATA；正常启动时
    // [_mobileProjectsDirectory] 已由 [prepareStorage] 指向持久化沙盒。
    // 原生桥接初始化失败时仍使用 cache/code_cache 同级的 files 推断，
    // 避免静默回退到可被系统清理的缓存目录。
    if (Platform.isAndroid || Platform.isIOS) {
      return _mobileProjectsDirectory ??
          '${_mobilePersistentRoot()}/GeoScatter3D/projects';
    }
    final root = resolveRepoRoot();
    if (File('$root/CMakeLists.txt').existsSync()) return '$root/data';
    final userRoot =
        Platform.environment['LOCALAPPDATA'] ??
        Platform.environment['HOME'] ??
        root;
    return '$userRoot/GeoScatter3D/projects';
  }

  String get backendStatus => _welcomeBusy
      ? (_latestProgress?.stage ?? '正在载入工程与启动渲染器…')
      : _engineAvailability == EngineAvailability.unavailable
      ? '渲染内核在本平台不可用${_initializationError == null ? '' : '（$_initializationError）'}'
      : isNativeRendererActive
      ? '原生渲染器已连接'
      : _initialized
      ? '数据引擎已连接，等待打开工程'
      : '原生数据引擎未连接';
  DatasetSummary get summary => _summary;
  List<RecentProjectItem> get recentProjects => _recentProjects;
  String get recentProjectFilter => _recentProjectFilter;
  List<RecentProjectItem> get filteredRecentProjects {
    final filter = _recentProjectFilter;
    if (filter == 'all') return List<RecentProjectItem>.of(recentProjects);
    return recentProjects
        .where((item) {
          final path = item.path.toLowerCase();
          switch (filter) {
            case 'bundle':
              return path.endsWith('.gs3d.bundle');
            case 'gs3d':
              return path.endsWith('.gs3d') && !path.endsWith('.gs3d.bundle');
            default:
              return true;
          }
        })
        .toList(growable: false);
  }

  void setRecentProjectFilter(String filter) {
    final normalized = switch (filter) {
      'bundle' => 'bundle',
      'gs3d' => 'gs3d',
      _ => 'all',
    };
    if (_recentProjectFilter == normalized) return;
    _recentProjectFilter = normalized;
    notifyListeners();
  }

  List<GpuDeviceInfo> get gpus => _gpus;
  int get activeGpuIndex => _activeGpuIndex;

  /// 当前活跃 GPU 的真实名称；没有引擎探测结果时不伪造桌面硬件型号。
  String get activeGpuDisplayName {
    final index = activeGpuIndex;
    if (index >= 0 && index < gpus.length) {
      final name = gpus[index].name.trim();
      if (name.isNotEmpty) return name;
    }
    return '未探测到 Vulkan 设备';
  }

  bool get isWorkbenchActive => _isWorkbenchActive;
  WorkbenchLayoutMode get layoutMode => _layoutMode;

  String get engineVersion {
    if (!_initialized || _bindings == null) {
      return '0.1.0 (Build 2026.09)';
    }
    try {
      final ptr = _bindings!.get_version();
      final str = ptr.toDartString();
      return str.isNotEmpty ? str : '0.1.0 (Build 2026.09)';
    } catch (_) {
      return '0.1.0 (Build 2026.09)';
    }
  }

  // 拖拽与离线构建状态
  Timer? _dragDropTimer;
  void Function(String path)? onFileDropped;
  Process? _activePreprocessProcess;
  bool _preprocessCancelled = false;
  PreprocessProgressInfo? _latestProgress;

  PreprocessProgressInfo? get latestProgress => _latestProgress;
  bool get isPreprocessing => _activePreprocessProcess != null;

  List<Point3D> get points => _points;
  double get pointSize => _pointSize;
  String get colormap => _colormap;
  double get scalarMin => _scalarMin;
  double get scalarMax => _scalarMax;

  double get cameraAzimuth => _cameraAzimuth;
  double get cameraElevation => _cameraElevation;
  double get cameraZoom => _cameraZoom;
  double get cameraPanX => _cameraPanX;
  double get cameraPanY => _cameraPanY;
  bool get isPanning => _isPanning;

  bool get showMapAxis => _showMapAxis;
  bool get showWorldAxis => _showWorldAxis;
  bool get showCrosshair => _showCrosshair;

  String get pointShape => _pointShape;
  double get heightScale => _heightScale;
  String get heightSource => _heightSource;
  String get colorAttribute => _colorAttribute;
  Color get viewportBackgroundColor => _viewportBackgroundColor;

  int get themeIndex => _themeIndex;
  String get themeName =>
      kThemeNames[_themeIndex.clamp(0, kThemeNames.length - 1)];
  bool get cameraLinked => _cameraLinked;
  Map<String, bool> get nativePanelVisible =>
      Map.unmodifiable(_nativePanelVisible);
  List<String> get measurementLineSummaries => _measurementLines
      .map(
        (l) =>
            '${l.id}: (${l.start.x.toStringAsFixed(2)}, ${l.start.y.toStringAsFixed(2)}, ${l.start.z.toStringAsFixed(2)}) → '
            '(${l.end.x.toStringAsFixed(2)}, ${l.end.y.toStringAsFixed(2)}, ${l.end.z.toStringAsFixed(2)})',
      )
      .toList();

  // 动态性能与流式加载指标
  double _fps = 0.0;
  double _frameTimeMs = 0.0;
  int _visiblePoints = 0;
  int _gpuMemoryBytes = 0;
  int _loadedTiles = 0;
  int _pendingTiles = 0;
  double _cacheHitRate = -1.0;
  String _cameraCoordsString = '0.0, 0.0, 143.4';
  Timer? _metricsTimer;

  // 点拾取、对焦与测量交互状态
  Point3D? _hoveredPoint;
  Point3D? _selectedPoint;
  bool _isMeasurementMode = false;
  Point3D? _pendingMeasureStart;
  final List<MeasurementLineItem> _measurementLines = [];
  String _measurementDisplayMode = '3d';

  // 局部选区统计分析状态
  Map<String, dynamic>? _activeRegionStats;

  // 2D 鸟瞰小地图
  Uint8List? _minimapRgba;
  ui.Image? _minimapImage;
  int _minimapWidth = 200;
  int _minimapHeight = 160;

  // 全局命令面板与快捷键速查图层
  bool _isCommandPaletteOpen = false;
  bool _isShortcutOverlayOpen = false;
  bool _isPerformancePanelOpen = false;
  bool _isTileInspectorOpen = false;
  bool _isRenderSettingsOpen = false;

  double get fps => _fps;
  double get frameTimeMs => _frameTimeMs;
  int get visiblePoints => _visiblePoints;
  int get gpuMemoryBytes => _gpuMemoryBytes;
  int get loadedTiles => _loadedTiles;
  int get pendingTiles => _pendingTiles;
  double get cacheHitRate => _cacheHitRate;
  String get cameraCoordsString => _cameraCoordsString;

  Point3D? get hoveredPoint => _hoveredPoint;
  Point3D? get selectedPoint => _selectedPoint;
  bool get isMeasurementMode => _isMeasurementMode;
  Point3D? get pendingMeasureStart => _pendingMeasureStart;
  List<MeasurementLineItem> get measurementLines =>
      List.unmodifiable(_measurementLines);
  String get measurementDisplayMode => _measurementDisplayMode;

  Map<String, dynamic>? get activeRegionStats => _activeRegionStats;
  Uint8List? get minimapRgba => _minimapRgba;
  ui.Image? get minimapImage => _minimapImage;
  int get minimapWidth => _minimapWidth;
  int get minimapHeight => _minimapHeight;

  bool get isCommandPaletteOpen => _isCommandPaletteOpen;
  bool get isShortcutOverlayOpen => _isShortcutOverlayOpen;
  bool get isPerformancePanelOpen => _isPerformancePanelOpen;
  bool get isTileInspectorOpen => _isTileInspectorOpen;
  bool get isRenderSettingsOpen => _isRenderSettingsOpen;

  bool get leftDockVisible => _leftDockVisible;
  bool get rightDockVisible => _rightDockVisible;

  /// 双进程已移除；保留 getter 供 UI 编译。
  bool get isDualProcessNative => false;
  bool get isEngineNative => _engineHandle != null;
  bool get isNativeRendererActive => isEngineNative;
  Uint8List? get nativeViewportPng => _nativeViewportPng;
  ui.Image? get nativeFrameImage => _nativeFrameImage;
  int get nativeFrameWidth => _engineW > 0 ? _engineW : 0;
  int get nativeFrameHeight => _engineH > 0 ? _engineH : 0;
  String? get nativeViewerError => _nativeViewerError;

  void setPanning(bool panning) {
    if (_isPanning != panning) {
      _isPanning = panning;
      notifyListeners();
    }
  }

  void resetCamera() {
    _cameraAzimuth = -45.0;
    _cameraElevation = 30.0;
    _cameraZoom = 1.0;
    _cameraPanX = 0.0;
    _cameraPanY = 0.0;
    _isPanning = false;
    final engine = _engineHandle;
    if (engine != null) {
      // 相机重置是轻量数学，在 UI isolate 直接调用；随后异步拉一帧。
      _bindings?.engine_reset_camera_opt?.call(engine);
      unawaited(_renderEngineFrame());
    }
    // 无 engine 时相机由 Dart 状态持有并直接生效，无需后端调用（R3）。
    notifyListeners();
  }

  void setCameraView({
    double? azimuth,
    double? elevation,
    double? zoom,
    double? panX,
    double? panY,
    String? preset,
  }) {
    final previousPanX = _cameraPanX;
    final previousPanY = _cameraPanY;
    if (preset != null) {
      switch (preset.toLowerCase()) {
        case 'top':
          _cameraAzimuth = 0.0;
          _cameraElevation = 90.0;
          break;
        case 'front':
          _cameraAzimuth = 0.0;
          _cameraElevation = 0.0;
          break;
        case 'side':
          _cameraAzimuth = 90.0;
          _cameraElevation = 0.0;
          break;
        case 'iso':
        default:
          _cameraAzimuth = -45.0;
          _cameraElevation = 30.0;
          break;
      }
    }
    if (azimuth != null) _cameraAzimuth = azimuth;
    if (elevation != null) _cameraElevation = elevation.clamp(-89.0, 89.0);
    if (zoom != null) _cameraZoom = zoom.clamp(0.05, 50.0);
    if (panX != null) _cameraPanX = panX;
    if (panY != null) _cameraPanY = panY;

    if (isEngineNative) {
      final axis = preset == null ? null : _nativeAxisForPreset(preset);
      if (axis != null) {
        // 轴对齐预设（顶 / 前 / 侧）：走引擎自带 gizmo 预设，由内核按其真实
        // 包围盒重算 look_at 与 up（顶/底视已做防奇异处理）。
        final gizmoFn = _bindings?.engine_gizmo_axis_opt;
        final eng = _engineHandle;
        final rc = (gizmoFn != null && eng != null) ? gizmoFn(eng, axis) : -1;
        if (rc == 0) {
          _scheduleEngineRender();
        } else {
          // 引擎 gizmo 不可用：退回 Dart 姿态推导，避免预设视角失效。
          _syncEngineCameraFromAzElZoom();
        }
      } else {
        final hasAbsolutePose =
            preset != null ||
            azimuth != null ||
            elevation != null ||
            zoom != null;
        if (hasAbsolutePose) {
          // 等轴测 / 自由角度：look_at 一次性同步。
          _syncEngineCameraFromAzElZoom();
        } else if (panX != null || panY != null) {
          // 纯平移增量：走增量相机通道，避免覆盖绝对姿态。
          _queueNativeCamera({
            'pan_delta_x': _cameraPanX - previousPanX,
            'pan_delta_y': _cameraPanY - previousPanY,
          });
        }
      }
    }
    // 无 engine 时相机完全由 Dart 状态持有，不产生无后端的伪动作（R3）。
    if (!isNativeRendererActive ||
        preset != null ||
        azimuth != null ||
        elevation != null ||
        zoom != null) {
      notifyListeners();
    }
  }

  void setViewportOverlays({bool? mapAxis, bool? worldAxis, bool? crosshair}) {
    if (mapAxis != null) _showMapAxis = mapAxis;
    if (worldAxis != null) _showWorldAxis = worldAxis;
    if (crosshair != null) _showCrosshair = crosshair;
    notifyListeners();
  }

  void setPointShape(String shape) {
    String normalized = shape;
    switch (shape.toLowerCase()) {
      case 'square':
        normalized = '方形';
        break;
      case 'circle':
        normalized = '圆形';
        break;
      case 'diamond':
        normalized = '菱形';
        break;
      case 'triangle':
        normalized = '三角形';
        break;
    }
    if (_pointShape != normalized) {
      _pointShape = normalized;
      // 点形状 / 高度 / 着色源统一由 _applyEngineRenderSettings 全量下发。
      _applyEngineRenderSettings();
      notifyListeners();
    }
  }

  void setHeightScale(double scale) {
    final clamped = scale.clamp(0.1, 10.0);
    if (_heightScale != clamped) {
      _heightScale = clamped;
      _applyEngineRenderSettings();
      notifyListeners();
    }
  }

  void setHeightSource(String source) {
    if (_heightSource != source) {
      _heightSource = source;
      _applyEngineRenderSettings();
      notifyListeners();
    }
  }

  void setColorAttribute(String attr) {
    if (_colorAttribute != attr) {
      _colorAttribute = attr;
      _applyEngineRenderSettings();
      notifyListeners();
    }
  }

  void setViewportBackgroundColor(Color color) {
    if (_viewportBackgroundColor != color) {
      _viewportBackgroundColor = color;
      _applyEngineRenderSettings();
      notifyListeners();
    }
  }

  void setTheme(int index) {
    final clamped = index.clamp(0, kThemeNames.length - 1);
    if (_themeIndex != clamped) {
      _themeIndex = clamped;
      notifyListeners();
    }
  }

  void cycleTheme() => setTheme((_themeIndex + 1) % kThemeNames.length);

  void setCameraLinked(bool linked) {
    if (_cameraLinked == linked) return;
    _cameraLinked = linked;
    notifyListeners();
  }

  void toggleCameraLinked() => setCameraLinked(!_cameraLinked);

  void requestNewView() {
    setCameraView(preset: 'iso');
  }

  void requestNewWorkspaceWindow() {
    restoreWorkspace();
  }

  void restoreWorkspace() {
    _leftDockVisible = true;
    _rightDockVisible = true;
    _layoutMode = WorkbenchLayoutMode.standard;
    _showMapAxis = true;
    _showWorldAxis = false;
    _showCrosshair = true;
    _isCommandPaletteOpen = false;
    _isShortcutOverlayOpen = false;
    _isPerformancePanelOpen = false;
    _isTileInspectorOpen = false;
    _isRenderSettingsOpen = false;
    notifyListeners();
  }

  void setNativePanelVisible(String panelId, bool visible) {
    if (!_nativePanelVisible.containsKey(panelId)) return;
    _nativePanelVisible[panelId] = visible;
    notifyListeners();
  }

  void toggleNativePanel(String panelId) {
    final current = _nativePanelVisible[panelId] ?? false;
    setNativePanelVisible(panelId, !current);
  }

  /// 打开文件：A 组原生对话框 → 交给既有加载流程。
  void openViaNativeFileMenu() {
    unawaited(_openPickedDataset(bundle: false));
  }

  void openViaNativeBundleMenu() {
    unawaited(_openPickedDataset(bundle: true));
  }

  Future<void> _openPickedDataset({required bool bundle}) async {
    if (_bindings == null || !_initialized) return;
    try {
      String path;
      if (bundle) {
        path = _readFfiCstr(_bindings!.pick_folder());
      } else {
        final filter = 'dataset'.toNativeUtf8();
        try {
          path = _readFfiCstr(_bindings!.pick_file(filter));
        } finally {
          calloc.free(filter);
        }
      }
      if (path.isEmpty) return;
      await executeActionAsync('welcome.open_project', {'path': path});
    } catch (error) {
      _nativeViewerError = '打开对话框失败: $error';
      notifyListeners();
    }
  }

  void requestNativeMenuScreenshot() {
    requestScreenshot();
  }

  void rememberRecentProject(String path) {
    if (_bindings == null || path.isEmpty) return;
    final nativePath = path.toNativeUtf8();
    try {
      _bindings!.remember_recent_project(nativePath);
      _refreshRecentProjects();
    } catch (_) {
    } finally {
      calloc.free(nativePath);
    }
  }

  /// 查询原生诊断，补齐状态栏只读信息。
  Future<void> refreshNativeStatusBar() async {
    if (isEngineNative) {
      _pollEngineMetrics();
    }
  }

  void resetScalarRange() {
    _scalarMin = _summary.minValue;
    _scalarMax = _summary.maxValue;
    setScalarRange(_scalarMin, _scalarMax);
  }

  void toggleLeftDock([bool? visible]) {
    _leftDockVisible = visible ?? !_leftDockVisible;
    notifyListeners();
  }

  void toggleRightDock([bool? visible]) {
    _rightDockVisible = visible ?? !_rightDockVisible;
    notifyListeners();
  }

  /// 全局属性统计。
  ///
  /// 优先走原生 `gs3d_ffi_calculate_region_stats`：以数据集包围盒为选区，
  /// 对**全量**点集求真实总体统计量。原生不可用（未载入 / 无 DLL）时退回
  /// Dart 有界预览样本重算，并以 `source` 字段显式标注，避免把 64K 采样值
  /// 当作总体值对外声称「统计完成」。
  Map<String, dynamic> calculateStats() {
    final native = _nativeGlobalStats();
    if (native != null) return native;
    return _dartSampleStats();
  }

  /// 原生全量统计：返回 null 表示原生路径不可用。
  Map<String, dynamic>? _nativeGlobalStats() {
    final b = _bindings;
    if (b == null || !_initialized) return null;
    final bmin = _summary.bboxMin;
    final bmax = _summary.bboxMax;
    if (bmin.length < 3 || bmax.length < 3) return null;

    final pCount = calloc<ffi.Uint64>();
    final pMean = calloc<ffi.Float>();
    final pMin = calloc<ffi.Float>();
    final pMax = calloc<ffi.Float>();
    final pStdDev = calloc<ffi.Float>();
    final pHist = calloc<ffi.Int32>(5);
    try {
      final res = b.calculate_region_stats(
        bmin[0],
        bmax[0],
        bmin[1],
        bmax[1],
        pCount,
        pMean,
        pMin,
        pMax,
        pStdDev,
        pHist,
      );
      if (res != 1 || pCount.value == 0) return null;
      final minV = pMin.value;
      final maxV = pMax.value;
      return {
        'source': 'native_region_stats',
        'count': pCount.value,
        'mean': pMean.value,
        'min': minV,
        'max': maxV,
        'range': maxV - minV,
        'std_dev': pStdDev.value,
        'histogram': [pHist[0], pHist[1], pHist[2], pHist[3], pHist[4]],
      };
    } catch (_) {
      return null;
    } finally {
      calloc.free(pCount);
      calloc.free(pMean);
      calloc.free(pMin);
      calloc.free(pMax);
      calloc.free(pStdDev);
      calloc.free(pHist);
    }
  }

  /// Dart 有界采样兜底（最多 64K 预览点）。仅当原生路径不可用时使用。
  Map<String, dynamic> _dartSampleStats() {
    final count = _points.length;
    final bins = [0, 0, 0, 0, 0];
    if (count > 0 && _scalarMax > _scalarMin) {
      final step = (_scalarMax - _scalarMin) / 5.0;
      for (final p in _points) {
        final idx = ((p.value - _scalarMin) / step).floor().clamp(0, 4);
        bins[idx]++;
      }
    }

    if (count == 0) {
      return {
        'source': 'dart_sample',
        'count': 0,
        'mean': 0.0,
        'min': _scalarMin,
        'max': _scalarMax,
        'range': 0.0,
        'std_dev': 0.0,
        'histogram': bins,
      };
    }
    double sum = 0.0;
    for (final p in _points) {
      sum += p.value;
    }
    final mean = sum / count;
    double varSum = 0.0;
    for (final p in _points) {
      varSum += (p.value - mean) * (p.value - mean);
    }
    final stdDev = math.sqrt(varSum / count);
    return {
      'source': 'dart_sample',
      'count': count,
      'mean': mean,
      'min': _scalarMin,
      'max': _scalarMax,
      'range': _scalarMax - _scalarMin,
      'std_dev': stdDev,
      'histogram': bins,
    };
  }

  Map<String, dynamic> calculateMeasurement([String type = 'distance']) {
    // 优先使用真实测量线；无测量线时回退包围盒对角线（兼容离线演示）。
    if (_measurementLines.isNotEmpty) {
      final line = _measurementLines.last;
      final dx = (line.end.x - line.start.x).abs();
      final dy = (line.end.y - line.start.y).abs();
      final dz = (line.end.z - line.start.z).abs();
      final diagonal = math.sqrt(dx * dx + dy * dy + dz * dz);
      final area2d = dx * dy;
      final strike = (dx > 0 || dy > 0)
          ? (math.atan2(dy, dx) * 180 / math.pi).abs()
          : 0.0;
      final dip = (diagonal > 0)
          ? (math.asin((dz / diagonal).clamp(-1.0, 1.0)) * 180 / math.pi)
          : 0.0;
      String metric = '3d_distance';
      double metricValue = diagonal;
      if (type == 'area') {
        metric = 'projected_area';
        metricValue = area2d;
      } else if (type == 'strike_dip') {
        metric = 'strike_dip';
        metricValue = dip;
      }
      return {
        'type': type,
        'source': 'measurement_lines',
        'line_count': _measurementLines.length,
        'line_id': line.id,
        'metric': metric,
        'metric_value': metricValue,
        'dx': dx,
        'dy': dy,
        'dz': dz,
        'distance_3d': diagonal,
        'distance_2d': math.sqrt(dx * dx + dy * dy),
        'area_2d': area2d,
        'strike': strike,
        'dip': dip,
        'dip_direction': (strike + 90.0) % 360.0,
        'start': [line.start.x, line.start.y, line.start.z],
        'end': [line.end.x, line.end.y, line.end.z],
      };
    }
    if (_pendingMeasureStart != null) {
      return {
        'type': type,
        'source': 'pending_start',
        'line_count': 0,
        'metric': 'awaiting_end_point',
        'metric_value': 0.0,
        'start': [
          _pendingMeasureStart!.x,
          _pendingMeasureStart!.y,
          _pendingMeasureStart!.z,
        ],
      };
    }

    final bmin = _summary.bboxMin;
    final bmax = _summary.bboxMax;
    final dx = (bmax[0] - bmin[0]).abs();
    final dy = (bmax[1] - bmin[1]).abs();
    final dz = (bmax[2] - bmin[2]).abs();
    final diagonal = math.sqrt(dx * dx + dy * dy + dz * dz);
    final area2d = dx * dy;
    final strike = (dx > 0) ? (math.atan2(dy, dx) * 180 / math.pi).abs() : 0.0;
    final dip = (diagonal > 0)
        ? (math.asin((dz / diagonal).clamp(-1.0, 1.0)) * 180 / math.pi)
        : 0.0;

    String metric = '3d_distance';
    double metricValue = diagonal;
    if (type == 'area') {
      metric = 'projected_area';
      metricValue = area2d;
    } else if (type == 'strike_dip') {
      metric = 'strike_dip';
      metricValue = dip;
    }

    return {
      'type': type,
      'source': 'dataset_bbox',
      'line_count': 0,
      'metric': metric,
      'metric_value': metricValue,
      'dx': dx,
      'dy': dy,
      'dz': dz,
      'distance_3d': diagonal,
      'area_2d': area2d,
      'strike': strike,
      'dip': dip,
      'dip_direction': (strike + 90.0) % 360.0,
    };
  }

  /// 视口截图。
  ///
  /// - engine 激活：读取引擎当前 RGBA 帧并由后端编码 PNG，Dart 仅负责落盘
  ///   （真实后端路径，不含 Dart 叠加层）。
  /// - 无 engine：回退 `RepaintBoundary` 内存回读（含叠加层），由视口组件写入
  ///   `tmp/`。
  void requestScreenshot() {
    if (isEngineNative) {
      unawaited(_captureEngineScreenshot());
      return;
    }
    onScreenshotRequested?.call();
  }

  Future<void> _captureEngineScreenshot() async {
    String? path;
    try {
      final timestamp = DateTime.now().microsecondsSinceEpoch;
      final temporaryPath = Platform.isAndroid
          ? '${Directory.systemTemp.path}/GeoScatter3D/exports/'
                'screenshot_$timestamp.png'
          : null;
      path = await captureEngineFramePng(outputPath: temporaryPath);
      if (path == null) {
        _nativeViewerError = '引擎帧尚未就绪，无法编码 PNG 截图';
        notifyListeners();
        return;
      }
      if (Platform.isAndroid) {
        final saved = await saveFileToDevice(
          sourcePath: path,
          suggestedName: 'geoscatter3d_viewport_$timestamp.png',
          mimeType: 'image/png',
        );
        if (saved) {
          onScreenshotSaved?.call('geoscatter3d_viewport_$timestamp.png');
        } else {
          _nativeViewerError = '视口截图已取消或保存失败';
          notifyListeners();
        }
        return;
      }
      onScreenshotSaved?.call(path);
    } finally {
      if (Platform.isAndroid && path != null) {
        try {
          final file = File(path);
          if (await file.exists()) await file.delete();
        } catch (_) {
          // 截图已经交给系统后，临时文件清理失败不覆盖用户结果。
        }
      }
    }
  }

  /// 读取引擎当前帧 → PNG → 写入 `tmp/`（或指定路径）。
  ///
  /// 返回落盘的绝对路径；engine 未激活 / 尚未渲染过返回 null。
  Future<String?> captureEngineFramePng({String? outputPath}) async {
    final e = _engineHandle;
    final b = _bindings;
    if (e == null || b == null) return null;
    if (b.engine_read_frame_png_opt == null) return null;
    final w = _engineW;
    final h = _engineH;
    if (w <= 0 || h <= 0) return null;
    final addr = e.address;
    try {
      final bytes = await Isolate.run(() {
        final lib = GeoScatter3dBindings.openLibrary();
        final enc = lib.engine_read_frame_png_opt;
        if (enc == null) return null;
        final eng = Pointer<Void>.fromAddress(addr);
        final maxBytes = w * h * 4 + 4096;
        final buf = calloc<Uint8>(maxBytes);
        final outLen = calloc<Uint32>();
        try {
          final rc = enc(eng, buf, maxBytes, outLen);
          if (rc != 0) return null;
          final len = outLen.value;
          if (len == 0) return null;
          return Uint8List.fromList(buf.asTypedList(len));
        } finally {
          calloc.free(buf);
          calloc.free(outLen);
        }
      });
      if (bytes == null || bytes.isEmpty) return null;
      final target = outputPath ?? defaultScreenshotPath();
      final file = File(target);
      final parent = file.parent;
      if (!parent.existsSync()) parent.createSync(recursive: true);
      await file.writeAsBytes(bytes, flush: true);
      return file.path.replaceAll('\\', '/');
    } catch (error) {
      _nativeViewerError = 'engine 截图失败: $error';
      return null;
    }
  }

  /// 截图默认落盘路径：仓库/运行目录下 `tmp/screenshot_<时间戳>.png`
  /// （遵守 R4：临时产物统一收敛于 `tmp/`）。
  static String defaultScreenshotPath() {
    final stamp = DateTime.now()
        .toIso8601String()
        .replaceAll(':', '-')
        .replaceAll('.', '-');
    return '${Directory.current.path}${Platform.pathSeparator}tmp'
        '${Platform.pathSeparator}screenshot_$stamp.png';
  }

  /// 启动原生渲染：仅 `gs3d_engine_*`；失败返回 false 并设置错误（无 Dart 回退）。
  Future<bool> startNativeRenderer() async {
    final datasetPath = _loadedDatasetPath;
    if (datasetPath == null || datasetPath.isEmpty) {
      _nativeViewerError = '请先载入数据集';
      notifyListeners();
      return false;
    }

    try {
      await stopNativeRenderer();
    } catch (error) {
      _nativeViewerError = '停止旧渲染器时出错: $error';
    }
    if (_bindings?.hasEngineApi != true) {
      _nativeViewerError = '需要 gs3d_engine_* 单进程内核（请重建并部署 gs3d_ffi.dll）';
      notifyListeners();
      return false;
    }
    try {
      await _ensureNativeShaderDirectory();
    } catch (error) {
      _nativeViewerError = '移动端着色器资源准备失败: $error';
      notifyListeners();
      return false;
    }
    final ok = await _startEngineRenderer(datasetPath);
    if (!ok) {
      notifyListeners();
    }
    return ok;
  }

  Future<bool> _startEngineRenderer(String datasetPath) async {
    final outer = _bindings;
    if (outer == null ||
        (outer.engine_create_opt == null &&
            outer.engine_create_simple_opt == null) ||
        outer.engine_load_dataset_opt == null ||
        outer.engine_render_rgba_opt == null ||
        outer.engine_camera_input_opt == null) {
      return false;
    }
    final w = (_lastSyncedNativeW ?? 1280).clamp(64, 4096);
    final h = (_lastSyncedNativeH ?? 720).clamp(64, 4096);
    final shaderDirectory = _nativeShaderDirectory;
    try {
      // create/load 可能阻塞；Isolate 内重新打开库，避免跨 isolate 捕获 FFI 对象。
      final addr = await Isolate.run(() {
        final b = GeoScatter3dBindings.openLibrary();
        final load = b.engine_load_dataset_opt;
        if (load == null) return 0;

        // 优先走完整 gs3d_engine_create：可携带视口尺寸 / GPU 偏好 /
        // 着色器目录 / validation 开关；旧 DLL 缺符号时回退 create_simple。
        Pointer<Void> e;
        final fullCreate = b.engine_create_opt;
        if (fullCreate != null) {
          final shaderPtr = shaderDirectory?.toNativeUtf8();
          final cfg = calloc<Gs3dEngineConfig>();
          try {
            cfg.ref.preferred_gpu_uuid = nullptr; // 空 = auto，由内核解析偏好
            cfg.ref.viewport_width = w;
            cfg.ref.viewport_height = h;
            cfg.ref.shader_dir = shaderPtr ?? nullptr;
            cfg.ref.enable_validation = 0;
            e = fullCreate(cfg);
          } finally {
            calloc.free(cfg);
            if (shaderPtr != null) calloc.free(shaderPtr);
          }
        } else {
          final simpleCreate = b.engine_create_simple_opt;
          if (simpleCreate == null) return 0;
          e = simpleCreate(w, h);
        }
        if (e.address == 0) return 0;

        final pathPtr = datasetPath.toNativeUtf8();
        try {
          final rc = load(e, pathPtr);
          if (rc != 0) {
            b.engine_destroy_opt?.call(e);
            return 0;
          }
        } finally {
          calloc.free(pathPtr);
        }
        b.engine_reset_camera_opt?.call(e);
        return e.address;
      });
      if (addr == 0) {
        final err = outer.engine_last_global_error_opt?.call().toDartString();
        _nativeViewerError = '单进程引擎启动失败: ${err ?? 'unknown'}';
        return false;
      }
      _engineHandle = Pointer<Void>.fromAddress(addr);
      _engineW = w;
      _engineH = h;
      _nativeViewerError = null;
      _nativeSnapshot = {'ready': true, 'point_count': _summary.pointCount};
      // 把当前 UI 渲染参数同步进 engine，避免首帧与侧栏状态脱节。
      // 不在 apply 里再 schedule，避免与下面的 await 首帧竞态。
      _applyEngineRenderSettings(scheduleRender: false);
      await _renderEngineFrame();
      if (_nativeFrameImage == null) {
        // 首帧失败：销毁句柄并报错；无软件预览回退。
        _engineHandle = null;
        try {
          await Isolate.run(() {
            final b = GeoScatter3dBindings.openLibrary();
            b.engine_destroy_opt?.call(Pointer<Void>.fromAddress(addr));
          });
        } catch (_) {}
        final err = _nativeViewerError ?? '首帧渲染失败';
        _nativeViewerError = '单进程引擎启动失败: $err';
        return false;
      }
      _restartNativeFrameTimer();
      unawaited(refreshMinimap());
      notifyListeners();
      return true;
    } catch (error) {
      _nativeViewerError = '单进程引擎异常: $error';
      _engineHandle = null;
      return false;
    }
  }

  /// 将 Flutter asset 中的 SPIR-V 解压到应用沙盒，供 C++ 通过文件系统读取。
  /// Android APK 内资源可能被压缩，原生 Vulkan 内核不能直接把 asset 名称当路径。
  Future<String?> _ensureNativeShaderDirectory() async {
    if (!Platform.isAndroid) return null;
    final directory = Directory(
      '${Directory.systemTemp.path}/GeoScatter3D/shaders',
    );
    await directory.create(recursive: true);
    const assets = <String>[
      'assets/shaders/point.vert.spv',
      'assets/shaders/point.frag.spv',
    ];
    for (final asset in assets) {
      final data = await rootBundle.load(asset);
      final bytes = data.buffer.asUint8List(
        data.offsetInBytes,
        data.lengthInBytes,
      );
      final file = File('${directory.path}/${asset.split('/').last}');
      if (!await file.exists() || await file.length() != bytes.length) {
        await file.writeAsBytes(bytes, flush: true);
      }
    }
    _nativeShaderDirectory = directory.path;
    return directory.path;
  }

  /// 异步渲染一帧 engine RGBA → ui.Image（不阻塞 UI 超过必要拷贝）。
  Future<void> _renderEngineFrame() async {
    final e = _engineHandle;
    if (e == null || _nativeFrameBusy) return;
    _nativeFrameBusy = true;
    final addr = e.address;
    final w = _engineW;
    final h = _engineH;
    final sw = Stopwatch()..start();
    try {
      final result = await Isolate.run(() {
        final b = GeoScatter3dBindings.openLibrary();
        final render = b.engine_render_rgba_opt;
        if (render == null) return <Object>['error', 'render missing'];
        final eng = Pointer<Void>.fromAddress(addr);
        final maxBytes = w * h * 4;
        final buf = calloc<Uint8>(maxBytes);
        final outW = calloc<Uint32>();
        final outH = calloc<Uint32>();
        try {
          final rc = render(eng, buf, maxBytes, outW, outH);
          if (rc != 0) {
            final err = b.engine_last_error_opt?.call(eng).toDartString();
            return <Object>['error', err ?? 'render rc=$rc'];
          }
          final ow = outW.value;
          final oh = outH.value;
          final bytes = Uint8List.fromList(buf.asTypedList(ow * oh * 4));
          return <Object>['ok', ow, oh, bytes];
        } finally {
          calloc.free(buf);
          calloc.free(outW);
          calloc.free(outH);
        }
      });
      if (!identical(e, _engineHandle)) return;
      if (result[0] == 'error') {
        _nativeViewerError = result[1].toString();
        return;
      }
      final ow = result[1] as int;
      final oh = result[2] as int;
      final bytes = result[3] as Uint8List;
      _engineW = ow;
      _engineH = oh;
      final image = await _decodeRgbaImage(bytes, ow, oh);
      if (!identical(e, _engineHandle)) {
        image.dispose();
        return;
      }
      _nativeFrameImage?.dispose();
      _nativeFrameImage = image;
      _nativeViewerError = null;
      notifyListeners();
    } catch (error) {
      if (identical(e, _engineHandle)) {
        _nativeViewerError = 'engine 渲染失败: $error';
      }
    } finally {
      final ms = sw.elapsedMicroseconds / 1000.0;
      if (ms > 0.3) {
        _lastEngineFrameMs = ms;
        _frameTimeMs = ms;
        _fps = 1000.0 / ms;
      }
      _nativeFrameBusy = false;
    }
  }

  static Future<ui.Image> _decodeRgbaImage(
    Uint8List rgba,
    int width,
    int height,
  ) {
    final completer = Completer<ui.Image>();
    ui.decodeImageFromPixels(
      rgba,
      width,
      height,
      ui.PixelFormat.rgba8888,
      completer.complete,
    );
    return completer.future;
  }

  void _restartNativeFrameTimer() {
    _nativeFrameTimer?.cancel();
    // engine：空闲低频即可，交互由 camera flush 驱动；双进程交互 33ms。
    final interval = isEngineNative
        ? (_nativeInteracting
              ? const Duration(milliseconds: 16)
              : const Duration(milliseconds: 33))
        : (_nativeInteracting
              ? const Duration(milliseconds: 33)
              : const Duration(milliseconds: 120));
    _nativeFrameTimer = Timer.periodic(interval, (_) {
      if (isEngineNative) {
        if (!_nativeInteracting) unawaited(_renderEngineFrame());
      }
    });
  }

  Future<void> refreshNativeViewport({bool includeState = true}) async {
    if (isEngineNative) {
      await _renderEngineFrame();
    }
  }

  /// 原生主线程产生的纯数据快照统一归并到状态中枢。
  void applyNativeSnapshot(Map<String, dynamic> state) {
    _nativeSnapshot = Map.unmodifiable(state);
    Point3D? point(dynamic value) {
      if (value is! List || value.length < 4) return null;
      return Point3D(
        x: (value[0] as num).toDouble(),
        y: (value[1] as num).toDouble(),
        z: (value[2] as num).toDouble(),
        value: (value[3] as num).toDouble(),
      );
    }

    _hoveredPoint = point(state['hover']);
    _selectedPoint = point(state['selected']);
    _pendingMeasureStart = point(state['pending_point']);
    _isMeasurementMode = state['measurement_mode'] == true;
    final mode = (state['measurement_display_mode'] as num?)?.toInt() ?? 0;
    _measurementDisplayMode = mode == 1 ? '2d' : (mode == 2 ? 'both' : '3d');
    _measurementLines.clear();
    for (final item in (state['measurement_lines'] as List? ?? const [])) {
      final start = point(item['start']);
      final end = point(item['end']);
      if (start == null || end == null) continue;
      final rgba = (item['color'] as num?)?.toInt() ?? 0xff1bc2f1;
      _measurementLines.add(
        MeasurementLineItem(
          id: item['id'].toString(),
          start: start,
          end: end,
          fixed: item['fixed'] == true,
          color: Color(
            (rgba & 0xff00ff00) | ((rgba & 0xff) << 16) | ((rgba >> 16) & 0xff),
          ),
        ),
      );
    }
    if (state['region_valid'] == true || state['region_computing'] == true) {
      _activeRegionStats = Map<String, dynamic>.from(
        state['region_stats'] as Map,
      );
    }
    final error = state['region_error'];
    if (error is String && error.isNotEmpty) _nativeViewerError = error;
    notifyListeners();
  }

  Future<void> stopNativeRenderer() async {
    _nativeFrameTimer?.cancel();
    _nativeFrameTimer = null;
    _camFlushTimer?.cancel();
    _camFlushTimer = null;
    _pickFlushTimer?.cancel();
    _pickFlushTimer = null;
    _pendingPickParams = null;
    _nativeInteracting = false;
    final engine = _engineHandle;
    _engineHandle = null;
    _nativeFrameImage?.dispose();
    _nativeFrameImage = null;
    _nativeViewportPng = null;
    _nativeSnapshot = const {};
    if (engine != null) {
      // 只捕获 address（int）；在 isolate 内重新 openLibrary，避免把
      // 带 Timer/ChangeNotifier 的 this 送进 Isolate.run（unsendable）。
      final addr = engine.address;
      try {
        await Isolate.run(() {
          final b = GeoScatter3dBindings.openLibrary();
          final e = Pointer<Void>.fromAddress(addr);
          // 先显式卸载数据集（释放 CPU 点缓存 / GPU 上传水位），再销毁内核。
          b.engine_unload_dataset_opt?.call(e);
          b.engine_destroy_opt?.call(e);
        });
      } catch (error) {
        // destroy 失败不阻断后续重启；句柄已从服务层摘除。
        _nativeViewerError ??= 'engine destroy: $error';
      }
    }
    notifyListeners();
  }

  void _queueNativeCamera(Map<String, dynamic> params) {
    final rx = (params['rotate_delta_x'] as num?)?.toDouble() ?? 0;
    final ry = (params['rotate_delta_y'] as num?)?.toDouble() ?? 0;
    final px = (params['pan_delta_x'] as num?)?.toDouble() ?? 0;
    final py = (params['pan_delta_y'] as num?)?.toDouble() ?? 0;
    final sy = (params['scroll_y'] as num?)?.toDouble() ?? 0;
    if (rx == 0 && ry == 0 && px == 0 && py == 0 && sy == 0) return;

    // 单进程：手势事件立刻改相机姿态（跟手），渲染异步跟上。
    final engine = _engineHandle;
    if (engine == null) return;
    _setNativeInteracting(true);
    final rc = _bindings?.engine_camera_input_opt?.call(
      engine,
      rx,
      ry,
      px,
      py,
      sy,
    );
    if (rc != null && rc != 0) {
      _nativeViewerError = 'engine camera_input rc=$rc';
    }
    _scheduleEngineRender();
  }

  void _setNativeInteracting(bool value) {
    if (_nativeInteracting == value) return;
    _nativeInteracting = value;
    _restartNativeFrameTimer();
  }

  /// 相机已同步改完，只需排队出帧；在途渲染结束后补最新姿态。
  void _scheduleEngineRender() {
    if (_engineHandle == null) return;
    if (_nativeFrameBusy) {
      _engineRenderQueued = true;
      return;
    }
    unawaited(
      _renderEngineFrame().then((_) {
        if (_engineRenderQueued && _engineHandle != null) {
          _engineRenderQueued = false;
          _scheduleEngineRender();
        }
      }),
    );
  }

  int? _lastSyncedNativeW;
  int? _lastSyncedNativeH;

  /// 将 Flutter 视口像素尺寸同步到原生离屏分辨率，避免前后端画布不一致。
  void syncNativeViewportSize(int width, int height) {
    if (width < 16 || height < 16) return;
    if (_lastSyncedNativeW == width && _lastSyncedNativeH == height) return;
    _lastSyncedNativeW = width;
    _lastSyncedNativeH = height;
    final engine = _engineHandle;
    if (engine != null) {
      _engineW = width;
      _engineH = height;
      _bindings?.engine_resize_opt?.call(engine, width, height);
      unawaited(_renderEngineFrame());
    }
    // 无 engine：仅记录目标尺寸，待引擎创建时按此分辨率初始化（R3）。
  }

  void queueNativePick(
    String kind, {
    required double localX,
    required double localY,
    required double displayWidth,
    required double displayHeight,
    double? localMaxX,
    double? localMaxY,
  }) {
    final point = pickPointAt(localX, localY, displayWidth, displayHeight);
    setHoveredPoint(point);
  }

  /// 高频 hover 只保留最后一帧，避免每次 onHover 都打一发引擎。
  Timer? _pickFlushTimer;
  Map<String, dynamic>? _pendingPickParams;

  void queueNativePickThrottled(
    String kind, {
    required double localX,
    required double localY,
    required double displayWidth,
    required double displayHeight,
  }) {
    if (!isEngineNative) return;
    _pendingPickParams = {
      'x': localX,
      'y': localY,
      'w': displayWidth,
      'h': displayHeight,
    };
    _pickFlushTimer?.cancel();
    _pickFlushTimer = Timer(const Duration(milliseconds: 33), () {
      final p = _pendingPickParams;
      _pendingPickParams = null;
      if (p == null) return;
      final point = pickPointAt(
        p['x'] as double,
        p['y'] as double,
        p['w'] as double,
        p['h'] as double,
      );
      setHoveredPoint(point);
    });
  }

  String _readFfiCstr(Pointer<Utf8> ptr) {
    if (ptr == nullptr) return '';
    return ptr.toDartString();
  }

  /// 预设视角 → engine gizmo 轴索引（0=-X, 1=+X, 2=-Y, 3=+Y, 4=+Z, 5=-Z）。
  static int? _nativeAxisForPreset(String? preset) {
    switch (preset?.toLowerCase()) {
      case 'top':
        return 4; // +Z 俯视
      case 'front':
        return 1; // +X 正视
      case 'side':
        return 2; // -Y 侧视
      default:
        return null; // iso 等自由角度走 look_at
    }
  }

  static int? _nativePointShape(String shape) {
    switch (shape) {
      case '方形':
      case 'square':
        return 0;
      case '圆形':
      case 'circle':
        return 1;
      case '菱形':
      case 'diamond':
        return 2;
      case '三角形':
      case 'triangle':
        return 3;
      default:
        return null;
    }
  }

  void _startMetricsPolling() {
    _metricsTimer?.cancel();
    _metricsTimer = Timer.periodic(const Duration(milliseconds: 500), (_) {
      _pollMetrics();
    });
  }

  void stopMetricsPolling() {
    _metricsTimer?.cancel();
    _metricsTimer = null;
  }

  void _pollMetrics() {
    if (isEngineNative) {
      _pollEngineMetrics();
      return;
    }
    if (_bindings != null && _initialized) {
      final pFps = calloc<ffi.Float>();
      final pFrameTime = calloc<ffi.Float>();
      final pVisiblePoints = calloc<ffi.Uint64>();
      final pGpuMemBytes = calloc<ffi.Uint64>();
      final pLoadedTiles = calloc<ffi.Uint32>();
      final pPendingTiles = calloc<ffi.Uint32>();
      final pCacheHitRate = calloc<ffi.Float>();
      try {
        _bindings!.get_performance_metrics(
          pFps,
          pFrameTime,
          pVisiblePoints,
          pGpuMemBytes,
          pLoadedTiles,
          pPendingTiles,
          pCacheHitRate,
        );
        _fps = pFps.value;
        _frameTimeMs = pFrameTime.value;
        _visiblePoints = pVisiblePoints.value;
        _gpuMemoryBytes = pGpuMemBytes.value;
        _loadedTiles = pLoadedTiles.value;
        _pendingTiles = pPendingTiles.value;
        _cacheHitRate = pCacheHitRate.value;
      } catch (_) {
      } finally {
        calloc.free(pFps);
        calloc.free(pFrameTime);
        calloc.free(pVisiblePoints);
        calloc.free(pGpuMemBytes);
        calloc.free(pLoadedTiles);
        calloc.free(pPendingTiles);
        calloc.free(pCacheHitRate);
      }

      try {
        final pCoords = _bindings!.get_camera_coords_string();
        final coordsStr = pCoords.toDartString();
        if (coordsStr.isNotEmpty) {
          _cameraCoordsString = coordsStr;
        }
      } catch (_) {}
    } else {
      _visiblePoints = _points.length;
      _cameraCoordsString =
          '${_cameraPanX.toStringAsFixed(1)}, ${_cameraPanY.toStringAsFixed(1)}, ${(100.0 * _cameraZoom).toStringAsFixed(1)}';
    }
    notifyListeners();
  }

  void clearCache() {
    // 单进程 engine：清帧缓存并重建上传；A 组镜像一并清理。
    if (_bindings != null && _initialized) {
      try {
        _bindings!.clear_cache();
      } catch (_) {}
    }
    final engine = _engineHandle;
    final fn = _bindings?.engine_clear_tile_cache_opt;
    if (engine != null && fn != null) {
      try {
        fn(engine);
      } catch (_) {}
    }
    _loadedTiles = 0;
    _pendingTiles = 0;
    _cacheHitRate = -1.0;
    if (isEngineNative) _scheduleEngineRender();
    notifyListeners();
  }

  void _pollEngineMetrics() {
    final engine = _engineHandle;
    final b = _bindings;
    if (engine == null || b == null) return;
    try {
      final snapRaw = b.engine_get_snapshot_json_opt
          ?.call(engine)
          .toDartString();
      if (snapRaw != null && snapRaw.isNotEmpty) {
        final decoded = jsonDecode(snapRaw);
        if (decoded is Map) {
          _nativeSnapshot = Map<String, dynamic>.from({
            ..._nativeSnapshot,
            ...decoded.map((k, v) => MapEntry(k.toString(), v)),
          });
          _visiblePoints =
              (decoded['point_count'] as num?)?.toInt() ?? _visiblePoints;
        }
      }
      final diagRaw = b.engine_get_diagnostics_json_opt
          ?.call(engine)
          .toDartString();
      if (diagRaw != null && diagRaw.isNotEmpty) {
        final decoded = jsonDecode(diagRaw);
        if (decoded is Map) {
          final err = decoded['last_error'];
          if (err is String && err.isNotEmpty) {
            _nativeViewerError = err;
          } else {
            _nativeViewerError = null;
          }
        }
      }
    } catch (_) {}
    if (_lastEngineFrameMs > 0.5) {
      _fps = 1000.0 / _lastEngineFrameMs;
      _frameTimeMs = _lastEngineFrameMs;
    }
    _cameraCoordsString =
        '${_cameraAzimuth.toStringAsFixed(1)}, ${_cameraElevation.toStringAsFixed(1)}, ${(_cameraZoom * 100).toStringAsFixed(1)}';
    // A 组性能镜像（loadDataset 后可用）作为补充
    if (_points.isNotEmpty) _visiblePoints = math.max(_visiblePoints, 0);
    notifyListeners();
  }

  Point3D? pickPointAt(
    double screenX,
    double screenY,
    double viewportWidth,
    double viewportHeight, {
    double tolerancePx = 25.0,
  }) {
    // engine 激活：用引擎真实相机 VP 拾取；否则 A 组简化投影兜底。
    final engine = _engineHandle;
    final enginePick = _bindings?.engine_pick_point_opt;
    if (engine != null && enginePick != null) {
      final pOut = calloc<ffi.Float>(4);
      try {
        final res = enginePick(engine, screenX, screenY, tolerancePx, pOut);
        if (res == 1) {
          return Point3D(x: pOut[0], y: pOut[1], z: pOut[2], value: pOut[3]);
        }
      } catch (_) {
      } finally {
        calloc.free(pOut);
      }
      return null;
    }
    if (_bindings == null || !_initialized) return null;
    final pOut = calloc<ffi.Float>(4);
    try {
      final res = _bindings!.pick_point(
        screenX,
        screenY,
        viewportWidth,
        viewportHeight,
        _cameraAzimuth,
        _cameraElevation,
        _cameraZoom,
        _cameraPanX,
        _cameraPanY,
        pOut,
      );
      if (res == 1) {
        return Point3D(x: pOut[0], y: pOut[1], z: pOut[2], value: pOut[3]);
      }
    } catch (_) {
    } finally {
      calloc.free(pOut);
    }
    return null;
  }

  void setHoveredPoint(Point3D? point) {
    if (_hoveredPoint != point) {
      _hoveredPoint = point;
      notifyListeners();
    }
  }

  void setSelectedPoint(Point3D? point) {
    if (_selectedPoint != point) {
      _selectedPoint = point;
      notifyListeners();
    }
  }

  void setOrbitPivot(Point3D point) {
    _selectedPoint = point;
    _cameraPanX = 0.0;
    _cameraPanY = 0.0;
    notifyListeners();
  }

  /// 将 Dart 相机方位角/仰角/缩放同步到 engine（look_at 绝对姿态）。
  void _syncEngineCameraFromAzElZoom() {
    final engine = _engineHandle;
    final lookAt = _bindings?.engine_set_camera_look_at_opt;
    if (engine == null || lookAt == null) return;
    final bmin = _summary.bboxMin;
    final bmax = _summary.bboxMax;
    if (bmin.length < 3 || bmax.length < 3) return;
    final centerX = (bmin[0] + bmax[0]) * 0.5;
    final centerY = (bmin[1] + bmax[1]) * 0.5;
    final centerZ = (bmin[2] + bmax[2]) * 0.5;
    final spanX = bmax[0] - bmin[0];
    final spanY = bmax[1] - bmin[1];
    final spanZ = bmax[2] - bmin[2];
    final span = math.max(spanX, math.max(spanY, spanZ));
    final dist = math.max(span * 2.0, 1.0) / math.max(_cameraZoom, 0.05);
    final az = _cameraAzimuth * math.pi / 180.0;
    final el = _cameraElevation * math.pi / 180.0;
    final posX = centerX + dist * math.cos(el) * math.cos(az);
    final posY = centerY + dist * math.cos(el) * math.sin(az);
    final posZ = centerZ + dist * math.sin(el);
    lookAt(engine, posX, posY, posZ, centerX, centerY, centerZ, 0, 0, 1);
    _scheduleEngineRender();
  }

  void setMeasurementMode(bool enabled) {
    if (_isMeasurementMode != enabled) {
      _isMeasurementMode = enabled;
      if (!enabled) {
        _pendingMeasureStart = null;
      }
      notifyListeners();
    }
  }

  void toggleMeasurementMode() {
    setMeasurementMode(!_isMeasurementMode);
  }

  void addMeasurementPoint(Point3D point) {
    if (_pendingMeasureStart == null) {
      _pendingMeasureStart = point;
      notifyListeners();
    } else {
      final start = _pendingMeasureStart!;
      final id =
          'measure_${DateTime.now().millisecondsSinceEpoch}_${_measurementLines.length + 1}';
      _measurementLines.add(
        MeasurementLineItem(
          id: id,
          start: start,
          end: point,
          color: const Color(0xFF00E5FF),
          fixed: false,
        ),
      );
      _pendingMeasureStart = null;
      notifyListeners();
    }
  }

  void clearPendingMeasurement() {
    if (_pendingMeasureStart != null) {
      _pendingMeasureStart = null;
      notifyListeners();
    }
  }

  void removeMeasurementLine(String id) {
    _measurementLines.removeWhere((item) => item.id == id);
    notifyListeners();
  }

  void clearMeasurementLines() {
    _measurementLines.clear();
    _pendingMeasureStart = null;
    notifyListeners();
  }

  void toggleMeasurementLineFixed(String id) {
    final idx = _measurementLines.indexWhere((item) => item.id == id);
    if (idx != -1) {
      final cur = _measurementLines[idx];
      _measurementLines[idx] = cur.copyWith(isFixed: !cur.isFixed);
      notifyListeners();
    }
  }

  void setMeasurementDisplayMode(String mode) {
    if (_measurementDisplayMode != mode) {
      _measurementDisplayMode = mode;
      notifyListeners();
    }
  }

  Map<String, dynamic> calculateRegionBoxStats(
    double minX,
    double minY,
    double maxX,
    double maxY,
    double viewportWidth,
    double viewportHeight,
  ) {
    if (_bindings != null && _initialized) {
      final pCount = calloc<ffi.Uint64>();
      final pMean = calloc<ffi.Float>();
      final pMin = calloc<ffi.Float>();
      final pMax = calloc<ffi.Float>();
      final pStdDev = calloc<ffi.Float>();
      final pHist = calloc<ffi.Int32>(5);
      try {
        final res = _bindings!.calculate_region_stats(
          minX,
          maxX,
          minY,
          maxY,
          pCount,
          pMean,
          pMin,
          pMax,
          pStdDev,
          pHist,
        );
        if (res == 1) {
          final stats = {
            'count': pCount.value,
            'mean': pMean.value,
            'min': pMin.value,
            'max': pMax.value,
            'std_dev': pStdDev.value,
            'histogram': [pHist[0], pHist[1], pHist[2], pHist[3], pHist[4]],
          };
          _activeRegionStats = stats;
          notifyListeners();
          return stats;
        }
      } catch (_) {
      } finally {
        calloc.free(pCount);
        calloc.free(pMean);
        calloc.free(pMin);
        calloc.free(pMax);
        calloc.free(pStdDev);
        calloc.free(pHist);
      }
    }

    // 无 A 组结果时返回空（禁止 Dart 投影硬算“可用”）
    const empty = {
      'count': 0,
      'mean': 0.0,
      'min': 0.0,
      'max': 0.0,
      'std_dev': 0.0,
      'histogram': [0, 0, 0, 0, 0],
    };
    _activeRegionStats = empty;
    notifyListeners();
    return empty;
  }

  void clearRegionStats() {
    _activeRegionStats = null;
    notifyListeners();
  }

  /// 刷新鸟瞰导航图：仅 `gs3d_ffi_get_nav_map_thumbnail`；失败返回 null。
  Future<Uint8List?> refreshMinimap({int width = 200, int height = 160}) async {
    _minimapWidth = width;
    _minimapHeight = height;

    Uint8List? buffer;
    if (_bindings != null && _initialized) {
      final bufferSize = width * height * 4;
      final pBuf = calloc<ffi.Uint8>(bufferSize);
      try {
        final res = _bindings!.get_nav_map_thumbnail(pBuf, width, height);
        if (res == 0) {
          buffer = Uint8List.fromList(pBuf.asTypedList(bufferSize));
        }
      } catch (_) {
      } finally {
        calloc.free(pBuf);
      }
    }

    _minimapRgba = buffer;
    _minimapImage?.dispose();
    _minimapImage = null;
    if (buffer != null) {
      final image = await _decodeRgbaImage(buffer, width, height);
      _minimapImage = image;
    }
    notifyListeners();
    return buffer;
  }

  void navigateCameraToWorld(double worldX, double worldY) {
    final bmin = _summary.bboxMin;
    final bmax = _summary.bboxMax;
    final midX = (bmin[0] + bmax[0]) * 0.5;
    final midY = (bmin[1] + bmax[1]) * 0.5;
    _cameraPanX = (midX - worldX) * 2.0;
    _cameraPanY = (worldY - midY) * 2.0;
    notifyListeners();
  }

  void toggleCommandPalette([bool? open]) {
    _isCommandPaletteOpen = open ?? !_isCommandPaletteOpen;
    notifyListeners();
  }

  void toggleShortcutOverlay([bool? open]) {
    _isShortcutOverlayOpen = open ?? !_isShortcutOverlayOpen;
    notifyListeners();
  }

  void togglePerformancePanel([bool? open]) {
    _isPerformancePanelOpen = open ?? !_isPerformancePanelOpen;
    notifyListeners();
  }

  void toggleTileInspector([bool? open]) {
    _isTileInspectorOpen = open ?? !_isTileInspectorOpen;
    notifyListeners();
  }

  void toggleRenderSettings([bool? open]) {
    _isRenderSettingsOpen = open ?? !_isRenderSettingsOpen;
    notifyListeners();
  }

  bool exportPointCloud(String outputPath, {String format = 'ply'}) {
    // 优先 engine 内全量导出（与渲染数据一致）
    final engine = _engineHandle;
    if (engine != null && _bindings != null) {
      final pPath = outputPath.toNativeUtf8();
      final pFmt = format.toNativeUtf8();
      try {
        final rc = _bindings!.engine_export_opt?.call(engine, pPath, pFmt);
        if (rc == 0) return true;
      } catch (_) {
      } finally {
        calloc.free(pPath);
        calloc.free(pFmt);
      }
    }
    if (_bindings != null && _initialized) {
      final pPath = outputPath.toNativeUtf8();
      final pFmt = format.toNativeUtf8();
      try {
        final res = _bindings!.export_dataset(pPath, pFmt);
        if (res == 0) return true;
      } catch (_) {
      } finally {
        calloc.free(pPath);
        calloc.free(pFmt);
      }
    }
    // 禁止 Dart 手写点集文件；导出失败即 false。
    return false;
  }

  /// 导出入口：同步走 engine/A 组；保留异步 API 形状供 UI 调用。
  Future<bool> exportPointCloudThroughNativeViewer(
    String outputPath, {
    String format = 'ply',
  }) async {
    if (Platform.isAndroid) {
      final normalizedFormat = format.toLowerCase() == 'csv' ? 'csv' : 'ply';
      final suggestedName = _androidExportSuggestedName(
        outputPath,
        normalizedFormat,
      );
      final exportDirectory = Directory(
        '${Directory.systemTemp.path}/GeoScatter3D/exports',
      );
      await exportDirectory.create(recursive: true);
      final sourcePath =
          '${exportDirectory.path}/export_${DateTime.now().microsecondsSinceEpoch}.$normalizedFormat';
      final source = File(sourcePath);
      try {
        if (!exportPointCloud(sourcePath, format: normalizedFormat)) {
          return false;
        }
        return await saveFileToDevice(
          sourcePath: sourcePath,
          suggestedName: suggestedName,
          mimeType: normalizedFormat == 'csv'
              ? 'text/csv'
              : 'application/octet-stream',
        );
      } finally {
        try {
          if (await source.exists()) await source.delete();
        } catch (_) {
          // 临时文件清理失败不应覆盖已经完成的系统保存结果。
        }
      }
    }
    return exportPointCloud(outputPath, format: format);
  }

  /// 把应用私有临时文件交给 Android 系统保存面板。
  ///
  /// C++/FFI 只接收普通文件路径；系统 Uri 只在 Kotlin 桥接层出现，
  /// 避免图形/文件句柄穿透到 UI 或原生引擎。
  Future<bool> saveFileToDevice({
    required String sourcePath,
    required String suggestedName,
    required String mimeType,
  }) async {
    if (!Platform.isAndroid) return false;
    return MobileDocumentPicker.saveFile(
      sourcePath: sourcePath,
      suggestedName: suggestedName,
      mimeType: mimeType,
    );
  }

  String _androidExportSuggestedName(String outputPath, String format) {
    final rawName = outputPath.replaceAll('\\', '/').split('/').last.trim();
    final cleaned = rawName.replaceAll(RegExp(r'[\\/:*?"<>|]'), '_');
    final withoutKnownExtension = cleaned.replaceFirst(
      RegExp(r'\.(ply|csv)$', caseSensitive: false),
      '',
    );
    final baseName = withoutKnownExtension.trim().isEmpty
        ? 'export_points'
        : withoutKnownExtension.trim();
    return '$baseName.$format';
  }

  void setLayoutMode(WorkbenchLayoutMode mode) {
    if (_layoutMode != mode) {
      _layoutMode = mode;
      notifyListeners();
    }
  }

  void openWorkbench() {
    _isWorkbenchActive = true;
    _leftDockVisible = true;
    _rightDockVisible = true;
    notifyListeners();
  }

  void closeWorkbench() {
    _isWorkbenchActive = false;
    notifyListeners();
  }

  /// 初始化底层 C++ 引擎
  bool initialize([String? customDylibPath]) {
    if (_initialized) return true;
    try {
      _bindings = GeoScatter3dBindings.openLibrary(customDylibPath);
      final res = _bindings!.init();
      _initialized = (res == 0);
      if (_initialized) {
        _refreshRecentProjects();
        _refreshGpus();
        _startMetricsPolling();
        if (Platform.isWindows) {
          try {
            _bindings!.init_drag_drop();
            _startDragDropPolling();
          } catch (_) {}
        }
      } else {
        // init 返回非 0：内核库虽已打开但自检失败，同样显式降级（R3）。
        markEngineUnavailable('渲染内核初始化失败 (init rc=$res)');
      }
      notifyListeners();
      return _initialized;
    } catch (e) {
      debugPrint('[GeoScatter3D FFI] Failed to initialize: $e');
      // 加载/初始化异常显式写入状态机并广播（R3：不再静默吞掉）。
      markEngineUnavailable(
        e is Gs3dEngineLoadException ? e.reason : e.toString(),
      );
      return false;
    }
  }

  void _startDragDropPolling() {
    _dragDropTimer?.cancel();
    _dragDropTimer = Timer.periodic(const Duration(milliseconds: 250), (_) {
      pollDroppedFile();
    });
  }

  void stopDragDropPolling() {
    _dragDropTimer?.cancel();
    _dragDropTimer = null;
  }

  /// 查询底层是否有新拖入的文件路径
  String? pollDroppedFile() {
    if (!_initialized || _bindings == null) return null;
    try {
      final ptr = _bindings!.poll_dropped_file();
      final path = ptr.toDartString();
      if (path.isNotEmpty) {
        handleDroppedPath(path);
        return path;
      }
    } catch (_) {}
    return null;
  }

  /// 手动设置/模拟拖入文件（供测试与 AI 调用）
  void setDroppedFile(String path) {
    if (_bindings == null) return;
    final ptr = path.toNativeUtf8();
    try {
      _bindings!.set_dropped_file(ptr);
    } finally {
      calloc.free(ptr);
    }
  }

  /// 处理拖入的文件或目录路径
  void handleDroppedPath(String path) {
    final norm = path.replaceAll('\\', '/');
    if (onFileDropped != null) {
      onFileDropped!(norm);
    } else {
      unawaited(executeActionAsync('welcome.open_project', {'path': norm}));
    }
    notifyListeners();
  }

  /// 移除单项最近工程记录
  void removeRecentProject(String path) {
    if (_bindings == null) return;
    final pathPtr = path.toNativeUtf8();
    try {
      _bindings!.remove_recent_project(pathPtr);
      _refreshRecentProjects();
      notifyListeners();
    } finally {
      calloc.free(pathPtr);
    }
  }

  /// 检测工程名称是否在数据目录冲突已存在
  bool checkProjectNameConflict(String name) {
    final trimmed = name.trim();
    if (trimmed.isEmpty) return false;
    final repoRoot = resolveRepoRoot();
    final bundleDir = '$repoRoot/data/$trimmed.gs3d.bundle';
    return Directory(bundleDir).existsSync();
  }

  /// 中止当前正在运行的离线预处理构建进程
  void cancelPreprocess() {
    _preprocessCancelled = true;
    if (_activePreprocessProcess != null) {
      try {
        _activePreprocessProcess!.kill(ProcessSignal.sigterm);
      } catch (_) {
        try {
          Process.killPid(_activePreprocessProcess!.pid);
        } catch (_) {}
      }
    }
    _latestProgress = const PreprocessProgressInfo(
      stage: '已取消',
      progress: 0.0,
      detail: '用户已终止构建操作',
      elapsedSeconds: 0,
      isRunning: false,
      isFinished: false,
      isFailed: true,
      errorMessage: '构建已取消',
    );
    notifyListeners();
  }

  /// 多级候选路径解析，确保在任何工作目录或构建目录下都能正确找到点云与工程文件
  static String? resolveDatasetPath(String inputPath) {
    final trimmed = inputPath.trim().replaceAll('\\', '/');
    if (trimmed.isEmpty) return null;

    final file = File(trimmed);
    if (file.isAbsolute) {
      try {
        if (file.existsSync()) {
          return file.absolute.path.replaceAll('\\', '/');
        }
        final dir = Directory(trimmed);
        if (dir.existsSync()) {
          return dir.absolute.path.replaceAll('\\', '/');
        }
      } catch (_) {}
      return null;
    }

    final candidates = [
      if (runtimeDirectory != null) '$runtimeDirectory/$trimmed',
      '${File(Platform.resolvedExecutable).parent.path}/$trimmed',
      trimmed,
      '../$trimmed',
      '../../$trimmed',
      '../../../$trimmed',
      '../../../../$trimmed',
    ];

    if (Platform.isWindows) {
      try {
        final exeParent = File(Platform.resolvedExecutable).parent;
        candidates.add('${exeParent.path}/$trimmed');
        candidates.add('${exeParent.parent.path}/$trimmed');
        candidates.add('${exeParent.parent.parent.path}/$trimmed');
        candidates.add('${exeParent.parent.parent.parent.path}/$trimmed');
        candidates.add(
          '${exeParent.parent.parent.parent.parent.path}/$trimmed',
        );
      } catch (_) {}
    }

    for (final c in candidates) {
      final normalized = c.replaceAll('\\', '/');
      try {
        if (File(normalized).existsSync()) {
          return File(normalized).absolute.path.replaceAll('\\', '/');
        }
        if (Directory(normalized).existsSync()) {
          return Directory(normalized).absolute.path.replaceAll('\\', '/');
        }
      } catch (_) {}
    }
    return null;
  }

  /// 解析代码仓根目录绝对路径
  static String resolveRepoRoot() {
    final candidates = ['.', '..', '../..'];
    for (final c in candidates) {
      if (File('$c/CMakeLists.txt').existsSync() &&
          Directory('$c/config').existsSync()) {
        return Directory(c).absolute.path.replaceAll('\\', '/');
      }
    }
    return File(
      Platform.resolvedExecutable,
    ).parent.absolute.path.replaceAll('\\', '/');
  }

  /// 解析预处理器可执行文件路径
  static String? resolvePreprocessExecutable() {
    // 以仓库根为基准解析（不依赖当前工作目录）。
    final repoRoot = resolveRepoRoot();
    final candidates = [
      if (runtimeDirectory != null)
        '$runtimeDirectory/GeoScatter3DPreprocess.exe',
      '${File(Platform.resolvedExecutable).parent.path}/GeoScatter3DPreprocess.exe',
      '$repoRoot/tmp/build-win/src/Release/GeoScatter3DPreprocess.exe',
      '$repoRoot/tmp/build-win/Release/GeoScatter3DPreprocess.exe',
      '$repoRoot/build/windows/x64/runner/Debug/GeoScatter3DPreprocess.exe',
      '$repoRoot/build/windows/x64/runner/Release/GeoScatter3DPreprocess.exe',
      'tmp/build-win/src/Release/GeoScatter3DPreprocess.exe',
      'tmp/build-win/Release/GeoScatter3DPreprocess.exe',
      'build/windows/x64/runner/Debug/GeoScatter3DPreprocess.exe',
      'build/windows/x64/runner/Release/GeoScatter3DPreprocess.exe',
      'GeoScatter3DPreprocess.exe',
    ];
    if (Platform.isWindows) {
      try {
        final exeParent = File(Platform.resolvedExecutable).parent;
        candidates.add('${exeParent.path}/GeoScatter3DPreprocess.exe');
      } catch (_) {}
    }
    for (final c in candidates) {
      final norm = c.replaceAll('\\', '/');
      if (File(norm).existsSync()) {
        return File(norm).absolute.path.replaceAll('\\', '/');
      }
    }
    return null;
  }

  /// 可注入的文件选择回调（用于自动化测试或无头运行环境模拟选择）
  String? Function(String filterType)? filePickerOverride;

  /// 可注入的目录选择回调（用于自动化测试或无头运行环境模拟选择）
  String? Function()? folderPickerOverride;

  /// 移动端通过 Android SAF 选择文件并复制到应用私有持久目录；桌面端
  /// 继续复用原有 C-ABI 文件选择器。覆盖回调优先用于 widget 测试。
  Future<String?> pickFileAsync([String filterType = 'point_cloud']) async {
    if (filePickerOverride != null) return pickFile(filterType);
    if (Platform.isAndroid) {
      try {
        final picked = await MobileDocumentPicker.pickFile();
        return picked?.replaceAll('\\', '/');
      } catch (error) {
        debugPrint('[GeoScatter3D mobile pickFile error]: $error');
        return null;
      }
    }
    return pickFile(filterType);
  }

  /// 移动端通过 Android SAF 选择 Bundle 目录；桌面端复用 C-ABI 目录选择器。
  Future<String?> pickFolderAsync() async {
    if (folderPickerOverride != null) return pickFolder();
    if (Platform.isAndroid) {
      try {
        final picked = await MobileDocumentPicker.pickDirectory();
        return picked?.replaceAll('\\', '/');
      } catch (error) {
        debugPrint('[GeoScatter3D mobile pickFolder error]: $error');
        return null;
      }
    }
    return pickFolder();
  }

  /// 原生调用系统文件选择对话框（支持 Windows 原生 Explorer 文件选择）
  String? pickFile([String filterType = 'point_cloud']) {
    if (filePickerOverride != null) {
      final res = filePickerOverride!(filterType);
      return res?.replaceAll('\\', '/');
    }
    if (!_initialized || _bindings == null) {
      if (!initialize()) return null;
    }
    if (_bindings != null) {
      final filterPtr = filterType.toNativeUtf8();
      try {
        final resPtr = _bindings!.pick_file(filterPtr);
        final res = resPtr.toDartString();
        if (res.isNotEmpty) {
          return res.replaceAll('\\', '/');
        }
      } catch (e) {
        debugPrint('[FFI pickFile error]: $e');
      } finally {
        calloc.free(filterPtr);
      }
    }
    return null;
  }

  /// 原生调用系统目录/文件夹选择对话框（支持 Windows 原生 Explorer 文件夹选择）
  String? pickFolder() {
    if (folderPickerOverride != null) {
      final res = folderPickerOverride!();
      return res?.replaceAll('\\', '/');
    }
    if (!_initialized || _bindings == null) {
      if (!initialize()) return null;
    }
    if (_bindings != null) {
      try {
        final resPtr = _bindings!.pick_folder();
        final res = resPtr.toDartString();
        if (res.isNotEmpty) {
          return res.replaceAll('\\', '/');
        }
      } catch (e) {
        debugPrint('[FFI pickFolder error]: $e');
      }
    }
    return null;
  }

  /// 新建工程：针对 CSV/DAT 原始数据源进行离线构建并载入工作台
  Map<String, dynamic> buildAndLoadProject({
    required String path,
    String? name,
    int threads = 8,
    String voxelMode = 'xyz',
    int maxPointsPerTile = 50000,
  }) {
    final trimmed = path.trim();
    if (trimmed.isEmpty) {
      return {'success': false, 'message': '缺少有效的数据文件路径'};
    }
    final resolvedSource = resolveDatasetPath(trimmed);
    if (resolvedSource == null) {
      return {'success': false, 'message': '指定数据源文件不存在: $path'};
    }

    final lower = resolvedSource.toLowerCase();
    final isCsvOrDat = lower.endsWith('.csv') || lower.endsWith('.dat');

    // 如果已经是二进制 GS3D 或 Bundle 目录，直接载入
    if (!isCsvOrDat) {
      final ok = loadDataset(resolvedSource);
      return {
        'success': ok,
        'message': ok ? '工程加载成功: $resolvedSource' : '工程加载失败，请核验文件格式',
        'data': ok ? _summary.toJson() : null,
      };
    }

    // 针对原始 CSV / DAT 进行预处理切片与工区构建
    final rawBaseName = File(resolvedSource).uri.pathSegments.last;
    final baseNameNoExt = rawBaseName.contains('.')
        ? rawBaseName.substring(0, rawBaseName.lastIndexOf('.'))
        : rawBaseName;
    final projName = (name != null && name.trim().isNotEmpty)
        ? name.trim()
        : baseNameNoExt;
    final repoRoot = resolveRepoRoot();
    final targetBundleDir = '$repoRoot/data/$projName.gs3d.bundle';

    final preprocessExe = resolvePreprocessExecutable();
    final configPath =
        resolveDatasetPath('config/sample-viewer.toml') ??
        '$repoRoot/config/sample-viewer.toml';

    if (preprocessExe != null && File(preprocessExe).existsSync()) {
      try {
        final result = Process.runSync(preprocessExe, [
          '--config',
          configPath,
          lower.endsWith('.dat') ? '--dat' : '--csv',
          resolvedSource,
          '--bundle',
          targetBundleDir,
        ], workingDirectory: repoRoot);
        if (result.exitCode != 0) {
          final errDetail = result.stderr.toString().trim();
          debugPrint(
            '[Preprocess Warning] Process exit ${result.exitCode}: $errDetail',
          );
          return {
            'success': false,
            'message':
                '数据预处理构建失败: ${errDetail.isNotEmpty ? errDetail : "退出码 ${result.exitCode}"}',
          };
        }
      } catch (e) {
        debugPrint('[Preprocess Error] $e');
        return {'success': false, 'message': '启动预处理构建器失败: $e'};
      }
    } else {
      return {
        'success': false,
        'message': '未找到预处理器可执行文件 GeoScatter3DPreprocess.exe',
      };
    }

    String? bundleToLoad = resolveDatasetPath(targetBundleDir);
    if (bundleToLoad != null && Directory(bundleToLoad).existsSync()) {
      final ok = loadDataset(bundleToLoad);
      return {
        'success': ok,
        'message': ok ? '工程创建并载入成功: $bundleToLoad' : '工区构建后载入失败',
        'data': ok ? _summary.toJson() : null,
      };
    }

    return {'success': false, 'message': '工程构建失败，未能生成工区包目录: $targetBundleDir'};
  }

  /// 兼容调用入口；欢迎页构建统一经过异步动作调度。
  Future<Map<String, dynamic>> buildAndLoadProjectAsync({
    required String path,
    String? name,
    int threads = 8,
    String voxelMode = 'xyz',
    int finestTargetPoints = 50000,
    void Function(PreprocessProgressInfo progress)? onProgress,
  }) async {
    void changed() {
      if (_latestProgress != null) onProgress?.call(_latestProgress!);
    }

    addListener(changed);
    try {
      return await executeActionAsync('welcome.new_project', {
        'path': path,
        'name': name,
        'threads': threads,
        'voxel_mode': voxelMode,
        'finest_target_points': finestTargetPoints,
      });
    } finally {
      removeListener(changed);
    }
  }

  /// 加载数据集文件（.gs3d、.gs3d.bundle 等）
  bool loadDataset(String path, {bool activate = true}) {
    if (!_initialized || _bindings == null) {
      if (!initialize()) return false;
    }

    final resolved = resolveDatasetPath(path) ?? path;
    final nativePath = resolved.toNativeUtf8();
    try {
      final code = _bindings!.load_dataset(nativePath);
      if (code == 0) {
        final wasNativeRendererActive = isNativeRendererActive;
        _loadedDatasetPath = resolved;
        _updateSummary();
        _updatePoints();
        _refreshRecentProjects();
        _isWorkbenchActive = activate;
        try {
          _bindings!.remember_recent_project(nativePath);
          _refreshRecentProjects();
        } catch (_) {}
        unawaited(refreshMinimap());
        notifyListeners();
        if (activate &&
            (wasNativeRendererActive ||
                (Platform.isWindows &&
                    !Platform.environment.containsKey('FLUTTER_TEST')))) {
          unawaited(_restartNativeRendererForLoadedDataset());
        }
        return true;
      }
      return false;
    } finally {
      calloc.free(nativePath);
    }
  }

  Future<void> _restartNativeRendererForLoadedDataset() async {
    await stopNativeRenderer();
    await startNativeRenderer();
  }

  /// CSV 预处理（编译入 .so，供移动端免外部可执行文件）。
  int preprocessCsv(
    String csvPath,
    String outputPath, {
    int threads = 2,
    int finestTargetPoints = 50000,
  }) {
    if (_bindings == null) return -99;
    return _bindings!.preprocessCsv(
      csvPath,
      outputPath,
      threads: threads,
      finestTargetPoints: finestTargetPoints,
    );
  }

  String get lastPreprocessError =>
      _bindings?.lastPreprocessError ?? 'FFI 未初始化';

  void _updateSummary() {
    if (_bindings == null) return;

    final isLoaded = _bindings!.is_dataset_loaded() == 1;
    final name = _bindings!.get_dataset_name().toDartString();
    final pointCount = _bindings!.get_point_count();
    final fileSize = _bindings!.get_file_size_string().toDartString();

    final lodEnabled = _bindings!.get_lod_enabled() == 1;
    final lodCount = _bindings!.get_lod_level_count();
    final lodDetails = <String>[];
    for (int i = 0; i < lodCount; i++) {
      lodDetails.add(_bindings!.get_lod_level_detail(i).toDartString());
    }

    final attrCount = _bindings!.get_attribute_count();
    final attributes = <String>[];
    for (int i = 0; i < attrCount; i++) {
      attributes.add(_bindings!.get_attribute_name(i).toDartString());
    }

    final bboxMinPtr = calloc<ffi.Float>(3);
    final bboxMaxPtr = calloc<ffi.Float>(3);
    final valRangePtr = calloc<ffi.Float>(2);
    try {
      _bindings!.get_bounding_box(bboxMinPtr, bboxMaxPtr);
      _bindings!.get_value_range(valRangePtr, valRangePtr + 1);

      _summary = DatasetSummary(
        isLoaded: isLoaded,
        name: name,
        pointCount: pointCount,
        fileSize: fileSize,
        lodEnabled: lodEnabled,
        lodDetails: lodDetails,
        attributes: attributes,
        bboxMin: [bboxMinPtr[0], bboxMinPtr[1], bboxMinPtr[2]],
        bboxMax: [bboxMaxPtr[0], bboxMaxPtr[1], bboxMaxPtr[2]],
        valueRange: [valRangePtr[0], valRangePtr[1]],
      );
    } finally {
      calloc.free(bboxMinPtr);
      calloc.free(bboxMaxPtr);
      calloc.free(valRangePtr);
    }
  }

  void _updatePoints() {
    if (_bindings == null) return;
    const maxPts = 65536;
    final buf = calloc<ffi.Float>(maxPts * 4);
    try {
      final count = _bindings!.get_points(buf, maxPts);
      final pts = <Point3D>[];
      for (int i = 0; i < count; i++) {
        final offset = i * 4;
        pts.add(
          Point3D(
            x: buf[offset],
            y: buf[offset + 1],
            z: buf[offset + 2],
            value: buf[offset + 3],
          ),
        );
      }
      _points = pts;
    } finally {
      calloc.free(buf);
    }

    _pointSize = _bindings!.get_point_size();
    final cmPtr = _bindings!.get_colormap();
    _colormap = cmPtr.toDartString();

    final minPtr = calloc<ffi.Float>();
    final maxPtr = calloc<ffi.Float>();
    try {
      _bindings!.get_scalar_range(minPtr, maxPtr);
      _scalarMin = minPtr.value;
      _scalarMax = maxPtr.value;
    } finally {
      calloc.free(minPtr);
      calloc.free(maxPtr);
    }
  }

  void _refreshRecentProjects() {
    if (_bindings == null) return;
    final count = _bindings!.get_recent_project_count();
    final list = <RecentProjectItem>[];
    for (int i = 0; i < count; i++) {
      final p = _bindings!.get_recent_project_path(i).toDartString();
      final ts = _bindings!.get_recent_project_timestamp(i);
      list.add(
        RecentProjectItem(
          path: p,
          timestampUnix: ts,
          exists: File(p).existsSync() || Directory(p).existsSync(),
        ),
      );
    }
    _recentProjects = list;
  }

  void _refreshGpus() {
    if (_bindings == null) return;
    final count = _bindings!.get_gpu_count();
    final list = <GpuDeviceInfo>[];
    for (int i = 0; i < count; i++) {
      final name = _bindings!.get_gpu_name(i).toDartString();
      final type = _bindings!.get_gpu_type(i).toDartString();
      list.add(
        GpuDeviceInfo(
          index: i,
          name: name,
          typeDescription: type,
          isDiscrete: type.contains('独立'),
        ),
      );
    }
    _gpus = list;
    _activeGpuIndex = _bindings!.get_active_gpu_index();
  }

  bool setPreferredGpu(int index) {
    if (_bindings == null || index < 0 || index >= _gpus.length) return false;
    _bindings!.set_preferred_gpu(index);
    _activeGpuIndex = _bindings!.get_active_gpu_index();
    notifyListeners();
    return _activeGpuIndex == index;
  }

  void clearRecentProjects() {
    if (_bindings == null) return;
    _bindings!.clear_recent_projects();
    _recentProjects.clear();
    notifyListeners();
  }

  /// 获取所有可用 UI 动作元数据清单（供 AI、MCP 工具及自动化测试调用）
  List<UiActionDescriptor> getAvailableActions() {
    return const [
      UiActionDescriptor(
        id: 'welcome.quick_demo',
        name: '快速体验示例',
        description:
            '自动解析并载入内置 25 测点 5 级八叉树 LOD 示例点云数据包 (sample-points.gs3d.bundle) 并进入三维工作台',
        category: 'welcome',
      ),
      UiActionDescriptor(
        id: 'welcome.open_project',
        name: '打开指定工程',
        description: '打开指定路径的 .gs3d 二进制文件或 .gs3d.bundle 空间工区包',
        category: 'welcome',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'path': {'type': 'string', 'description': '数据集绝对路径或相对工程根目录路径'},
          },
          'required': ['path'],
        },
      ),
      UiActionDescriptor(
        id: 'welcome.new_project',
        name: '新建散点工程',
        description: '从指定原始 CSV/DAT/GS3D 数据源创建并加载新工程',
        category: 'welcome',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'path': {'type': 'string', 'description': '数据源文件路径'},
            'name': {'type': 'string', 'description': '工程名称 (可选)'},
            'threads': {
              'type': 'integer',
              'minimum': 1,
              'maximum': 256,
              'description': '预处理线程数 (默认 8)',
            },
            'voxel_mode': {
              'type': 'string',
              'enum': ['xy', 'xyz'],
              'description': 'LOD 体素模式',
            },
            'finest_target_points': {
              'type': 'integer',
              'minimum': 1,
              'description': '最细 LOD 层目标点数',
            },
          },
          'required': ['path'],
        },
      ),
      UiActionDescriptor(
        id: 'welcome.recent.open',
        name: '打开最近工程',
        description: '根据索引或路径打开历史工程记录中的项目',
        category: 'welcome',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'index': {'type': 'integer', 'description': '最近列表项索引 (从 0 开始)'},
            'path': {'type': 'string', 'description': '或者直接指定项目路径'},
          },
        },
      ),
      UiActionDescriptor(
        id: 'welcome.recent.clear',
        name: '清空最近工程记录',
        description: '清空所有最近打开的项目历史记录',
        category: 'welcome',
      ),
      UiActionDescriptor(
        id: 'welcome.gpu.set_preferred',
        name: '设置首选渲染 GPU',
        description: '选择并切换 Vulkan 物理显卡硬件设备',
        category: 'welcome',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'index': {'type': 'integer', 'description': '显卡设备索引 (0 为独立显卡)'},
          },
          'required': ['index'],
        },
      ),
      UiActionDescriptor(
        id: 'welcome.gpu.get_list',
        name: '获取 GPU 设备列表',
        description: '获取系统所有已探测到的 Vulkan 显卡设备及当前选用状态',
        category: 'welcome',
      ),
      UiActionDescriptor(
        id: 'welcome.get_state',
        name: '获取欢迎页数据状态快照',
        description: '获取欢迎页的完整状态快照（最近工程列表、当前选用 GPU、引擎版本及工区激活状态）',
        category: 'welcome',
      ),
      UiActionDescriptor(
        id: 'welcome.about.get_info',
        name: '获取系统关于信息',
        description: '获取 GeoScatter3D 版本、架构特性与核心能力说明',
        category: 'welcome',
      ),
      UiActionDescriptor(
        id: 'welcome.docs.get_info',
        name: '获取操作指南与格式标准',
        description: '获取 GS3D v2 显式小端标准、八叉树 LOD 与交互控制指南',
        category: 'welcome',
      ),
      UiActionDescriptor(
        id: 'welcome.browse_file',
        name: '浏览选择本地数据文件',
        description: '呼出原生系统文件选择对话框，选择 CSV/DAT 点云或 GS3D 工程文件',
        category: 'welcome',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'type': {
              'type': 'string',
              'description': '过滤类型: point_cloud, csv 或 open_project',
            },
          },
        },
      ),
      UiActionDescriptor(
        id: 'welcome.browse_folder',
        name: '浏览选择本地工区目录/文件夹',
        description: '呼出原生系统文件夹选择对话框，选择 .gs3d.bundle 金字塔多分辨率工区目录',
        category: 'welcome',
      ),
      UiActionDescriptor(
        id: 'welcome.recent.remove',
        name: '移除单项最近工程',
        description: '从最近打开记录中移除指定的历史项目',
        category: 'welcome',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'index': {'type': 'integer', 'description': '要移除的项目索引'},
            'path': {'type': 'string', 'description': '要移除的项目绝对或相对路径'},
          },
        },
      ),
      UiActionDescriptor(
        id: 'welcome.drop_file',
        name: '拖入文件或工区目录',
        description: '模拟或触发原生桌面拖入文件/工区包至欢迎页',
        category: 'welcome',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'path': {'type': 'string', 'description': '拖入的文件或目录路径'},
          },
          'required': ['path'],
        },
      ),
      UiActionDescriptor(
        id: 'welcome.preprocess.cancel',
        name: '取消预处理构建任务',
        description: '中止正在执行中的离线预处理切片构建任务',
        category: 'welcome',
      ),

      // ======================================================================
      // 工作台动作 (Workbench Actions)
      // ======================================================================
      UiActionDescriptor(
        id: 'workbench.camera.reset',
        name: '重置相机视角',
        description: '将三维视口相机复位到默认等轴测观察姿态 (方位角 -45°, 仰角 30°, 缩放 1.0x)',
        category: 'workbench',
      ),
      UiActionDescriptor(
        id: 'workbench.camera.set_view',
        name: '设置相机姿态与视角',
        description: '调整相机方位角、仰角、缩放、平移偏移或应用预设视角 (top, front, side, iso)',
        category: 'workbench',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'preset': {
              'type': 'string',
              'enum': ['top', 'front', 'side', 'iso'],
              'description': '预设视角名称',
            },
            'azimuth': {'type': 'number', 'description': '水平方位角 (度)'},
            'elevation': {'type': 'number', 'description': '垂直仰角 (-89° ~ 89°)'},
            'zoom': {'type': 'number', 'description': '视口缩放倍率 (0.05 ~ 50.0)'},
            'pan_x': {'type': 'number', 'description': '水平平移像素'},
            'pan_y': {'type': 'number', 'description': '垂直平移像素'},
          },
        },
      ),
      UiActionDescriptor(
        id: 'workbench.camera.get',
        name: '获取当前相机姿态',
        description: '查询当前视口相机的方位角、仰角、缩放及平移偏移参数',
        category: 'workbench',
      ),
      UiActionDescriptor(
        id: 'workbench.render.set_point_size',
        name: '设置点云绘制大小',
        description: '调节三维视口中散点绘制的像素半径 (0.5 ~ 8.0)',
        category: 'workbench',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'size': {'type': 'number', 'description': '点大小像素值 (0.5 ~ 8.0)'},
          },
          'required': ['size'],
        },
      ),
      UiActionDescriptor(
        id: 'workbench.render.set_colormap',
        name: '设置色标配色方案',
        description: '切换点云属性标量伪彩色带 (Viridis, Plasma, Turbo, Jet, Coolwarm)',
        category: 'workbench',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'colormap': {'type': 'string', 'description': '色标方案名称'},
          },
          'required': ['colormap'],
        },
      ),
      UiActionDescriptor(
        id: 'workbench.render.set_color_attribute',
        name: '设置着色属性字段',
        description: '切换当前点云渲染使用的属性维度字段',
        category: 'workbench',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'attribute': {'type': 'string', 'description': '着色属性名称'},
          },
          'required': ['attribute'],
        },
      ),
      UiActionDescriptor(
        id: 'workbench.render.set_shape',
        name: '设置散点图元形状',
        description: '切换点渲染图元形状 (方形 / 圆形)',
        category: 'workbench',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'shape': {
              'type': 'string',
              'enum': ['方形', '圆形'],
              'description': '图元形状',
            },
          },
          'required': ['shape'],
        },
      ),
      UiActionDescriptor(
        id: 'workbench.render.set_height_scale',
        name: '设置高度缩放起伏倍率',
        description: '动态调节地质高程 Z 轴的垂直拉伸倍率 (0.1x ~ 10.0x)',
        category: 'workbench',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'scale': {'type': 'number', 'description': '缩放倍率'},
          },
          'required': ['scale'],
        },
      ),
      UiActionDescriptor(
        id: 'workbench.render.set_scalar_range',
        name: '设置标量显示过滤区间',
        description: '设置标量过滤范围裁剪的下限与上限',
        category: 'workbench',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'min': {'type': 'number', 'description': '标量极小值'},
            'max': {'type': 'number', 'description': '标量极大值'},
          },
          'required': ['min', 'max'],
        },
      ),
      UiActionDescriptor(
        id: 'workbench.render.reset_scalar_range',
        name: '自动自适应标量极值范围',
        description: '根据当前工程数据集的真实验收范围自动复位标量裁剪区间',
        category: 'workbench',
      ),
      UiActionDescriptor(
        id: 'workbench.render.set_background',
        name: '设置视口背景颜色',
        description: '切换视口三维底色 (dark: 暗黑深空, light: 浅灰工程, black: 纯黑)',
        category: 'workbench',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'theme': {
              'type': 'string',
              'enum': ['dark', 'light', 'black'],
              'description': '视口背景主题',
            },
          },
          'required': ['theme'],
        },
      ),
      UiActionDescriptor(
        id: 'workbench.layout.set',
        name: '切换工作台布局模式',
        description:
            '切换主工作台界面布局 (standard: 标准三栏, floatingDock: 悬浮胶囊Dock, analysisRail: 暗色分析舱)',
        category: 'workbench',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'mode': {
              'type': 'string',
              'enum': ['standard', 'floatingDock', 'analysisRail'],
              'description': '布局模式标识',
            },
          },
          'required': ['mode'],
        },
      ),
      UiActionDescriptor(
        id: 'workbench.dock.toggle',
        name: '切换停靠面板显示状态',
        description: '控制左侧数据抽屉或右侧渲染面板的展开与折叠',
        category: 'workbench',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'panel': {
              'type': 'string',
              'enum': ['left', 'right', 'both'],
              'description': '面板名称',
            },
            'visible': {'type': 'boolean', 'description': '显隐状态 (可选，默认反转)'},
          },
          'required': ['panel'],
        },
      ),
      UiActionDescriptor(
        id: 'workbench.viewport.set_overlays',
        name: '控制视口图层叠加开关',
        description: '开启或关闭地图轴、世界坐标轴、视口十字准线等叠加元素',
        category: 'workbench',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'map_axis': {'type': 'boolean', 'description': '地图轴开关'},
            'world_axis': {'type': 'boolean', 'description': '世界坐标轴开关'},
            'crosshair': {'type': 'boolean', 'description': '十字准线开关'},
          },
        },
      ),
      UiActionDescriptor(
        id: 'workbench.measure.calculate',
        name: '空间几何测量计算',
        description: '计算当前工区的空间距离、投影底面积、地层走向与倾向指标',
        category: 'workbench',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'type': {
              'type': 'string',
              'enum': ['distance', 'area', 'strike_dip'],
              'description': '测量类型',
            },
          },
        },
      ),
      UiActionDescriptor(
        id: 'workbench.stats.get',
        name: '获取属性统计分布指标',
        description: '计算当前点云的有效样本数、均值、极值、极差与直方图统计指标',
        category: 'workbench',
      ),
      UiActionDescriptor(
        id: 'workbench.screenshot',
        name: '截取三维视口并导出图片',
        description: '触发工作台主三维视口的帧画面截屏与保存',
        category: 'workbench',
      ),
      UiActionDescriptor(
        id: 'workbench.pick.point',
        name: '三维点拾取',
        description: '在三维视口屏幕坐标处拾取最近的数据点',
        category: 'workbench',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'screen_x': {'type': 'number', 'description': '屏幕 X 坐标'},
            'screen_y': {'type': 'number', 'description': '屏幕 Y 坐标'},
            'viewport_width': {'type': 'number', 'description': '视口宽度'},
            'viewport_height': {'type': 'number', 'description': '视口高度'},
            'tolerance': {'type': 'number', 'description': '容差像素 (默认 25.0)'},
          },
          'required': [
            'screen_x',
            'screen_y',
            'viewport_width',
            'viewport_height',
          ],
        },
      ),
      UiActionDescriptor(
        id: 'workbench.measure.mode',
        name: '切换测距标尺模式',
        description: '启用或关闭视口两点/多点测距标尺与走向测量',
        category: 'workbench',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'enabled': {'type': 'boolean', 'description': '是否开启标尺模式 (默认切换反转)'},
          },
        },
      ),
      UiActionDescriptor(
        id: 'workbench.measure.add_point',
        name: '添加测量参考点',
        description: '向测量序列追加空间参考坐标点',
        category: 'workbench',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'x': {'type': 'number', 'description': '空间 X 坐标'},
            'y': {'type': 'number', 'description': '空间 Y 坐标'},
            'z': {'type': 'number', 'description': '空间 Z 坐标'},
            'value': {'type': 'number', 'description': '标量属性值'},
          },
          'required': ['x', 'y', 'z'],
        },
      ),
      UiActionDescriptor(
        id: 'workbench.measure.clear',
        name: '清空测量线段',
        description: '清除所有已记录的三维几何测量线段',
        category: 'workbench',
      ),
      UiActionDescriptor(
        id: 'workbench.stats.box',
        name: '视口矩形选区属性统计',
        description: '框选屏幕矩形区域统计点云属性均值、极值、方差及直方图',
        category: 'workbench',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'min_x': {'type': 'number', 'description': '选区左边界像素'},
            'min_y': {'type': 'number', 'description': '选区上边界像素'},
            'max_x': {'type': 'number', 'description': '选区右边界像素'},
            'max_y': {'type': 'number', 'description': '选区下边界像素'},
            'viewport_width': {'type': 'number', 'description': '视口宽度'},
            'viewport_height': {'type': 'number', 'description': '视口高度'},
          },
          'required': [
            'min_x',
            'min_y',
            'max_x',
            'max_y',
            'viewport_width',
            'viewport_height',
          ],
        },
      ),
      UiActionDescriptor(
        id: 'workbench.minimap.refresh',
        name: '刷新二维俯视小地图',
        description: '重新渲染当前工区二维平面投影缩略图',
        category: 'workbench',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'width': {'type': 'integer', 'description': '缩略图宽度 (默认 200)'},
            'height': {'type': 'integer', 'description': '缩略图高度 (默认 160)'},
          },
        },
      ),
      UiActionDescriptor(
        id: 'workbench.minimap.navigate',
        name: '小地图跳转导航',
        description: '根据二维小地图点击坐标调整三维视口中心至该物理坐标',
        category: 'workbench',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'world_x': {'type': 'number', 'description': '目标工区物理 X 坐标'},
            'world_y': {'type': 'number', 'description': '目标工区物理 Y 坐标'},
          },
          'required': ['world_x', 'world_y'],
        },
      ),
      UiActionDescriptor(
        id: 'workbench.overlay.command_palette',
        name: '切换全局命令面板',
        description: '打开或关闭 Ctrl+P 快捷搜索与命令浮窗',
        category: 'workbench',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'open': {'type': 'boolean', 'description': '显隐状态 (默认反转)'},
          },
        },
      ),
      UiActionDescriptor(
        id: 'workbench.overlay.shortcut_help',
        name: '切换快捷键速查面板',
        description: '打开或关闭 F1 全局按键映射与视口操作速查浮窗',
        category: 'workbench',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'open': {'type': 'boolean', 'description': '显隐状态 (默认反转)'},
          },
        },
      ),
      UiActionDescriptor(
        id: 'workbench.overlay.performance',
        name: '切换性能诊断面板',
        description: '打开或关闭实时 FPS、帧耗时、显存占用与瓦片统计窗口',
        category: 'workbench',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'open': {'type': 'boolean', 'description': '显隐状态 (默认反转)'},
          },
        },
      ),
      UiActionDescriptor(
        id: 'workbench.overlay.tile_inspector',
        name: '切换瓦片与LOD检查器',
        description: '打开或关闭八叉树分级结构与瓦片流式加载诊断对话框',
        category: 'workbench',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'open': {'type': 'boolean', 'description': '显隐状态 (默认反转)'},
          },
        },
      ),
      UiActionDescriptor(
        id: 'workbench.overlay.render_settings',
        name: '渲染外观设置',
        description: '唤起或关闭散点核心渲染设置悬浮卡片 (点大小/色标/形状/高度缩放/底色)',
        category: 'workbench',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'open': {'type': 'boolean', 'description': '显隐状态 (默认反转)'},
          },
        },
      ),
      UiActionDescriptor(
        id: 'workbench.export',
        name: '导出点云数据',
        description: '将当前点云或过滤结果导出为 PLY 或 CSV 格式',
        category: 'workbench',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'path': {'type': 'string', 'description': '目标文件导出路径'},
            'format': {
              'type': 'string',
              'enum': ['ply', 'csv'],
              'description': '导出格式 (默认 ply)',
            },
          },
          'required': ['path'],
        },
      ),
      UiActionDescriptor(
        id: 'workbench.cache.clear',
        name: '清空显存与瓦片缓存',
        description: '释放当前已加载的瓦片内存并重置缓存命中率',
        category: 'workbench',
      ),
      UiActionDescriptor(
        id: 'workbench.theme.set',
        name: '切换界面主题',
        description: '切换 UI 主题（0-4，对齐原生 menu.view.theme）',
        category: 'workbench',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'index': {'type': 'integer', 'minimum': 0, 'maximum': 4},
          },
        },
      ),
      UiActionDescriptor(
        id: 'workbench.theme.cycle',
        name: '循环切换主题',
        description: '按顺序切换到下一主题',
        category: 'workbench',
      ),
      UiActionDescriptor(
        id: 'workbench.camera.link',
        name: '切换相机联动',
        description: '开启或关闭多视口相机联动 (toolbar.link_camera)',
        category: 'workbench',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'linked': {'type': 'boolean', 'description': '联动开关 (默认反转)'},
          },
        },
      ),
      UiActionDescriptor(
        id: 'workbench.view.new',
        name: '新建原生视图',
        description: '请求原生 Viewer 新建视口 (menu.view.new_view)',
        category: 'workbench',
      ),
      UiActionDescriptor(
        id: 'workbench.workspace.new_window',
        name: '新建工作窗口',
        description: '请求原生 Viewer 新建工作区窗口 (menu.window.new_workspace)',
        category: 'workbench',
      ),
      UiActionDescriptor(
        id: 'workbench.workspace.restore',
        name: '恢复默认工作区',
        description: '重置布局、停靠面板与 overlay 到默认状态',
        category: 'workbench',
      ),
      UiActionDescriptor(
        id: 'workbench.panel.toggle',
        name: '切换原生面板显隐',
        description: '切换 ComponentRegistry 中 8 张原生面板的显隐状态',
        category: 'workbench',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'panel': {
              'type': 'string',
              'description':
                  '面板 ID: panel.dataset / render_settings / performance / navigation_map / measurement / region_stats / tile_inspector / lod_view',
            },
            'visible': {'type': 'boolean'},
          },
          'required': ['panel'],
        },
      ),
      UiActionDescriptor(
        id: 'workbench.file.open_native',
        name: '原生打开数据文件',
        description: '触发原生 menu.file.open 系统文件对话框',
        category: 'workbench',
      ),
      UiActionDescriptor(
        id: 'workbench.file.open_bundle_native',
        name: '原生打开工区包',
        description: '触发原生 menu.file.open_bundle 系统目录对话框',
        category: 'workbench',
      ),
      UiActionDescriptor(
        id: 'workbench.screenshot.native',
        name: '原生截图保存',
        description: '触发原生 menu.file.screenshot 显存截图保存流程',
        category: 'workbench',
      ),
      UiActionDescriptor(
        id: 'workbench.diagnostics.refresh',
        name: '刷新原生诊断状态',
        description: '从 status.bar 拉取 FPS/瓦片/显存/相机坐标等实时指标',
        category: 'workbench',
      ),
      UiActionDescriptor(
        id: 'workbench.measure.display_mode',
        name: '设置测量标签显示模式',
        description: '设置测量线标签显示为 3d / 2d / both',
        category: 'workbench',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'mode': {
              'type': 'string',
              'enum': ['3d', '2d', 'both'],
            },
          },
          'required': ['mode'],
        },
      ),
      UiActionDescriptor(
        id: 'workbench.measure.remove',
        name: '删除测量线',
        description: '按索引删除指定测量线段',
        category: 'workbench',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'index': {'type': 'integer', 'minimum': 0},
            'id': {'type': 'string'},
          },
        },
      ),
      UiActionDescriptor(
        id: 'workbench.measure.toggle_fixed',
        name: '固定/解除测量线',
        description: '切换指定测量线的图钉固定状态',
        category: 'workbench',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'index': {'type': 'integer', 'minimum': 0},
            'id': {'type': 'string'},
          },
        },
      ),
      UiActionDescriptor(
        id: 'workbench.camera.focus_point',
        name: '旋转中心对焦',
        description: '将相机旋转中心对焦到指定三维物理点',
        category: 'workbench',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'x': {'type': 'number'},
            'y': {'type': 'number'},
            'z': {'type': 'number'},
            'value': {'type': 'number'},
          },
          'required': ['x', 'y', 'z'],
        },
      ),
      UiActionDescriptor(
        id: 'workbench.gizmo.axis',
        name: '导航球预设视角',
        description: '通过 gizmo.navigation 切换 +X/-X/+Y/-Y/+Z/-Z 预设视角',
        category: 'workbench',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'axis': {
              'type': 'integer',
              'minimum': 0,
              'maximum': 5,
              'description': '0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z',
            },
          },
          'required': ['axis'],
        },
      ),
    ];
  }

  /// 执行高层 UI 动作（统一供 AI、MCP 工具、控制面及前台按钮调用）
  Map<String, dynamic> executeAction(
    String actionId, [
    Map<String, dynamic>? params,
  ]) {
    final p = params ?? const {};
    // R3 前置 guard：引擎不可用时，引擎依赖类动作保持可达但显式报错
    // （不静默失败、不置灰隐藏，与「功能缺失」显式区分）。
    if (_engineAvailability == EngineAvailability.unavailable &&
        _isEngineDependentAction(actionId)) {
      return {
        'success': false,
        'message':
            '${GeoScatter3dService.engineUnavailableMessage}'
            ' ($_initializationError)',
        'engine_available': false,
      };
    }
    switch (actionId) {
      case 'welcome.quick_demo':
        final resolved = resolveDatasetPath('data/sample-points.gs3d.bundle');
        if (resolved == null) {
          return {
            'success': false,
            'message': '未在当前环境找到 sample-points.gs3d.bundle 示例数据包',
          };
        }
        final ok = loadDataset(resolved);
        return {
          'success': ok,
          'message': ok ? '成功加载示例工区并进入工作台' : '加载示例数据失败',
          'data': ok ? _summary.toJson() : null,
        };

      case 'welcome.browse_file':
        final type = (p['type'] as String?) ?? 'point_cloud';
        final picked = pickFile(type);
        return {
          'success': picked != null && picked.isNotEmpty,
          'message': (picked != null && picked.isNotEmpty)
              ? '已选择文件: $picked'
              : '已取消或未选择文件',
          'data': {'path': picked},
        };

      case 'welcome.browse_folder':
        final picked = pickFolder();
        return {
          'success': picked != null && picked.isNotEmpty,
          'message': (picked != null && picked.isNotEmpty)
              ? '已选择目录: $picked'
              : '已取消或未选择目录',
          'data': {'path': picked},
        };

      case 'welcome.open_project':
        final path = p['path'] as String?;
        if (path == null || path.trim().isEmpty) {
          return {'success': false, 'message': '缺少必要参数: path'};
        }
        final resolved = resolveDatasetPath(path.trim());
        if (resolved == null) {
          return {'success': false, 'message': '指定的数据集路径不存在: $path'};
        }
        final lower = resolved.toLowerCase();
        if (lower.endsWith('.csv') || lower.endsWith('.dat')) {
          return buildAndLoadProject(path: resolved);
        }
        final ok = loadDataset(resolved);
        return {
          'success': ok,
          'message': ok ? '成功打开工程: $resolved' : '工程加载失败，请检查格式是否为 GS3D v2',
          'data': ok ? _summary.toJson() : null,
        };

      case 'welcome.new_project':
        final path = p['path'] as String?;
        if (path == null || path.trim().isEmpty) {
          return {'success': false, 'message': '缺少必要参数: path'};
        }
        return buildAndLoadProject(
          path: path,
          name: p['name'] as String?,
          threads: (p['threads'] as num?)?.toInt() ?? 8,
        );

      case 'welcome.recent.open':
        if (p.containsKey('index')) {
          final idx = p['index'] as int?;
          if (idx == null || idx < 0 || idx >= _recentProjects.length) {
            return {'success': false, 'message': '非法的最近项目索引: $idx'};
          }
          final path = _recentProjects[idx].path;
          final resolved = resolveDatasetPath(path);
          if (resolved == null) {
            return {'success': false, 'message': '该最近项目路径已失效: $path'};
          }
          final ok = loadDataset(resolved);
          return {
            'success': ok,
            'message': ok ? '成功打开最近项目: $resolved' : '加载失败',
            'data': ok ? _summary.toJson() : null,
          };
        } else if (p.containsKey('path')) {
          return executeAction('welcome.open_project', {'path': p['path']});
        }
        return {'success': false, 'message': '必须提供 index 或 path 参数'};

      case 'welcome.recent.clear':
        clearRecentProjects();
        return {'success': true, 'message': '已清空最近打开的项目记录'};

      case 'welcome.recent.remove':
        String? targetPath;
        if (p.containsKey('index')) {
          final idx = (p['index'] as num?)?.toInt();
          if (idx == null || idx < 0 || idx >= _recentProjects.length) {
            return {'success': false, 'message': '非法的最近项目索引: $idx'};
          }
          targetPath = _recentProjects[idx].path;
        } else if (p.containsKey('path')) {
          targetPath = p['path'] as String?;
        }
        if (targetPath == null || targetPath.isEmpty) {
          return {'success': false, 'message': '必须提供 index 或 path 参数'};
        }
        removeRecentProject(targetPath);
        return {
          'success': true,
          'message': '已成功移除历史项目记录: $targetPath',
          'data': {'remaining_count': _recentProjects.length},
        };

      case 'welcome.drop_file':
        final path = p['path'] as String? ?? '';
        if (path.isEmpty) {
          return {'success': false, 'message': '缺少待拖入的文件路径'};
        }
        handleDroppedPath(path);
        return {
          'success': true,
          'message': '成功处理拖入文件: $path',
          'data': {'path': path, 'is_workbench_active': _isWorkbenchActive},
        };

      case 'welcome.preprocess.cancel':
        cancelPreprocess();
        return {'success': true, 'message': '已取消当前构建任务'};

      case 'welcome.gpu.set_preferred':
        final idx = p['index'] as int?;
        if (idx == null || idx < 0 || idx >= _gpus.length) {
          return {'success': false, 'message': '非法的 GPU 索引: $idx'};
        }
        if (!setPreferredGpu(idx)) {
          return {'success': false, 'message': '无法保存显卡设置，请检查用户配置目录'};
        }
        return {
          'success': true,
          'message': '首选 GPU 已保存，下次打开工程时生效: ${_gpus[idx].name}',
          'data': {'active_gpu': _gpus[idx].toJson()},
        };

      case 'welcome.gpu.get_list':
        return {
          'success': true,
          'data': {
            'active_index': _activeGpuIndex,
            'gpus': _gpus.map((g) => g.toJson()).toList(),
          },
        };

      case 'welcome.get_state':
        return {
          'success': true,
          'data': {
            'is_initialized': _initialized,
            'is_workbench_active': _isWorkbenchActive,
            'engine_version': engineVersion,
            'recent_project_count': _recentProjects.length,
            'recent_projects': _recentProjects.map((r) => r.toJson()).toList(),
            'active_gpu':
                (_activeGpuIndex >= 0 && _activeGpuIndex < _gpus.length)
                ? _gpus[_activeGpuIndex].toJson()
                : null,
            'dataset_loaded': _summary.isLoaded,
          },
        };

      case 'welcome.about.get_info':
        return {
          'success': true,
          'data': {
            'app_name': 'GeoScatter3D',
            'version': engineVersion,
            'architecture': 'Flutter Desktop + C++20 / Vulkan 1.3 C-ABI FFI',
            'spec': 'GS3D v2 Little-Endian Explicit Format',
          },
        };

      case 'welcome.docs.get_info':
        return {
          'success': true,
          'data': {
            'supported_formats': ['.gs3d', '.gs3d.bundle', '.csv', '.dat'],
            'controls': {
              'orbit': '鼠标左键拖拽旋转',
              'pan': '鼠标右键/中键拖拽，或 Shift+左键',
              'zoom': '鼠标滚轮缩放',
              'reset': '顶部重置视角按钮',
            },
          },
        };

      // ======================================================================
      // 工作台动作实现 (Workbench Action Handlers)
      // ======================================================================
      case 'workbench.camera.reset':
        resetCamera();
        return {
          'success': true,
          'message': '视口相机已复位为默认等轴测姿态',
          'data': {
            'azimuth': _cameraAzimuth,
            'elevation': _cameraElevation,
            'zoom': _cameraZoom,
          },
        };

      case 'workbench.camera.set_view':
        setCameraView(
          preset: p['preset'] as String?,
          azimuth: (p['azimuth'] as num?)?.toDouble(),
          elevation: (p['elevation'] as num?)?.toDouble(),
          zoom: (p['zoom'] as num?)?.toDouble(),
          panX: (p['pan_x'] as num?)?.toDouble(),
          panY: (p['pan_y'] as num?)?.toDouble(),
        );
        return {
          'success': true,
          'message': '视口相机视角更新成功',
          'data': {
            'azimuth': _cameraAzimuth,
            'elevation': _cameraElevation,
            'zoom': _cameraZoom,
            'pan_x': _cameraPanX,
            'pan_y': _cameraPanY,
          },
        };

      case 'workbench.camera.set_panning':
        final panning = (p['panning'] as bool?) ?? true;
        setPanning(panning);
        return {
          'success': true,
          'message': '视口平移模式已${panning ? "开启" : "关闭"}',
          'data': {'is_panning': _isPanning},
        };

      case 'workbench.camera.get':
        return {
          'success': true,
          'data': {
            'azimuth': _cameraAzimuth,
            'elevation': _cameraElevation,
            'zoom': _cameraZoom,
            'pan_x': _cameraPanX,
            'pan_y': _cameraPanY,
            'is_panning': _isPanning,
          },
        };

      case 'workbench.render.set_point_size':
      case 'workbench.point_cloud.set_size':
        final size = (p['size'] as num?)?.toDouble();
        if (size == null) return {'success': false, 'message': '缺少必要参数: size'};
        setPointSize(size);
        return {
          'success': true,
          'message': '点大小已更新为: $_pointSize',
          'data': {'point_size': _pointSize},
        };

      case 'workbench.render.set_colormap':
      case 'workbench.point_cloud.set_colormap':
        final cm = p['colormap'] as String?;
        if (cm == null || cm.isEmpty) {
          return {'success': false, 'message': '缺少必要参数: colormap'};
        }
        setColormap(cm);
        return {
          'success': true,
          'message': '色标方案已切换为: $_colormap',
          'data': {'colormap': _colormap},
        };

      case 'workbench.render.set_color_attribute':
      case 'workbench.point_cloud.set_color_attribute':
        final attr = p['attribute'] as String?;
        if (attr == null || attr.isEmpty) {
          return {'success': false, 'message': '缺少必要参数: attribute'};
        }
        setColorAttribute(attr);
        return {
          'success': true,
          'message': '着色字段已切换为: $_colorAttribute',
          'data': {'attribute': _colorAttribute},
        };

      case 'workbench.render.set_shape':
      case 'workbench.point_cloud.set_shape':
        final shape = p['shape'] as String?;
        if (shape == null || shape.isEmpty) {
          return {'success': false, 'message': '缺少必要参数: shape'};
        }
        setPointShape(shape);
        return {
          'success': true,
          'message': '散点形状已切换为: $_pointShape',
          'data': {'shape': _pointShape},
        };

      case 'workbench.render.set_height_scale':
      case 'workbench.point_cloud.set_height_scale':
        final scale = (p['scale'] as num?)?.toDouble();
        if (scale == null) {
          return {'success': false, 'message': '缺少必要参数: scale'};
        }
        setHeightScale(scale);
        return {
          'success': true,
          'message': '高度缩放倍率已更新为: ${_heightScale.toStringAsFixed(2)}x',
          'data': {'height_scale': _heightScale},
        };

      case 'workbench.render.set_height_source':
      case 'workbench.point_cloud.set_height_source':
        final source = p['source'] as String?;
        if (source == null || source.isEmpty) {
          return {'success': false, 'message': '缺少必要参数: source'};
        }
        setHeightSource(source);
        return {
          'success': true,
          'message': '高度来源已切换为: $_heightSource',
          'data': {'height_source': _heightSource},
        };

      case 'workbench.render.set_scalar_range':
        final minVal = (p['min'] as num?)?.toDouble();
        final maxVal = (p['max'] as num?)?.toDouble();
        if (minVal == null || maxVal == null) {
          return {'success': false, 'message': '缺少必要参数: min, max'};
        }
        setScalarRange(minVal, maxVal);
        return {
          'success': true,
          'message': '标量过滤区间已更新为: [$_scalarMin, $_scalarMax]',
          'data': {'scalar_min': _scalarMin, 'scalar_max': _scalarMax},
        };

      case 'workbench.render.reset_scalar_range':
      case 'workbench.point_cloud.reset_scalar_range':
        resetScalarRange();
        return {
          'success': true,
          'message': '已将标量区间自适应复位为: [$_scalarMin, $_scalarMax]',
          'data': {'scalar_min': _scalarMin, 'scalar_max': _scalarMax},
        };

      case 'workbench.render.set_background':
      case 'workbench.viewport.set_bg_color':
        final theme = (p['theme'] as String?)?.toLowerCase();
        final colorStr = (p['color'] as String?)?.toLowerCase();
        Color bg = const Color(0xFF161A22);
        if (theme == 'light' ||
            colorStr == '#f0f2f5' ||
            colorStr == '#ffffff') {
          bg = const Color(0xFFF0F2F5);
        } else if (theme == 'black' ||
            colorStr == '#000000' ||
            colorStr == 'black') {
          bg = Colors.black;
        } else if (colorStr != null &&
            colorStr.startsWith('#') &&
            colorStr.length == 7) {
          final hex = int.tryParse(colorStr.substring(1), radix: 16);
          if (hex != null) {
            bg = Color(0xFF000000 | hex);
          }
        }
        setViewportBackgroundColor(bg);
        return {
          'success': true,
          'message': '视口背景主题已切换',
          'data': {'theme': theme, 'color': colorStr},
        };

      case 'workbench.layout.set':
        final modeStr = p['mode'] as String?;
        WorkbenchLayoutMode targetMode = WorkbenchLayoutMode.standard;
        if (modeStr == 'floatingDock') {
          targetMode = WorkbenchLayoutMode.floatingDock;
        } else if (modeStr == 'analysisRail') {
          targetMode = WorkbenchLayoutMode.analysisRail;
        }
        setLayoutMode(targetMode);
        return {
          'success': true,
          'message': '工作台布局已切换为: ${targetMode.displayName}',
          'data': {'layout_mode': targetMode.name},
        };

      case 'workbench.dock.toggle':
      case 'workbench.dock.toggle_left':
      case 'workbench.dock.toggle_right':
        final isLeft = actionId == 'workbench.dock.toggle_left';
        final isRight = actionId == 'workbench.dock.toggle_right';
        final panel = isLeft
            ? 'left'
            : (isRight
                  ? 'right'
                  : ((p['panel'] as String?)?.toLowerCase() ?? 'left'));
        final vis = p['visible'] as bool?;
        if (panel == 'left') {
          toggleLeftDock(vis);
        } else if (panel == 'right') {
          toggleRightDock(vis);
        } else {
          toggleLeftDock(vis);
          toggleRightDock(vis);
        }
        return {
          'success': true,
          'message': '停靠面板状态已更新',
          'data': {
            'left_dock_visible': _leftDockVisible,
            'right_dock_visible': _rightDockVisible,
          },
        };

      case 'workbench.viewport.set_overlays':
        setViewportOverlays(
          mapAxis: p['map_axis'] as bool?,
          worldAxis: p['world_axis'] as bool?,
          crosshair: p['crosshair'] as bool?,
        );
        return {
          'success': true,
          'message': '视口图层叠加开关已更新',
          'data': {
            'map_axis': _showMapAxis,
            'world_axis': _showWorldAxis,
            'crosshair': _showCrosshair,
          },
        };

      case 'workbench.measure.calculate':
        final type = (p['type'] as String?) ?? 'distance';
        final m = calculateMeasurement(type);
        return {'success': true, 'message': '空间几何指标推导完成', 'data': m};

      case 'workbench.stats.get':
      case 'workbench.stats.calculate':
        final stats = calculateStats();
        return {'success': true, 'message': '属性统计指标计算完成', 'data': stats};

      case 'workbench.screenshot':
      case 'workbench.viewport.screenshot':
        requestScreenshot();
        return {
          'success': true,
          'message': '已触发三维视口截图',
          'data': {
            'viewport_name': 'CenterViewport',
            'timestamp': DateTime.now().toIso8601String(),
          },
        };

      case 'workbench.pick.point':
        final sx = (p['screen_x'] as num?)?.toDouble() ?? 0.0;
        final sy = (p['screen_y'] as num?)?.toDouble() ?? 0.0;
        final vw = (p['viewport_width'] as num?)?.toDouble() ?? 800.0;
        final vh = (p['viewport_height'] as num?)?.toDouble() ?? 600.0;
        final tol = (p['tolerance'] as num?)?.toDouble() ?? 25.0;
        final picked = pickPointAt(sx, sy, vw, vh, tolerancePx: tol);
        if (picked != null) {
          setSelectedPoint(picked);
        }
        return {
          'success': picked != null,
          'message': picked != null ? '拾取成功' : '未在指定容差内命中点',
          'data': picked != null
              ? {
                  'x': picked.x,
                  'y': picked.y,
                  'z': picked.z,
                  'value': picked.value,
                }
              : null,
        };

      case 'workbench.measure.mode':
        if (p.containsKey('enabled')) {
          setMeasurementMode(p['enabled'] as bool);
        } else {
          toggleMeasurementMode();
        }
        return {
          'success': true,
          'message': '测量模式: ${_isMeasurementMode ? "已开启" : "已关闭"}',
          'data': {'is_measurement_mode': _isMeasurementMode},
        };

      case 'workbench.measure.add_point':
        final x = (p['x'] as num?)?.toDouble() ?? 0.0;
        final y = (p['y'] as num?)?.toDouble() ?? 0.0;
        final z = (p['z'] as num?)?.toDouble() ?? 0.0;
        final val = (p['value'] as num?)?.toDouble() ?? 0.0;
        final pt = Point3D(x: x, y: y, z: z, value: val);
        addMeasurementPoint(pt);
        return {
          'success': true,
          'message': '参考点已追加',
          'data': {
            'pending_start': _pendingMeasureStart != null,
            'lines_count': _measurementLines.length,
          },
        };

      case 'workbench.measure.clear':
        clearMeasurementLines();
        return {'success': true, 'message': '已清空测量线段'};

      case 'workbench.stats.box':
        final minX = (p['min_x'] as num?)?.toDouble() ?? 0.0;
        final minY = (p['min_y'] as num?)?.toDouble() ?? 0.0;
        final maxX = (p['max_x'] as num?)?.toDouble() ?? 100.0;
        final maxY = (p['max_y'] as num?)?.toDouble() ?? 100.0;
        final vw = (p['viewport_width'] as num?)?.toDouble() ?? 800.0;
        final vh = (p['viewport_height'] as num?)?.toDouble() ?? 600.0;
        final bStats = calculateRegionBoxStats(minX, minY, maxX, maxY, vw, vh);
        return {'success': true, 'message': '选区统计计算完成', 'data': bStats};

      case 'workbench.minimap.refresh':
        final w = (p['width'] as num?)?.toInt() ?? 200;
        final h = (p['height'] as num?)?.toInt() ?? 160;
        unawaited(refreshMinimap(width: w, height: h));
        return {
          'success': true,
          'message': '导航图缩略图刷新中',
          'data': {'width': w, 'height': h},
        };

      case 'workbench.minimap.navigate':
        final wx = (p['world_x'] as num?)?.toDouble() ?? 0.0;
        final wy = (p['world_y'] as num?)?.toDouble() ?? 0.0;
        navigateCameraToWorld(wx, wy);
        return {
          'success': true,
          'message': '已导航至物理坐标 ($wx, $wy)',
          'data': {'world_x': wx, 'world_y': wy},
        };

      case 'workbench.overlay.command_palette':
        final open = p['open'] as bool?;
        toggleCommandPalette(open);
        return {
          'success': true,
          'message': '命令面板状态已更新',
          'data': {'open': _isCommandPaletteOpen},
        };

      case 'workbench.overlay.shortcut_help':
        final open = p['open'] as bool?;
        toggleShortcutOverlay(open);
        return {
          'success': true,
          'message': '快捷键速查状态已更新',
          'data': {'open': _isShortcutOverlayOpen},
        };

      case 'workbench.overlay.performance':
        final open = p['open'] as bool?;
        togglePerformancePanel(open);
        return {
          'success': true,
          'message': '性能诊断状态已更新',
          'data': {'open': _isPerformancePanelOpen},
        };

      case 'workbench.overlay.tile_inspector':
        final open = p['open'] as bool?;
        toggleTileInspector(open);
        return {
          'success': true,
          'message': '瓦片检查器状态已更新',
          'data': {'open': _isTileInspectorOpen},
        };

      case 'workbench.overlay.render_settings':
        final open = p['open'] as bool?;
        toggleRenderSettings(open);
        return {
          'success': true,
          'message': '渲染设置状态已更新',
          'data': {'open': _isRenderSettingsOpen},
        };

      case 'workbench.export':
        final path = p['path'] as String?;
        if (path == null || path.isEmpty) {
          return {'success': false, 'message': '缺少必要参数: path'};
        }
        final fmt = (p['format'] as String?) ?? 'ply';
        final ok = exportPointCloud(path, format: fmt);
        return {
          'success': ok,
          'message': ok ? '点云已导出至: $path' : '点云导出失败',
          'data': {'path': path, 'format': fmt},
        };

      case 'workbench.cache.clear':
        clearCache();
        return {'success': true, 'message': '显存与瓦片缓存已清空'};

      case 'workbench.theme.set':
        final idx = (p['index'] as num?)?.toInt() ?? 0;
        setTheme(idx);
        return {
          'success': true,
          'message': '主题已切换为: $themeName',
          'data': {'theme_index': _themeIndex, 'theme_name': themeName},
        };

      case 'workbench.theme.cycle':
        cycleTheme();
        return {
          'success': true,
          'message': '主题已切换为: $themeName',
          'data': {'theme_index': _themeIndex, 'theme_name': themeName},
        };

      case 'workbench.camera.link':
        if (p.containsKey('linked')) {
          setCameraLinked(p['linked'] == true);
        } else {
          toggleCameraLinked();
        }
        return {
          'success': true,
          'message': '相机联动: ${_cameraLinked ? "已开启" : "已关闭"}',
          'data': {'camera_linked': _cameraLinked},
        };

      case 'workbench.view.new':
        requestNewView();
        return {'success': true, 'message': '已重置为等轴测视角'};

      case 'workbench.workspace.new_window':
        requestNewWorkspaceWindow();
        return {'success': true, 'message': '已恢复默认工作区布局'};

      case 'workbench.workspace.restore':
        restoreWorkspace();
        return {'success': true, 'message': '工作区已恢复默认布局'};

      case 'workbench.panel.toggle':
        final panel = p['panel'] as String?;
        if (panel == null || panel.isEmpty) {
          return {'success': false, 'message': '缺少必要参数: panel'};
        }
        if (p.containsKey('visible')) {
          setNativePanelVisible(panel, p['visible'] == true);
        } else {
          toggleNativePanel(panel);
        }
        return {
          'success': true,
          'message': '面板状态已更新: $panel',
          'data': Map<String, dynamic>.from(_nativePanelVisible),
        };

      case 'workbench.file.open_native':
        openViaNativeFileMenu();
        return {'success': true, 'message': '已触发原生打开文件对话框'};

      case 'workbench.file.open_bundle_native':
        openViaNativeBundleMenu();
        return {'success': true, 'message': '已触发原生打开工区包对话框'};

      case 'workbench.screenshot.native':
        requestNativeMenuScreenshot();
        return {'success': true, 'message': '已触发原生截图保存'};

      case 'workbench.diagnostics.refresh':
        unawaited(refreshNativeStatusBar());
        return {
          'success': true,
          'message': '已请求刷新原生诊断指标',
          'data': {
            'fps': _fps,
            'visible_points': _visiblePoints,
            'loaded_tiles': _loadedTiles,
            'pending_tiles': _pendingTiles,
            'gpu_memory_bytes': _gpuMemoryBytes,
            'camera_coords': _cameraCoordsString,
          },
        };

      case 'workbench.measure.display_mode':
        final mode = (p['mode'] as String?) ?? '3d';
        if (!const ['3d', '2d', 'both'].contains(mode)) {
          return {'success': false, 'message': 'mode 必须是 3d/2d/both'};
        }
        setMeasurementDisplayMode(mode);
        return {
          'success': true,
          'message': '测量标签显示模式: $mode',
          'data': {'mode': mode},
        };

      case 'workbench.measure.remove':
        String? id = p['id'] as String?;
        if (id == null && p['index'] is num) {
          final idx = (p['index'] as num).toInt();
          if (idx >= 0 && idx < _measurementLines.length) {
            id = _measurementLines[idx].id;
          } else if (isNativeRendererActive) {
            id = idx.toString();
          }
        }
        if (id == null) {
          return {'success': false, 'message': '缺少 index 或 id'};
        }
        removeMeasurementLine(id);
        return {
          'success': true,
          'message': '已删除测量线',
          'data': {'id': id, 'lines_count': _measurementLines.length},
        };

      case 'workbench.measure.toggle_fixed':
        String? id = p['id'] as String?;
        if (id == null && p['index'] is num) {
          final idx = (p['index'] as num).toInt();
          if (idx >= 0 && idx < _measurementLines.length) {
            id = _measurementLines[idx].id;
          } else if (isNativeRendererActive) {
            id = idx.toString();
          }
        }
        if (id == null) {
          return {'success': false, 'message': '缺少 index 或 id'};
        }
        toggleMeasurementLineFixed(id);
        return {
          'success': true,
          'message': '已切换测量线固定状态',
          'data': {'id': id},
        };

      case 'workbench.camera.focus_point':
        final x = (p['x'] as num?)?.toDouble();
        final y = (p['y'] as num?)?.toDouble();
        final z = (p['z'] as num?)?.toDouble();
        if (x == null || y == null || z == null) {
          return {'success': false, 'message': '缺少必要参数: x, y, z'};
        }
        final focus = Point3D(
          x: x,
          y: y,
          z: z,
          value: (p['value'] as num?)?.toDouble() ?? 0.0,
        );
        setOrbitPivot(focus);
        return {
          'success': true,
          'message': '旋转中心已对焦',
          'data': {'x': x, 'y': y, 'z': z},
        };

      case 'workbench.gizmo.axis':
        final axis = (p['axis'] as num?)?.toInt();
        if (axis == null || axis < 0 || axis > 5) {
          return {'success': false, 'message': 'axis 必须是 0-5'};
        }
        // engine 激活时优先引擎预设；否则 Dart 相机预设。
        final gizmoFn = _bindings?.engine_gizmo_axis_opt;
        final eng = _engineHandle;
        String preset = 'iso';
        if (eng != null && gizmoFn != null) {
          final rc = gizmoFn(eng, axis);
          if (rc != 0) {
            return {'success': false, 'message': 'engine gizmo rc=$rc'};
          }
          _scheduleEngineRender();
        } else {
          const presets = <int, String>{
            0: 'front',
            1: 'front',
            2: 'side',
            3: 'side',
            4: 'top',
            5: 'top',
          };
          preset = presets[axis] ?? 'iso';
          setCameraView(preset: preset);
        }
        return {
          'success': true,
          'message': '已切换导航球预设视角 axis=$axis ($preset)',
          'data': {'axis': axis, 'preset': preset},
        };

      default:
        return {'success': false, 'message': '未知的动作 ID: $actionId'};
    }
  }

  /// 执行通用 JSON-RPC 指令（支持 MCP 动作协议与底层 C++ 引擎命令）
  Future<String> executeJsonRpcAsync(String requestJson) async {
    try {
      final request = jsonDecode(requestJson);
      if (request is! Map<String, dynamic> || request['jsonrpc'] != '2.0') {
        return jsonEncode({
          'jsonrpc': '2.0',
          'id': null,
          'error': {'code': -32600, 'message': 'Invalid Request'},
        });
      }
      if (request['method'] != 'ui_action') return executeJsonRpc(requestJson);
      final params = request['params'];
      if (params is! Map<String, dynamic> ||
          params['action'] is! String ||
          (params['params'] != null &&
              params['params'] is! Map<String, dynamic>)) {
        return jsonEncode({
          'jsonrpc': '2.0',
          'id': request['id'],
          'error': {'code': -32602, 'message': 'Invalid params'},
        });
      }
      final result = await executeActionAsync(
        params['action'] as String,
        params['params'] as Map<String, dynamic>? ?? {},
      );
      return request.containsKey('id')
          ? jsonEncode({
              'jsonrpc': '2.0',
              'id': request['id'],
              'result': result,
            })
          : '';
    } on FormatException {
      return jsonEncode({
        'jsonrpc': '2.0',
        'id': null,
        'error': {'code': -32700, 'message': 'Parse error'},
      });
    }
  }

  String executeJsonRpc(String requestJson) {
    try {
      final decoded = jsonDecode(requestJson) as Map<String, dynamic>;
      final id = decoded['id'];
      final method = decoded['method'] as String?;

      if (method == 'ui_action') {
        final params = decoded['params'] as Map<String, dynamic>? ?? {};
        final action = params['action'] as String? ?? '';
        final actionParams = params['params'] as Map<String, dynamic>?;
        final res = executeAction(action, actionParams);
        return jsonEncode({'jsonrpc': '2.0', 'id': id, 'result': res});
      } else if (method == 'list_ui_actions') {
        return jsonEncode({
          'jsonrpc': '2.0',
          'id': id,
          'result': getAvailableActions().map((a) => a.toJson()).toList(),
        });
      }
    } catch (_) {}

    if (_bindings == null) return '{}';
    // 同步接口禁止阻塞 Viewer RPC（会卡 UI）。已连接时请走 executeJsonRpcAsync。
    if (isNativeRendererActive) {
      return jsonEncode({
        'jsonrpc': '2.0',
        'id': null,
        'error': {
          'code': -32000,
          'message': 'viewer connected: use executeJsonRpcAsync',
        },
      });
    }
    final nativeReq = requestJson.toNativeUtf8();
    try {
      final resPtr = _bindings!.execute_command(nativeReq);
      return resPtr.toDartString();
    } finally {
      calloc.free(nativeReq);
    }
  }

  void setPointSize(double size) {
    if (_bindings != null) {
      _bindings!.set_point_size(size);
    }
    _pointSize = size;
    _applyEngineRenderSettings();
    notifyListeners();
  }

  void setColormap(String name) {
    if (_bindings != null) {
      final nativeStr = name.toNativeUtf8();
      try {
        _bindings!.set_colormap(nativeStr);
      } finally {
        calloc.free(nativeStr);
      }
    }
    _colormap = name;
    _applyEngineRenderSettings();
    notifyListeners();
  }

  void setScalarRange(double minVal, double maxVal) {
    if (_bindings != null) {
      _bindings!.set_scalar_range(minVal, maxVal);
    }
    _scalarMin = minVal;
    _scalarMax = maxVal;
    _applyEngineRenderSettings();
    notifyListeners();
  }

  /// 将当前 UI 渲染参数**全量**写入单进程 engine。
  ///
  /// 这是渲染设置唯一的后端同步路径（旧双进程控制面已删除）：
  /// 点大小 / 色带 / 点形状 / 值域裁切 / 高度源 / 着色源 / 高度夸张 / 背景。
  /// [scheduleRender]=false 仅写参数（启动路径会自己 await 首帧，避免竞态）。
  void _applyEngineRenderSettings({bool scheduleRender = true}) {
    final e = _engineHandle;
    final b = _bindings;
    if (e == null || b == null) return;

    b.engine_set_point_size_opt?.call(e, _pointSize);
    final cm = _nativeColormapIndex(_colormap);
    if (cm != null) b.engine_set_colormap_index_opt?.call(e, cm);
    final shape = _nativePointShape(_pointShape);
    if (shape != null) b.engine_set_point_shape_opt?.call(e, shape);
    // 标量区间：与 set_value_clip 语义一致（启用裁剪）
    b.engine_set_value_clip_opt?.call(e, 1, _scalarMin, _scalarMax);
    b.engine_set_height_exaggeration_opt?.call(e, _heightScale);
    b.engine_set_height_source_opt?.call(e, _heightSourceIndex);
    b.engine_set_color_source_opt?.call(e, _colorSourceIndex);
    final bg = _viewportBackgroundColor;
    // Flutter 3.27+: Color.r/g/b 为 0–1 double
    b.engine_set_background_opt?.call(e, bg.r, bg.g, bg.b, 1.0);
    if (scheduleRender) _scheduleEngineRender();
  }

  /// 0=Z 高程, 1=Value 属性
  int get _heightSourceIndex {
    final s = _heightSource.toLowerCase();
    if (s.contains('value') || s.contains('属性') || s.contains('fold')) {
      return 1;
    }
    return 0;
  }

  int get _colorSourceIndex {
    final s = _colorAttribute.toLowerCase();
    if (s.contains('elev') || s.contains('高程') || s == 'z') return 0;
    return 1;
  }

  static int? _nativeColormapIndex(String name) {
    switch (name.toLowerCase()) {
      case 'geo':
        return 0;
      case 'viridis':
        return 1;
      case 'jet':
        return 2;
      case 'gray':
      case 'grey':
        return 3;
      case 'thermal':
        return 4;
      case 'coolwarm':
      case 'cool-warm':
        return 5;
      case 'turbo':
        return 6;
      case 'plasma':
        return 7;
      case 'rainbow':
      case 'rainbow256':
        return 8;
      default:
        return null;
    }
  }

  @visibleForTesting
  void setSummaryForTest(DatasetSummary s) {
    _summary = s;
    notifyListeners();
  }

  void shutdown() {
    if (_welcomeBusy || _activePreprocessProcess != null) cancelPreprocess();
    unawaited(stopNativeRenderer());
    stopMetricsPolling();
    stopDragDropPolling();
    if (_bindings != null) {
      _bindings!.shutdown();
      _initialized = false;
      _summary = const DatasetSummary();
      _recentProjects.clear();
      _points.clear();
      notifyListeners();
    }
  }

  Future<void> shutdownAsync() async {
    if (_welcomeBusy) cancelPreprocess();
    await _welcomeTask;
    await stopNativeRenderer();
    shutdown();
  }
}
