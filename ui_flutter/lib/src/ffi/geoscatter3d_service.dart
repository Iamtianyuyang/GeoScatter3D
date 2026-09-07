import 'dart:async';
import 'dart:convert';
import 'dart:ffi' as ffi;
import 'dart:ffi';
import 'dart:io';
import 'dart:math' as math;
import 'dart:typed_data';
import 'package:ffi/ffi.dart';
import 'package:flutter/material.dart';
import '../models/dataset_model.dart';
import '../models/gpu_info_model.dart';
import '../models/point_cloud_model.dart';
import '../models/recent_project_model.dart';
import '../models/measurement_line_model.dart';
import '../native/viewer_control_client.dart';
import 'geoscatter3d_bindings.dart';

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

class GeoScatter3dService extends ChangeNotifier {
  static final GeoScatter3dService _instance = GeoScatter3dService._internal();
  factory GeoScatter3dService() => _instance;
  GeoScatter3dService._internal();

  GeoScatter3dBindings? _bindings;
  bool _initialized = false;
  NativeViewerControlClient? _nativeViewer;
  Uint8List? _nativeViewportPng;
  String? _nativeViewerError;
  String? _loadedDatasetPath;
  DatasetSummary _summary = const DatasetSummary();
  List<RecentProjectItem> _recentProjects = [];
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

  // 截图通知回调
  VoidCallback? onScreenshotRequested;

  bool get isInitialized => _initialized;
  DatasetSummary get summary => _summary;
  List<RecentProjectItem> get recentProjects => _recentProjects;
  List<GpuDeviceInfo> get gpus => _gpus;
  int get activeGpuIndex => _activeGpuIndex;
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

  // 动态性能与流式加载指标
  double _fps = 120.0;
  double _frameTimeMs = 0.83;
  int _visiblePoints = 0;
  int _gpuMemoryBytes = 0;
  int _loadedTiles = 1;
  int _pendingTiles = 0;
  double _cacheHitRate = 99.2;
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
  int _minimapWidth = 200;
  int _minimapHeight = 160;

  // 全局命令面板与快捷键速查图层
  bool _isCommandPaletteOpen = false;
  bool _isShortcutOverlayOpen = false;
  bool _isPerformancePanelOpen = false;
  bool _isTileInspectorOpen = false;

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
  int get minimapWidth => _minimapWidth;
  int get minimapHeight => _minimapHeight;

  bool get isCommandPaletteOpen => _isCommandPaletteOpen;
  bool get isShortcutOverlayOpen => _isShortcutOverlayOpen;
  bool get isPerformancePanelOpen => _isPerformancePanelOpen;
  bool get isTileInspectorOpen => _isTileInspectorOpen;

  bool get leftDockVisible => _leftDockVisible;
  bool get rightDockVisible => _rightDockVisible;
  bool get isNativeRendererActive => _nativeViewer?.isConnected ?? false;
  Uint8List? get nativeViewportPng => _nativeViewportPng;
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
    _queueNativeCommand('canvas.viewport', 'reset_camera');
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
    final previousAzimuth = _cameraAzimuth;
    final previousElevation = _cameraElevation;
    final previousZoom = _cameraZoom;
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
    final nativeAxis = _nativeAxisForPreset(preset);
    if (nativeAxis != null) {
      _queueNativeCommand('gizmo.navigation', 'click', {'axis': nativeAxis});
    } else if (preset == null) {
      if (azimuth != null || elevation != null) {
        _queueNativeCamera({
          'rotate_delta_x': (_cameraAzimuth - previousAzimuth) / 0.4,
          'rotate_delta_y': (previousElevation - _cameraElevation) / 0.4,
        });
      } else if (panX != null || panY != null) {
        _queueNativeCamera({
          'pan_delta_x': _cameraPanX - previousPanX,
          'pan_delta_y': _cameraPanY - previousPanY,
        });
      } else if (zoom != null && previousZoom > 0.0 && _cameraZoom > 0.0) {
        _queueNativeCamera({
          'scroll_y': math.log(_cameraZoom / previousZoom) / math.log(1.15),
        });
      }
    }
    notifyListeners();
  }

  void setViewportOverlays({bool? mapAxis, bool? worldAxis, bool? crosshair}) {
    if (mapAxis != null) _showMapAxis = mapAxis;
    if (worldAxis != null) _showWorldAxis = worldAxis;
    if (crosshair != null) _showCrosshair = crosshair;
    notifyListeners();
  }

  void setPointShape(String shape) {
    if (_pointShape != shape) {
      _pointShape = shape;
      final nativeShape = _nativePointShape(shape);
      if (nativeShape != null) {
        _queueNativeRenderSettings({'point_shape': nativeShape});
      }
      notifyListeners();
    }
  }

  void setHeightScale(double scale) {
    final clamped = scale.clamp(0.1, 10.0);
    if (_heightScale != clamped) {
      _heightScale = clamped;
      _queueNativeRenderSettings({'height_exaggeration': clamped});
      notifyListeners();
    }
  }

  void setHeightSource(String source) {
    if (_heightSource != source) {
      _heightSource = source;
      _queueNativeRenderSettings({'height_attribute': source});
      notifyListeners();
    }
  }

  void setColorAttribute(String attr) {
    if (_colorAttribute != attr) {
      _colorAttribute = attr;
      _queueNativeRenderSettings({'color_attribute': attr});
      notifyListeners();
    }
  }

  void setViewportBackgroundColor(Color color) {
    if (_viewportBackgroundColor != color) {
      _viewportBackgroundColor = color;
      notifyListeners();
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

  Map<String, dynamic> calculateStats() {
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

  void requestScreenshot() {
    if (isNativeRendererActive) {
      unawaited(refreshNativeViewport());
    }
    if (onScreenshotRequested != null) {
      onScreenshotRequested!();
    }
  }

  /// 启动原生 ViewerApp，并通过其控制面取得真实 Vulkan 渲染帧。
  ///
  /// Flutter 仍负责界面组合；LOD、tile、GPU pick 和渲染生命周期由原生进程持有。
  Future<bool> startNativeRenderer() async {
    final datasetPath = _loadedDatasetPath;
    if (datasetPath == null || datasetPath.isEmpty) {
      _nativeViewerError = '请先载入数据集';
      notifyListeners();
      return false;
    }

    await stopNativeRenderer();
    final executable = _resolveNativeViewerExecutable();
    if (executable == null) {
      _nativeViewerError = '未找到 GeoScatter3D.exe';
      notifyListeners();
      return false;
    }

    final client = NativeViewerControlClient();
    _nativeViewer = client;
    try {
      final started = await client.start(
        executablePath: executable,
        datasetPath: datasetPath,
      );
      if (!started) {
        _nativeViewer = null;
        _nativeViewerError = '原生查看器未能在 10 秒内就绪';
        notifyListeners();
        return false;
      }
      _nativeViewerError = null;
      await refreshNativeViewport();
      return true;
    } catch (error) {
      _nativeViewer = null;
      _nativeViewerError = '原生查看器启动失败: $error';
      notifyListeners();
      return false;
    }
  }

  Future<void> refreshNativeViewport() async {
    final client = _nativeViewer;
    if (client == null || !client.isConnected) return;
    try {
      _nativeViewportPng = await client.capturePng();
      _nativeViewerError = null;
    } catch (error) {
      _nativeViewerError = '读取原生渲染帧失败: $error';
    }
    notifyListeners();
  }

  Future<void> stopNativeRenderer() async {
    final client = _nativeViewer;
    _nativeViewer = null;
    _nativeViewportPng = null;
    if (client != null) {
      await client.stop();
    }
    notifyListeners();
  }

  void _queueNativeRenderSettings(Map<String, dynamic> params) {
    _queueNativeCommand('runtime.render_settings', 'set', params);
  }

  void _queueNativeCamera(Map<String, dynamic> params) {
    final hasMovement = params.values.any(
      (value) => value is num && value != 0.0,
    );
    if (hasMovement) {
      _queueNativeCommand('runtime.camera', 'input', params);
    }
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
    final client = _nativeViewer;
    if (client == null ||
        !client.isConnected ||
        displayWidth <= 0 ||
        displayHeight <= 0) {
      return;
    }
    final sourceWidth = client.lastCaptureWidth;
    final sourceHeight = client.lastCaptureHeight;
    if (sourceWidth <= 0 || sourceHeight <= 0) return;
    final scale = math.min(
      displayWidth / sourceWidth,
      displayHeight / sourceHeight,
    );
    final offsetX = (displayWidth - sourceWidth * scale) * 0.5;
    final offsetY = (displayHeight - sourceHeight * scale) * 0.5;
    double mapX(double x) => (x - offsetX) / scale;
    double mapY(double y) => (y - offsetY) / scale;
    final params = <String, dynamic>{
      'kind': kind,
      'screen_x': mapX(localX),
      'screen_y': mapY(localY),
    };
    if (localMaxX != null && localMaxY != null) {
      params['screen_min_x'] = mapX(localX);
      params['screen_min_y'] = mapY(localY);
      params['screen_max_x'] = mapX(localMaxX);
      params['screen_max_y'] = mapY(localMaxY);
    }
    _queueNativeCommand('runtime.pick', 'input', params);
  }

  void _synchronizeNativeMeasurementMode(bool enabled) {
    final client = _nativeViewer;
    if (client == null || !client.isConnected) return;
    unawaited(_syncNativeMeasurementMode(client, enabled));
  }

  Future<void> _syncNativeMeasurementMode(
    NativeViewerControlClient client,
    bool enabled,
  ) async {
    try {
      final response = await client.request('get_state', {
        'component': 'toolbar.measure',
      });
      final result = response['result'];
      final nativeEnabled = result is Map<String, dynamic>
          ? result['measure_mode_active'] as bool?
          : null;
      if (nativeEnabled != enabled) {
        await client.request('execute', {
          'component': 'toolbar.measure',
          'command': 'click',
          'params': const {},
        });
        if (identical(client, _nativeViewer)) {
          await refreshNativeViewport();
        }
      }
    } catch (error) {
      if (identical(client, _nativeViewer)) {
        _nativeViewerError = '原生测量模式同步失败: $error';
        notifyListeners();
      }
    }
  }

  void _queueNativeCommand(
    String component,
    String command, [
    Map<String, dynamic> params = const {},
  ]) {
    final client = _nativeViewer;
    if (client == null || !client.isConnected) return;
    unawaited(_sendNativeCommand(client, component, command, params));
  }

  Future<void> _sendNativeCommand(
    NativeViewerControlClient client,
    String component,
    String command,
    Map<String, dynamic> params,
  ) async {
    try {
      await client.request('execute', {
        'component': component,
        'command': command,
        'params': params,
      });
      if (identical(client, _nativeViewer)) {
        await refreshNativeViewport();
      }
    } catch (error) {
      if (identical(client, _nativeViewer)) {
        _nativeViewerError = '原生命令执行失败: $error';
        notifyListeners();
      }
    }
  }

  static int? _nativeAxisForPreset(String? preset) {
    switch (preset?.toLowerCase()) {
      case 'top':
        return 4; // +Z
      case 'front':
        return 1; // -X
      case 'side':
        return 2; // +Y
      default:
        return null;
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

  String? _resolveNativeViewerExecutable() {
    final candidates = <String>[
      '${File(Platform.resolvedExecutable).parent.path}${Platform.pathSeparator}GeoScatter3D.exe',
      '${Directory.current.path}${Platform.pathSeparator}GeoScatter3D.exe',
      '${resolveRepoRoot()}${Platform.pathSeparator}tmp${Platform.pathSeparator}build-win${Platform.pathSeparator}Release${Platform.pathSeparator}GeoScatter3D.exe',
    ];
    for (final candidate in candidates) {
      if (File(candidate).existsSync()) return candidate;
    }
    return null;
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
    if (isNativeRendererActive) {
      unawaited(_pollNativeMetrics());
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
    if (isNativeRendererActive) {
      _queueNativeCommand('runtime.diagnostics', 'clear_cache');
      return;
    }
    if (_bindings != null && _initialized) {
      try {
        _bindings!.clear_cache();
      } catch (_) {}
    }
    _loadedTiles = 0;
    _pendingTiles = 0;
    notifyListeners();
  }

  Future<void> _pollNativeMetrics() async {
    final client = _nativeViewer;
    if (client == null || !client.isConnected) return;
    try {
      final response = await client.request('get_state', {
        'component': 'runtime.diagnostics',
      });
      if (!identical(client, _nativeViewer)) return;
      final result = response['result'];
      if (result is! Map<String, dynamic>) return;
      _fps = (result['fps'] as num?)?.toDouble() ?? _fps;
      _frameTimeMs = _fps > 0 ? 1000.0 / _fps : 0.0;
      _visiblePoints =
          (result['visible_points'] as num?)?.toInt() ?? _visiblePoints;
      _gpuMemoryBytes =
          (result['gpu_memory_bytes'] as num?)?.toInt() ?? _gpuMemoryBytes;
      _loadedTiles = (result['loaded_tiles'] as num?)?.toInt() ?? _loadedTiles;
      _pendingTiles =
          (result['pending_tiles'] as num?)?.toInt() ?? _pendingTiles;
      _cacheHitRate =
          (result['cache_hit_rate'] as num?)?.toDouble() ?? _cacheHitRate;
      final camera = result['camera_position'];
      if (camera is String && camera.isNotEmpty) _cameraCoordsString = camera;
      notifyListeners();
    } catch (_) {
      // 原生进程停止时由 stopNativeRenderer 统一清理状态。
    }
  }

  Point3D? pickPointAt(
    double screenX,
    double screenY,
    double viewportWidth,
    double viewportHeight, {
    double tolerancePx = 25.0,
  }) {
    if (_bindings != null && _initialized) {
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
    }

    if (_points.isEmpty) return null;
    final radAz = _cameraAzimuth * (math.pi / 180.0);
    final radEl = _cameraElevation * (math.pi / 180.0);
    final cosAz = math.cos(radAz);
    final sinAz = math.sin(radAz);
    final cosEl = math.cos(radEl);
    final sinEl = math.sin(radEl);

    final bmin = _summary.bboxMin;
    final bmax = _summary.bboxMax;
    final midX = (bmin[0] + bmax[0]) * 0.5;
    final midY = (bmin[1] + bmax[1]) * 0.5;
    final midZ = (bmin[2] + bmax[2]) * 0.5;
    final maxSpan = math.max(
      bmax[0] - bmin[0],
      math.max(bmax[1] - bmin[1], bmax[2] - bmin[2]),
    );
    final baseScale = (maxSpan > 1e-4)
        ? (math.min(viewportWidth, viewportHeight) * 0.65 / maxSpan)
        : 1.0;
    final totalScale = baseScale * _cameraZoom;
    final cx = viewportWidth * 0.5 + _cameraPanX;
    final cy = viewportHeight * 0.5 + _cameraPanY;

    Point3D? closest;
    double closestDist = tolerancePx;
    for (final p in _points) {
      final rx = p.x - midX;
      final ry = p.y - midY;
      final rz = p.z - midZ;

      final x1 = rx * cosAz - ry * sinAz;
      final y1 = rx * sinAz + ry * cosAz;
      final z1 = rz;

      final x2 = x1;
      final y2 = y1 * cosEl - z1 * sinEl;

      final px = cx + x2 * totalScale;
      final py = cy - y2 * totalScale;

      final dist = math.sqrt(
        (screenX - px) * (screenX - px) + (screenY - py) * (screenY - py),
      );
      if (dist < closestDist) {
        closestDist = dist;
        closest = p;
      }
    }
    return closest;
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

  void setMeasurementMode(bool enabled) {
    if (_isMeasurementMode != enabled) {
      _isMeasurementMode = enabled;
      if (!enabled) {
        _pendingMeasureStart = null;
      }
      notifyListeners();
    }
    _synchronizeNativeMeasurementMode(enabled);
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

    if (_points.isEmpty) {
      final empty = {
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

    final radAz = _cameraAzimuth * (math.pi / 180.0);
    final radEl = _cameraElevation * (math.pi / 180.0);
    final cosAz = math.cos(radAz);
    final sinAz = math.sin(radAz);
    final cosEl = math.cos(radEl);
    final sinEl = math.sin(radEl);

    final bmin = _summary.bboxMin;
    final bmax = _summary.bboxMax;
    final midX = (bmin[0] + bmax[0]) * 0.5;
    final midY = (bmin[1] + bmax[1]) * 0.5;
    final midZ = (bmin[2] + bmax[2]) * 0.5;
    final maxSpan = math.max(
      bmax[0] - bmin[0],
      math.max(bmax[1] - bmin[1], bmax[2] - bmin[2]),
    );
    final baseScale = (maxSpan > 1e-4)
        ? (math.min(viewportWidth, viewportHeight) * 0.65 / maxSpan)
        : 1.0;
    final totalScale = baseScale * _cameraZoom;
    final cx = viewportWidth * 0.5 + _cameraPanX;
    final cy = viewportHeight * 0.5 + _cameraPanY;

    final selected = <Point3D>[];
    for (final p in _points) {
      final rx = p.x - midX;
      final ry = p.y - midY;
      final rz = p.z - midZ;

      final x1 = rx * cosAz - ry * sinAz;
      final y1 = rx * sinAz + ry * cosAz;
      final z1 = rz;

      final x2 = x1;
      final y2 = y1 * cosEl - z1 * sinEl;

      final px = cx + x2 * totalScale;
      final py = cy - y2 * totalScale;

      if (px >= minX && px <= maxX && py >= minY && py <= maxY) {
        selected.add(p);
      }
    }

    if (selected.isEmpty) {
      final empty = {
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

    double sum = 0.0;
    double smin = selected[0].value;
    double smax = selected[0].value;
    for (final p in selected) {
      sum += p.value;
      if (p.value < smin) smin = p.value;
      if (p.value > smax) smax = p.value;
    }
    final mean = sum / selected.length;
    double varSum = 0.0;
    for (final p in selected) {
      varSum += (p.value - mean) * (p.value - mean);
    }
    final stdDev = math.sqrt(varSum / selected.length);

    final bins = [0, 0, 0, 0, 0];
    final step = (smax > smin) ? (smax - smin) / 5.0 : 1.0;
    for (final p in selected) {
      final b = ((p.value - smin) / step).floor().clamp(0, 4);
      bins[b]++;
    }

    final stats = {
      'count': selected.length,
      'mean': mean,
      'min': smin,
      'max': smax,
      'std_dev': stdDev,
      'histogram': bins,
    };
    _activeRegionStats = stats;
    notifyListeners();
    return stats;
  }

  void clearRegionStats() {
    _activeRegionStats = null;
    notifyListeners();
  }

  Uint8List? refreshMinimap({int width = 200, int height = 160}) {
    _minimapWidth = width;
    _minimapHeight = height;

    if (_bindings != null && _initialized) {
      final bufferSize = width * height * 4;
      final pBuf = calloc<ffi.Uint8>(bufferSize);
      try {
        final res = _bindings!.get_nav_map_thumbnail(pBuf, width, height);
        if (res == 1) {
          _minimapRgba = Uint8List.fromList(pBuf.asTypedList(bufferSize));
          notifyListeners();
          return _minimapRgba;
        }
      } catch (_) {
      } finally {
        calloc.free(pBuf);
      }
    }

    final buffer = Uint8List(width * height * 4);
    for (int i = 0; i < width * height; ++i) {
      buffer[i * 4 + 0] = 0x1A;
      buffer[i * 4 + 1] = 0x1F;
      buffer[i * 4 + 2] = 0x2C;
      buffer[i * 4 + 3] = 0xFF;
    }

    if (_points.isNotEmpty) {
      final bmin = _summary.bboxMin;
      final bmax = _summary.bboxMax;
      final spanX = (bmax[0] - bmin[0]) > 1e-4 ? (bmax[0] - bmin[0]) : 1.0;
      final spanY = (bmax[1] - bmin[1]) > 1e-4 ? (bmax[1] - bmin[1]) : 1.0;

      for (final p in _points) {
        final normX = ((p.x - bmin[0]) / spanX).clamp(0.0, 1.0);
        final normY = ((p.y - bmin[1]) / spanY).clamp(0.0, 1.0);
        final px = (normX * (width - 1)).round();
        final py = ((1.0 - normY) * (height - 1)).round();
        final idx = (py * width + px) * 4;
        buffer[idx + 0] = 0x4F;
        buffer[idx + 1] = 0x46;
        buffer[idx + 2] = 0xE5;
        buffer[idx + 3] = 0xFF;
      }
    }

    _minimapRgba = buffer;
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

  bool exportPointCloud(String outputPath, {String format = 'ply'}) {
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

    try {
      final file = File(outputPath);
      if (!file.parent.existsSync()) {
        try {
          file.parent.createSync(recursive: true);
        } catch (_) {}
      }
      final sb = StringBuffer();
      final isPly = format.toLowerCase() == 'ply';
      if (isPly) {
        sb.writeln('ply');
        sb.writeln('format ascii 1.0');
        sb.writeln('element vertex ${_points.length}');
        sb.writeln('property float x');
        sb.writeln('property float y');
        sb.writeln('property float z');
        sb.writeln('property float value');
        sb.writeln('end_header');
        for (final p in _points) {
          sb.writeln('${p.x} ${p.y} ${p.z} ${p.value}');
        }
      } else {
        sb.writeln('x,y,z,value');
        for (final p in _points) {
          sb.writeln('${p.x},${p.y},${p.z},${p.value}');
        }
      }
      file.writeAsStringSync(sb.toString());
      return true;
    } catch (e) {
      debugPrint('Export failed: $e');
      return false;
    }
  }

  /// 由运行中的 ViewerApp 从源 GS3D 全量导出，并等待原生任务完成。
  /// 未连接原生查看器时，保留 FFI 的全量导出作为兼容路径。
  Future<bool> exportPointCloudThroughNativeViewer(
    String outputPath, {
    String format = 'ply',
  }) async {
    final client = _nativeViewer;
    if (client == null || !client.isConnected) {
      return exportPointCloud(outputPath, format: format);
    }

    try {
      final response = await client.request('execute', {
        'component': 'runtime.dataset_export',
        'command': 'export',
        'params': {'output_path': outputPath, 'format': format},
      });
      final result = response['result'];
      final requestId = result is Map<String, dynamic>
          ? (result['request_id'] as num?)?.toInt()
          : null;
      if (requestId == null) return false;

      final deadline = DateTime.now().add(const Duration(minutes: 2));
      while (DateTime.now().isBefore(deadline)) {
        await Future<void>.delayed(const Duration(milliseconds: 150));
        final stateResponse = await client.request('get_state', {
          'component': 'runtime.dataset_export',
        });
        final state = stateResponse['result'];
        if (state is! Map<String, dynamic>) continue;
        final completed = (state['completed_request_id'] as num?)?.toInt();
        if (completed != requestId) continue;
        return state['success'] == true;
      }
      return false;
    } catch (error) {
      if (identical(client, _nativeViewer)) {
        _nativeViewerError = '原生完整数据导出失败: $error';
        notifyListeners();
      }
      return false;
    }
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
      }
      notifyListeners();
      return _initialized;
    } catch (e) {
      debugPrint('[GeoScatter3D FFI] Failed to initialize: $e');
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
    }
    final lower = norm.toLowerCase();
    if (lower.endsWith('.gs3d') || lower.endsWith('.gs3d.bundle')) {
      loadDataset(norm);
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
      _activePreprocessProcess = null;
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
      trimmed,
      '../$trimmed',
      '../../$trimmed',
      '../../../$trimmed',
      '../../../../$trimmed',
      'D:/code/GeoScatter3D/$trimmed',
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
    final candidates = ['D:/code/GeoScatter3D', '.', '..', '../..'];
    for (final c in candidates) {
      if (File('$c/CMakeLists.txt').existsSync() &&
          Directory('$c/config').existsSync()) {
        return Directory(c).absolute.path.replaceAll('\\', '/');
      }
    }
    return Directory.current.absolute.path.replaceAll('\\', '/');
  }

  /// 解析预处理器可执行文件路径
  static String? resolvePreprocessExecutable() {
    final candidates = [
      'tmp/build-win/src/Release/GeoScatter3DPreprocess.exe',
      'tmp/build-win/Release/GeoScatter3DPreprocess.exe',
      'build/windows/x64/runner/Debug/GeoScatter3DPreprocess.exe',
      'build/windows/x64/runner/Release/GeoScatter3DPreprocess.exe',
      'GeoScatter3DPreprocess.exe',
      'D:/code/GeoScatter3D/tmp/build-win/src/Release/GeoScatter3DPreprocess.exe',
      'D:/code/GeoScatter3D/tmp/build-win/Release/GeoScatter3DPreprocess.exe',
      'D:/code/GeoScatter3D/ui_flutter/GeoScatter3DPreprocess.exe',
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

  /// 异步流式预处理构建：监听 stdout / stderr 输出并实时反馈进度状态
  Future<Map<String, dynamic>> buildAndLoadProjectAsync({
    required String path,
    String? name,
    int threads = 8,
    String voxelMode = 'xyz',
    int maxPointsPerTile = 50000,
    void Function(PreprocessProgressInfo progress)? onProgress,
  }) async {
    _preprocessCancelled = false;
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

    if (!isCsvOrDat) {
      final ok = loadDataset(resolvedSource);
      return {
        'success': ok,
        'message': ok ? '工程加载成功: $resolvedSource' : '工程加载失败，请核验文件格式',
        'data': ok ? _summary.toJson() : null,
      };
    }

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

    final stopwatch = Stopwatch()..start();

    void emitProgress(
      String stage,
      double p,
      String detail, {
      bool isRunning = true,
      bool isFinished = false,
      bool isFailed = false,
      String? err,
    }) {
      final info = PreprocessProgressInfo(
        stage: stage,
        progress: p,
        detail: detail,
        elapsedSeconds: stopwatch.elapsedMilliseconds / 1000.0,
        isRunning: isRunning,
        isFinished: isFinished,
        isFailed: isFailed,
        errorMessage: err,
      );
      _latestProgress = info;
      onProgress?.call(info);
      notifyListeners();
    }

    emitProgress('初始化构建环境', 0.10, '启动离线预处理构建器...');

    String lastStderr = '';
    if (preprocessExe != null && File(preprocessExe).existsSync()) {
      try {
        final process = await Process.start(preprocessExe, [
          '--config',
          configPath,
          lower.endsWith('.dat') ? '--dat' : '--csv',
          resolvedSource,
          '--bundle',
          targetBundleDir,
        ], workingDirectory: repoRoot);
        _activePreprocessProcess = process;
        notifyListeners();

        process.stderr
            .transform(utf8.decoder)
            .transform(const LineSplitter())
            .listen((errLine) {
              final cleanErr = errLine.trim();
              if (cleanErr.isNotEmpty) {
                lastStderr = cleanErr;
                debugPrint('[Preprocess stderr] $cleanErr');
              }
            });

        final sub = process.stdout
            .transform(utf8.decoder)
            .transform(const LineSplitter())
            .listen((line) {
              if (_preprocessCancelled) return;
              final clean = line.trim();
              if (clean.isEmpty) return;

              String stage = '正在处理散点数据';
              double p = 0.45;
              if (clean.contains('Reading') ||
                  clean.contains('Parsing') ||
                  clean.contains('csv') ||
                  clean.contains('points')) {
                stage = '阶段 1/3: 解析点云源数据';
                p = 0.35;
              } else if (clean.contains('grid') ||
                  clean.contains('voxel') ||
                  clean.contains('Spatial') ||
                  clean.contains('tile')) {
                stage = '阶段 2/3: 空间剖分与体素重排';
                p = 0.70;
              } else if (clean.contains('lod') ||
                  clean.contains('LOD') ||
                  clean.contains('pyramid') ||
                  clean.contains('bundle')) {
                stage = '阶段 3/3: 构建八叉树 LOD 金字塔';
                p = 0.90;
              }
              emitProgress(stage, p, clean);
            });

        final exitCode = await process.exitCode;
        await sub.cancel();
        _activePreprocessProcess = null;

        if (_preprocessCancelled) {
          emitProgress(
            '已取消',
            0.0,
            '构建已被用户取消',
            isRunning: false,
            isFailed: true,
            err: '构建已取消',
          );
          return {'success': false, 'message': '用户已取消构建操作'};
        }

        if (exitCode != 0) {
          final errMsg = lastStderr.isNotEmpty ? lastStderr : '退出码 $exitCode';
          emitProgress(
            '构建失败',
            0.0,
            '数据转换失败: $errMsg',
            isRunning: false,
            isFailed: true,
            err: errMsg,
          );
          return {'success': false, 'message': '数据转换构建失败 ($errMsg)'};
        }
      } catch (e) {
        debugPrint('[Preprocess Error] $e');
        _activePreprocessProcess = null;
        emitProgress(
          '启动失败',
          0.0,
          '未能启动构建器: $e',
          isRunning: false,
          isFailed: true,
          err: e.toString(),
        );
        return {'success': false, 'message': '未能启动构建器: $e'};
      }
    } else {
      emitProgress(
        '构建失败',
        0.0,
        '未找到预处理程序 GeoScatter3DPreprocess.exe',
        isRunning: false,
        isFailed: true,
        err: '缺少构建器',
      );
      return {
        'success': false,
        'message': '未找到预处理程序 GeoScatter3DPreprocess.exe',
      };
    }

    if (_preprocessCancelled) {
      return {'success': false, 'message': '用户已取消构建操作'};
    }

    emitProgress('挂载工区包', 0.96, '正在载入生成的金字塔工区包...');

    String? bundleToLoad = resolveDatasetPath(targetBundleDir);
    if (bundleToLoad != null && Directory(bundleToLoad).existsSync()) {
      final ok = loadDataset(bundleToLoad);
      emitProgress(
        '完成',
        1.0,
        '工区创建成功，已载入工作台',
        isRunning: false,
        isFinished: true,
      );
      return {
        'success': ok,
        'message': ok ? '工程创建并载入成功: $bundleToLoad' : '工区构建后载入失败',
        'data': ok ? _summary.toJson() : null,
      };
    }

    emitProgress(
      '构建失败',
      0.0,
      '未能生成有效的工区包目录',
      isRunning: false,
      isFailed: true,
      err: '未能生成工区包',
    );
    return {'success': false, 'message': '工程构建失败，未能生成工区包目录: $targetBundleDir'};
  }

  /// 加载数据集文件（.gs3d、.gs3d.bundle 等）
  bool loadDataset(String path) {
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
        _isWorkbenchActive = true;
        notifyListeners();
        if (wasNativeRendererActive) {
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
      list.add(RecentProjectItem(path: p, timestampUnix: ts));
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

  void setPreferredGpu(int index) {
    if (_bindings == null) return;
    _bindings!.set_preferred_gpu(index);
    _activeGpuIndex = index;
    notifyListeners();
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
            'threads': {'type': 'integer', 'description': '预处理线程数 (可选，默认 16)'},
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
    ];
  }

  /// 执行高层 UI 动作（统一供 AI、MCP 工具、控制面及前台按钮调用）
  Map<String, dynamic> executeAction(
    String actionId, [
    Map<String, dynamic>? params,
  ]) {
    final p = params ?? const {};
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
        setPreferredGpu(idx);
        return {
          'success': true,
          'message': '首选 GPU 已切换为: ${_gpus[idx].name}',
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
              'pan': '鼠标中键拖拽平移',
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
        if (cm == null || cm.isEmpty)
          return {'success': false, 'message': '缺少必要参数: colormap'};
        setColormap(cm);
        return {
          'success': true,
          'message': '色标方案已切换为: $_colormap',
          'data': {'colormap': _colormap},
        };

      case 'workbench.render.set_color_attribute':
      case 'workbench.point_cloud.set_color_attribute':
        final attr = p['attribute'] as String?;
        if (attr == null || attr.isEmpty)
          return {'success': false, 'message': '缺少必要参数: attribute'};
        setColorAttribute(attr);
        return {
          'success': true,
          'message': '着色字段已切换为: $_colorAttribute',
          'data': {'attribute': _colorAttribute},
        };

      case 'workbench.render.set_shape':
      case 'workbench.point_cloud.set_shape':
        final shape = p['shape'] as String?;
        if (shape == null || shape.isEmpty)
          return {'success': false, 'message': '缺少必要参数: shape'};
        setPointShape(shape);
        return {
          'success': true,
          'message': '散点形状已切换为: $_pointShape',
          'data': {'shape': _pointShape},
        };

      case 'workbench.render.set_height_scale':
      case 'workbench.point_cloud.set_height_scale':
        final scale = (p['scale'] as num?)?.toDouble();
        if (scale == null)
          return {'success': false, 'message': '缺少必要参数: scale'};
        setHeightScale(scale);
        return {
          'success': true,
          'message': '高度缩放倍率已更新为: ${_heightScale.toStringAsFixed(2)}x',
          'data': {'height_scale': _heightScale},
        };

      case 'workbench.render.set_height_source':
      case 'workbench.point_cloud.set_height_source':
        final source = p['source'] as String?;
        if (source == null || source.isEmpty)
          return {'success': false, 'message': '缺少必要参数: source'};
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
        final thumb = refreshMinimap(width: w, height: h);
        return {
          'success': thumb != null,
          'message': thumb != null ? '小地图缩略图已刷新' : '刷新小地图失败',
          'data': {'width': w, 'height': h, 'bytes': thumb?.length ?? 0},
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

      default:
        return {'success': false, 'message': '未知的动作 ID: $actionId'};
    }
  }

  /// 执行通用 JSON-RPC 指令（支持 MCP 动作协议与底层 C++ 引擎命令）
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
    _queueNativeRenderSettings({'point_size': size});
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
    final nativeColormap = _nativeColormapIndex(name);
    if (nativeColormap != null) {
      _queueNativeRenderSettings({'colormap_index': nativeColormap});
    }
    notifyListeners();
  }

  void setScalarRange(double minVal, double maxVal) {
    if (_bindings != null) {
      _bindings!.set_scalar_range(minVal, maxVal);
    }
    _scalarMin = minVal;
    _scalarMax = maxVal;
    _queueNativeRenderSettings({
      'value_clip_enabled': true,
      'value_clip_min': minVal,
      'value_clip_max': maxVal,
    });
    notifyListeners();
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

  void shutdown() {
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
}
