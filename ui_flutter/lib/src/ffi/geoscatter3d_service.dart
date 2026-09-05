import 'dart:async';
import 'dart:convert';
import 'dart:ffi' as ffi;
import 'dart:io';
import 'dart:math' as math;
import 'package:ffi/ffi.dart';
import 'package:flutter/material.dart';
import '../models/dataset_model.dart';
import '../models/gpu_info_model.dart';
import '../models/point_cloud_model.dart';
import '../models/recent_project_model.dart';
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

  bool get leftDockVisible => _leftDockVisible;
  bool get rightDockVisible => _rightDockVisible;

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
      notifyListeners();
    }
  }

  void setHeightScale(double scale) {
    final clamped = scale.clamp(0.1, 10.0);
    if (_heightScale != clamped) {
      _heightScale = clamped;
      notifyListeners();
    }
  }

  void setHeightSource(String source) {
    if (_heightSource != source) {
      _heightSource = source;
      notifyListeners();
    }
  }

  void setColorAttribute(String attr) {
    if (_colorAttribute != attr) {
      _colorAttribute = attr;
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
    final dip = (diagonal > 0) ? (math.asin((dz / diagonal).clamp(-1.0, 1.0)) * 180 / math.pi) : 0.0;

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
    if (onScreenshotRequested != null) {
      onScreenshotRequested!();
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
        candidates.add('${exeParent.parent.parent.parent.parent.path}/$trimmed');
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
    final candidates = [
      'D:/code/GeoScatter3D',
      '.',
      '..',
      '../..',
    ];
    for (final c in candidates) {
      if (File('$c/CMakeLists.txt').existsSync() && Directory('$c/config').existsSync()) {
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
    final projName = (name != null && name.trim().isNotEmpty) ? name.trim() : baseNameNoExt;
    final repoRoot = resolveRepoRoot();
    final targetBundleDir = '$repoRoot/data/$projName.gs3d.bundle';

    final preprocessExe = resolvePreprocessExecutable();
    final configPath = resolveDatasetPath('config/sample-viewer.toml') ?? '$repoRoot/config/sample-viewer.toml';

    if (preprocessExe != null && File(preprocessExe).existsSync()) {
      try {
        final result = Process.runSync(
          preprocessExe,
          [
            '--config',
            configPath,
            '--csv',
            resolvedSource,
            '--bundle',
            targetBundleDir,
          ],
          workingDirectory: repoRoot,
        );
        if (result.exitCode != 0) {
          debugPrint('[Preprocess Warning] Process exit ${result.exitCode}: ${result.stderr}');
        }
      } catch (e) {
        debugPrint('[Preprocess Error] $e');
      }
    }

    // 优先尝试载入刚刚生成的工区包，如果未生成则回退到备选工区
    String? bundleToLoad = resolveDatasetPath(targetBundleDir);
    if (bundleToLoad == null || !Directory(bundleToLoad).existsSync()) {
      bundleToLoad = resolveDatasetPath('data/sample-points.gs3d.bundle');
    }

    if (bundleToLoad != null) {
      final ok = loadDataset(bundleToLoad);
      return {
        'success': ok,
        'message': ok ? '工程创建并载入成功: $bundleToLoad' : '工区构建后载入失败',
        'data': ok ? _summary.toJson() : null,
      };
    }

    return {'success': false, 'message': '工程构建失败，未能生成工区包'};
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
    final projName = (name != null && name.trim().isNotEmpty) ? name.trim() : baseNameNoExt;
    final repoRoot = resolveRepoRoot();
    final targetBundleDir = '$repoRoot/data/$projName.gs3d.bundle';

    final preprocessExe = resolvePreprocessExecutable();
    final configPath = resolveDatasetPath('config/sample-viewer.toml') ?? '$repoRoot/config/sample-viewer.toml';

    final stopwatch = Stopwatch()..start();

    void emitProgress(String stage, double p, String detail, {bool isRunning = true, bool isFinished = false, bool isFailed = false, String? err}) {
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

    if (preprocessExe != null && File(preprocessExe).existsSync()) {
      try {
        final process = await Process.start(
          preprocessExe,
          [
            '--config',
            configPath,
            '--csv',
            resolvedSource,
            '--bundle',
            targetBundleDir,
          ],
          workingDirectory: repoRoot,
        );
        _activePreprocessProcess = process;
        notifyListeners();

        final sub = process.stdout
            .transform(utf8.decoder)
            .transform(const LineSplitter())
            .listen((line) {
          if (_preprocessCancelled) return;
          final clean = line.trim();
          if (clean.isEmpty) return;

          String stage = '正在处理散点数据';
          double p = 0.45;
          if (clean.contains('Reading') || clean.contains('Parsing') || clean.contains('csv') || clean.contains('points')) {
            stage = '阶段 1/3: 解析点云源数据';
            p = 0.35;
          } else if (clean.contains('grid') || clean.contains('voxel') || clean.contains('Spatial') || clean.contains('tile')) {
            stage = '阶段 2/3: 空间剖分与体素重排';
            p = 0.70;
          } else if (clean.contains('lod') || clean.contains('LOD') || clean.contains('pyramid') || clean.contains('bundle')) {
            stage = '阶段 3/3: 构建八叉树 LOD 金字塔';
            p = 0.90;
          }
          emitProgress(stage, p, clean);
        });

        final exitCode = await process.exitCode;
        await sub.cancel();
        _activePreprocessProcess = null;

        if (_preprocessCancelled) {
          emitProgress('已取消', 0.0, '构建已被用户取消', isRunning: false, isFailed: true, err: '构建已取消');
          return {'success': false, 'message': '用户已取消构建操作'};
        }

        if (exitCode != 0) {
          debugPrint('[Preprocess Warning] Process exit $exitCode');
        }
      } catch (e) {
        debugPrint('[Preprocess Error] $e');
        _activePreprocessProcess = null;
      }
    }

    if (_preprocessCancelled) {
      return {'success': false, 'message': '用户已取消构建操作'};
    }

    emitProgress('挂载工区包', 0.96, '正在载入生成的金字塔工区包...');

    String? bundleToLoad = resolveDatasetPath(targetBundleDir);
    if (bundleToLoad == null || !Directory(bundleToLoad).existsSync()) {
      bundleToLoad = resolveDatasetPath('data/sample-points.gs3d.bundle');
    }

    if (bundleToLoad != null) {
      final ok = loadDataset(bundleToLoad);
      emitProgress('完成', 1.0, '工区创建成功，已载入工作台', isRunning: false, isFinished: true);
      return {
        'success': ok,
        'message': ok ? '工程创建并载入成功: $bundleToLoad' : '工区构建后载入失败',
        'data': ok ? _summary.toJson() : null,
      };
    }

    emitProgress('构建失败', 0.0, '未能生成有效的工区包目录', isRunning: false, isFailed: true, err: '未能生成工区包');
    return {'success': false, 'message': '工程构建失败，未能生成工区包'};
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
        _updateSummary();
        _updatePoints();
        _refreshRecentProjects();
        _isWorkbenchActive = true;
        notifyListeners();
        return true;
      }
      return false;
    } finally {
      calloc.free(nativePath);
    }
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
        pts.add(Point3D(
          x: buf[offset],
          y: buf[offset + 1],
          z: buf[offset + 2],
          value: buf[offset + 3],
        ));
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
      list.add(GpuDeviceInfo(
        index: i,
        name: name,
        typeDescription: type,
        isDiscrete: type.contains('独立'),
      ));
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
        description: '自动解析并载入内置 25 测点 5 级八叉树 LOD 示例点云数据包 (sample-points.gs3d.bundle) 并进入三维工作台',
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
            'path': {
              'type': 'string',
              'description': '数据集绝对路径或相对工程根目录路径',
            },
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
            'type': {'type': 'string', 'description': '过滤类型: point_cloud, csv 或 open_project'},
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
            'preset': {'type': 'string', 'enum': ['top', 'front', 'side', 'iso'], 'description': '预设视角名称'},
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
            'shape': {'type': 'string', 'enum': ['方形', '圆形'], 'description': '图元形状'},
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
            'theme': {'type': 'string', 'enum': ['dark', 'light', 'black'], 'description': '视口背景主题'},
          },
          'required': ['theme'],
        },
      ),
      UiActionDescriptor(
        id: 'workbench.layout.set',
        name: '切换工作台布局模式',
        description: '切换主工作台界面布局 (standard: 标准三栏, floatingDock: 悬浮胶囊Dock, analysisRail: 暗色分析舱)',
        category: 'workbench',
        parameterSchema: {
          'type': 'object',
          'properties': {
            'mode': {'type': 'string', 'enum': ['standard', 'floatingDock', 'analysisRail'], 'description': '布局模式标识'},
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
            'panel': {'type': 'string', 'enum': ['left', 'right', 'both'], 'description': '面板名称'},
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
            'type': {'type': 'string', 'enum': ['distance', 'area', 'strike_dip'], 'description': '测量类型'},
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
    ];
  }

  /// 执行高层 UI 动作（统一供 AI、MCP 工具、控制面及前台按钮调用）
  Map<String, dynamic> executeAction(String actionId, [Map<String, dynamic>? params]) {
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
          'message': (picked != null && picked.isNotEmpty) ? '已选择文件: $picked' : '已取消或未选择文件',
          'data': {'path': picked},
        };

      case 'welcome.browse_folder':
        final picked = pickFolder();
        return {
          'success': picked != null && picked.isNotEmpty,
          'message': (picked != null && picked.isNotEmpty) ? '已选择目录: $picked' : '已取消或未选择目录',
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
        final ok = loadDataset(resolved);
        return {
          'success': ok,
          'message': ok ? '成功打开工程: $resolved' : '工程加载失败',
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
          'data': {
            'path': path,
            'is_workbench_active': _isWorkbenchActive,
          },
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
            'active_gpu': (_activeGpuIndex >= 0 && _activeGpuIndex < _gpus.length)
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
        if (cm == null || cm.isEmpty) return {'success': false, 'message': '缺少必要参数: colormap'};
        setColormap(cm);
        return {
          'success': true,
          'message': '色标方案已切换为: $_colormap',
          'data': {'colormap': _colormap},
        };

      case 'workbench.render.set_color_attribute':
      case 'workbench.point_cloud.set_color_attribute':
        final attr = p['attribute'] as String?;
        if (attr == null || attr.isEmpty) return {'success': false, 'message': '缺少必要参数: attribute'};
        setColorAttribute(attr);
        return {
          'success': true,
          'message': '着色字段已切换为: $_colorAttribute',
          'data': {'attribute': _colorAttribute},
        };

      case 'workbench.render.set_shape':
      case 'workbench.point_cloud.set_shape':
        final shape = p['shape'] as String?;
        if (shape == null || shape.isEmpty) return {'success': false, 'message': '缺少必要参数: shape'};
        setPointShape(shape);
        return {
          'success': true,
          'message': '散点形状已切换为: $_pointShape',
          'data': {'shape': _pointShape},
        };

      case 'workbench.render.set_height_scale':
      case 'workbench.point_cloud.set_height_scale':
        final scale = (p['scale'] as num?)?.toDouble();
        if (scale == null) return {'success': false, 'message': '缺少必要参数: scale'};
        setHeightScale(scale);
        return {
          'success': true,
          'message': '高度缩放倍率已更新为: ${_heightScale.toStringAsFixed(2)}x',
          'data': {'height_scale': _heightScale},
        };

      case 'workbench.render.set_height_source':
      case 'workbench.point_cloud.set_height_source':
        final source = p['source'] as String?;
        if (source == null || source.isEmpty) return {'success': false, 'message': '缺少必要参数: source'};
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
        if (theme == 'light' || colorStr == '#f0f2f5' || colorStr == '#ffffff') {
          bg = const Color(0xFFF0F2F5);
        } else if (theme == 'black' || colorStr == '#000000' || colorStr == 'black') {
          bg = Colors.black;
        } else if (colorStr != null && colorStr.startsWith('#') && colorStr.length == 7) {
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
        final panel = isLeft ? 'left' : (isRight ? 'right' : ((p['panel'] as String?)?.toLowerCase() ?? 'left'));
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
        return {
          'success': true,
          'message': '空间几何指标推导完成',
          'data': m,
        };

      case 'workbench.stats.get':
      case 'workbench.stats.calculate':
        final stats = calculateStats();
        return {
          'success': true,
          'message': '属性统计指标计算完成',
          'data': stats,
        };

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

      default:
        return {
          'success': false,
          'message': '未知的动作 ID: $actionId',
        };
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
        return jsonEncode({
          'jsonrpc': '2.0',
          'id': id,
          'result': res,
        });
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
    if (_bindings == null) return;
    _bindings!.set_point_size(size);
    _pointSize = size;
    notifyListeners();
  }

  void setColormap(String name) {
    if (_bindings == null) return;
    final nativeStr = name.toNativeUtf8();
    try {
      _bindings!.set_colormap(nativeStr);
      _colormap = name;
      notifyListeners();
    } finally {
      calloc.free(nativeStr);
    }
  }

  void setScalarRange(double minVal, double maxVal) {
    if (_bindings == null) return;
    _bindings!.set_scalar_range(minVal, maxVal);
    _scalarMin = minVal;
    _scalarMax = maxVal;
    notifyListeners();
  }

  void shutdown() {
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
