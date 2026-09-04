import 'dart:convert';
import 'dart:ffi' as ffi;
import 'dart:io';
import 'package:ffi/ffi.dart';
import 'package:flutter/foundation.dart';
import '../models/dataset_model.dart';
import '../models/gpu_info_model.dart';
import '../models/point_cloud_model.dart';
import '../models/recent_project_model.dart';
import 'geoscatter3d_bindings.dart';

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

  bool get isInitialized => _initialized;
  DatasetSummary get summary => _summary;
  List<RecentProjectItem> get recentProjects => _recentProjects;
  List<GpuDeviceInfo> get gpus => _gpus;
  int get activeGpuIndex => _activeGpuIndex;
  bool get isWorkbenchActive => _isWorkbenchActive;
  WorkbenchLayoutMode get layoutMode => _layoutMode;

  List<Point3D> get points => _points;
  double get pointSize => _pointSize;
  String get colormap => _colormap;
  double get scalarMin => _scalarMin;
  double get scalarMax => _scalarMax;

  void setLayoutMode(WorkbenchLayoutMode mode) {
    if (_layoutMode != mode) {
      _layoutMode = mode;
      notifyListeners();
    }
  }

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

  /// 新建工程：针对 CSV/DAT 原始数据源进行离线构建并载入工作台
  Map<String, dynamic> buildAndLoadProject({
    required String path,
    String? name,
    int threads = 8,
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
            'type': {'type': 'string', 'description': '过滤类型: point_cloud 或 csv'},
          },
        },
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
            'version': '2.0.0-rc1',
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
