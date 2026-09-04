import 'dart:ffi' as ffi;
import 'package:ffi/ffi.dart';
import 'package:flutter/foundation.dart';
import '../models/dataset_model.dart';
import '../models/gpu_info_model.dart';
import '../models/point_cloud_model.dart';
import '../models/recent_project_model.dart';
import 'geoscatter3d_bindings.dart';

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

  List<Point3D> _points = [];
  double _pointSize = 1.5;
  String _colormap = 'viridis';
  double _scalarMin = 0.0;
  double _scalarMax = 100.0;

  bool get isInitialized => _initialized;
  DatasetSummary get summary => _summary;
  List<RecentProjectItem> get recentProjects => _recentProjects;
  List<GpuDeviceInfo> get gpus => _gpus;
  int get activeGpuIndex => _activeGpuIndex;
  bool get isWorkbenchActive => _isWorkbenchActive;

  List<Point3D> get points => _points;
  double get pointSize => _pointSize;
  String get colormap => _colormap;
  double get scalarMin => _scalarMin;
  double get scalarMax => _scalarMax;

  void openWorkbench() {
    _isWorkbenchActive = true;
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
      }
      notifyListeners();
      return _initialized;
    } catch (e) {
      debugPrint('[GeoScatter3D FFI] Failed to initialize: $e');
      return false;
    }
  }

  /// 加载数据集文件（.gs3d、.gs3d.bundle 等）
  bool loadDataset(String path) {
    if (!_initialized || _bindings == null) {
      if (!initialize()) return false;
    }

    final nativePath = path.toNativeUtf8();
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

  /// 执行通用 JSON-RPC 指令
  String executeJsonRpc(String requestJson) {
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
