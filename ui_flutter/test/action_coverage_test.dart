import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/ffi/geoscatter3d_service.dart';

/// 动作目录端到端路由覆盖。
///
/// 目标：目录中**声明**的每一个动作都必须有真实路由分支，不得落入
/// 「未知的动作 ID」默认分支，也不得抛异常或返回 null。这是「45 项
/// executeAction 全部有可观察后端结果」的自动化回归护栏。
const _dllCandidates = [
  '../tmp/build-win/src/ffi/Release/gs3d_ffi.dll',
  '../tmp/build-win/Release/gs3d_ffi.dll',
  'tmp/build-win/src/ffi/Release/gs3d_ffi.dll',
  'gs3d_ffi.dll',
  'ui_flutter/gs3d_ffi.dll',
];

const _bundleCandidates = [
  '../data/sample-points.gs3d.bundle',
  'data/sample-points.gs3d.bundle',
];

String? _firstExisting(List<String> paths, bool isDir) {
  for (final p in paths) {
    final ok = isDir ? Directory(p).existsSync() : File(p).existsSync();
    if (ok) return p;
  }
  return null;
}

/// 每个动作的合法入参。缺省动作使用空参数（处理器均有默认值）。
Map<String, Map<String, dynamic>> _params() => {
  'workbench.camera.set_view': {'preset': 'top'},
  'workbench.camera.focus_point': {'x': 0.0, 'y': 0.0, 'z': 0.0},
  'workbench.dock.toggle': {'panel': 'left', 'visible': true},
  'workbench.gizmo.axis': {'axis': 4},
  'workbench.layout.set': {'mode': 'standard'},
  'workbench.measure.add_point': {'x': 0.0, 'y': 0.0, 'z': 0.0, 'value': 1.0},
  'workbench.measure.calculate': {'type': 'distance'},
  'workbench.measure.display_mode': {'mode': '3d'},
  'workbench.measure.mode': {'enabled': true},
  'workbench.measure.remove': {'index': 0},
  'workbench.measure.toggle_fixed': {'index': 0},
  'workbench.minimap.navigate': {'world_x': 0.0, 'world_y': 0.0},
  'workbench.minimap.refresh': {'width': 120, 'height': 96},
  'workbench.panel.toggle': {'panel': 'panel.performance', 'visible': true},
  'workbench.pick.point': {'screen_x': 400.0, 'screen_y': 300.0},
  'workbench.render.set_background': {'theme': 'dark'},
  'workbench.render.set_colormap': {'colormap': 'viridis'},
  'workbench.render.set_color_attribute': {'attribute': 'elevation'},
  'workbench.render.set_height_scale': {'scale': 1.5},
  'workbench.render.set_point_size': {'size': 2.0},
  'workbench.render.set_scalar_range': {'min': 0.0, 'max': 100.0},
  'workbench.render.set_shape': {'shape': 'circle'},
  'workbench.stats.box': {
    'min_x': 0.0,
    'min_y': 0.0,
    'max_x': 100.0,
    'max_y': 100.0,
  },
  'workbench.theme.set': {'index': 1},
  'workbench.viewport.set_overlays': {
    'map_axis': true,
    'world_axis': false,
    'crosshair': true,
  },
  'welcome.recent.remove': {'index': -1},
  'welcome.gpu.set_preferred': {'index': 0},
};

void main() {
  group('动作目录端到端路由覆盖', () {
    late GeoScatter3dService service;
    final exportPath = 'tmp/action_coverage_export.ply';

    setUp(() {
      service = GeoScatter3dService();
      final dll = _firstExisting(_dllCandidates, false);
      if (dll != null) {
        service.initialize(dll);
      } else {
        service.initialize();
      }
      service.openWorkbench();
      final bundle = _firstExisting(_bundleCandidates, true);
      if (bundle != null) service.loadDataset(bundle);
    });

    tearDown(() {
      // R4：测试临时产物必须自清理，保持工作树干净。
      final f = File(exportPath);
      if (f.existsSync()) f.deleteSync();
      service.shutdown();
    });

    test('目录中每个动作都有真实路由分支（无未知动作 / 无崩溃）', () {
      final catalog = service.getAvailableActions();
      expect(
        catalog.length,
        greaterThanOrEqualTo(45),
        reason: '动作目录规模异常，应包含全部原子 UI 动作',
      );

      final params = _params();
      final unrouted = <String>[];
      final crashed = <String>[];
      final badShape = <String>[];

      for (final action in catalog) {
        final arg = Map<String, dynamic>.from(params[action.id] ?? const {});
        if (action.id == 'workbench.export') {
          arg.addAll({'path': exportPath, 'format': 'ply'});
        }
        try {
          final res = service.executeAction(action.id, arg);
          if (res == null) {
            crashed.add('${action.id}: 返回 null');
            continue;
          }
          if (!res.containsKey('success')) {
            badShape.add('${action.id}: 缺少 success 字段');
          }
          final message = res['message'] as String? ?? '';
          if (message.startsWith('未知的动作 ID')) {
            unrouted.add(action.id);
          }
        } catch (error) {
          crashed.add('${action.id}: $error');
        }
      }

      expect(unrouted, isEmpty, reason: '声明但未实现路由的动作: $unrouted');
      expect(crashed, isEmpty, reason: '执行抛异常/返回 null 的动作: $crashed');
      expect(badShape, isEmpty, reason: '返回结构不合规的动作: $badShape');
    });

    test('数据依赖动作在真实后端下返回可观察结果', () {
      // 属性统计：原生全量区域统计路径。
      final stats = service.executeAction('workbench.stats.get');
      expect(stats['success'], isTrue);
      expect((stats['data'] as Map)['histogram'], hasLength(5));

      // 选区统计：原生 calculate_region_stats。
      final box = service.executeAction('workbench.stats.box', {
        'min_x': -1e9,
        'min_y': -1e9,
        'max_x': 1e9,
        'max_y': 1e9,
      });
      expect(box['success'], isTrue);
      expect((box['data'] as Map)['count'], isA<num>());

      // 测量：加入两点后计算真实距离。
      service.executeAction('workbench.measure.add_point', {
        'x': 0.0,
        'y': 0.0,
        'z': 0.0,
        'value': 1.0,
      });
      service.executeAction('workbench.measure.add_point', {
        'x': 3.0,
        'y': 4.0,
        'z': 0.0,
        'value': 2.0,
      });
      final measure = service.executeAction('workbench.measure.calculate', {
        'type': 'distance',
      });
      expect(measure['success'], isTrue);
      expect(
        ((measure['data'] as Map)['distance_3d'] as num).toDouble(),
        closeTo(5.0, 1e-6),
      );

      // 导出：真实写出文件。
      final export = service.executeAction('workbench.export', {
        'path': exportPath,
        'format': 'ply',
      });
      expect(export['success'], isTrue);
      expect(File(exportPath).existsSync(), isTrue);
      expect(File(exportPath).lengthSync(), greaterThan(0));
    });
  });
}
