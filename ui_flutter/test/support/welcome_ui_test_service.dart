import 'package:ui_flutter/src/ffi/geoscatter3d_service.dart';
import 'package:ui_flutter/src/models/dataset_model.dart';
import 'package:ui_flutter/src/models/gpu_info_model.dart';
import 'package:ui_flutter/src/models/recent_project_model.dart';

/// 组件测试仅核对事件与展示；原生执行由 welcome_backend_integration_test 覆盖。
class WelcomeUiTestService extends GeoScatter3dService {
  WelcomeUiTestService() : super.detached();
  String? lastAction;
  Map<String, dynamic> lastParams = {};
  bool active = false;
  int gpuIndex = 0;
  final history = <RecentProjectItem>[
    const RecentProjectItem(
      path: 'data/sample-points.gs3d.bundle',
      timestampUnix: 1,
    ),
  ];
  @override
  bool initialize([String? customDylibPath]) => false;
  @override
  bool get isWorkbenchActive => active;
  @override
  DatasetSummary get summary =>
      DatasetSummary(isLoaded: active, pointCount: active ? 25 : 0);
  @override
  List<RecentProjectItem> get recentProjects => history;
  @override
  List<GpuDeviceInfo> get gpus => const [
    GpuDeviceInfo(
      index: 0,
      name: 'Test GPU A',
      typeDescription: '独立显卡',
      isDiscrete: true,
    ),
    GpuDeviceInfo(
      index: 1,
      name: 'Test GPU B',
      typeDescription: '集成显卡',
      isDiscrete: false,
    ),
  ];
  @override
  int get activeGpuIndex => gpuIndex;
  @override
  void closeWorkbench() {
    active = false;
    notifyListeners();
  }

  @override
  void clearRecentProjects() {
    history.clear();
    notifyListeners();
  }

  @override
  void removeRecentProject(String path) {
    history.removeWhere((p) => p.path == path);
    notifyListeners();
  }

  @override
  Future<Map<String, dynamic>> executeActionAsync(
    String action, [
    Map<String, dynamic> params = const {},
  ]) async => executeAction(action, params);
  @override
  Map<String, dynamic> executeAction(
    String action, [
    Map<String, dynamic>? params,
  ]) {
    lastAction = action;
    lastParams = params ?? {};
    if ([
      'welcome.quick_demo',
      'welcome.new_project',
      'welcome.open_project',
      'welcome.recent.open',
    ].contains(action)) {
      active = true;
      if (history.isEmpty) {
        history.add(
          const RecentProjectItem(
            path: 'data/sample-points.gs3d.bundle',
            timestampUnix: 1,
          ),
        );
      }
      notifyListeners();
      return {'success': true};
    }
    if (action == 'welcome.recent.clear') {
      clearRecentProjects();
      return {'success': true};
    }
    if (action == 'welcome.gpu.set_preferred') {
      gpuIndex = lastParams['index'] as int;
      return {'success': true};
    }
    return super.executeAction(action, params);
  }
}
