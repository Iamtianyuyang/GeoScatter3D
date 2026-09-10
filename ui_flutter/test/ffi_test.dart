import 'dart:io';
import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/ffi/geoscatter3d_bindings.dart';
import 'package:ui_flutter/src/ffi/geoscatter3d_service.dart';

const _dllCandidates = [
  // 优先仓库最新构建产物（避免 ui_flutter/ 下的历史部署副本掩盖新符号）。
  '../tmp/build-win/src/ffi/Release/gs3d_ffi.dll',
  '../tmp/build-win/Release/gs3d_ffi.dll',
  'tmp/build-win/src/ffi/Release/gs3d_ffi.dll',
  'gs3d_ffi.dll',
  'ui_flutter/gs3d_ffi.dll',
  'D:/code/GeoScatter3D/tmp/build-win/src/ffi/Release/gs3d_ffi.dll',
];

const _bundleCandidates = [
  '../data/sample-points.gs3d.bundle',
  'data/sample-points.gs3d.bundle',
  'D:/code/GeoScatter3D/data/sample-points.gs3d.bundle',
];

String? _findDll() {
  for (final p in _dllCandidates) {
    if (File(p).existsSync()) return p;
  }
  return null;
}

String? _findBundle() {
  for (final b in _bundleCandidates) {
    if (Directory(b).existsSync()) return b;
  }
  return null;
}

void main() {
  test('GeoScatter3D FFI initialize and load dataset test', () {
    final service = GeoScatter3dService();

    // 寻找 dll
    final foundDll = _findDll();
    expect(foundDll, isNotNull, reason: 'gs3d_ffi.dll must exist');

    final inited = service.initialize(foundDll);
    expect(inited, isTrue);

    // 测试 JSON-RPC 通信
    final pingResp = service.executeJsonRpc('{"jsonrpc":"2.0","id":1,"method":"ping"}');
    expect(pingResp, contains('pong'));

    // 测试加载真实 bundle
    final foundBundle = _findBundle();
    expect(foundBundle, isNotNull, reason: 'sample-points bundle must exist');

    final loaded = service.loadDataset(foundBundle!);
    expect(loaded, isTrue);

    final summary = service.summary;
    expect(summary.isLoaded, isTrue);
    expect(summary.name, equals('sample-points'));
    expect(summary.pointCount, equals(25));
    expect(summary.formattedPointCount, equals('25 点'));
    expect(summary.attributes, contains('field_statics'));
    expect(summary.attributes, contains('elevation'));

    service.shutdown();
  });

  test('单进程内核符号覆盖：create / unload / pick / gizmo / png 均可绑定', () {
    final dll = _findDll();
    expect(dll, isNotNull, reason: 'gs3d_ffi.dll must exist');
    final b = GeoScatter3dBindings.openLibrary(dll);

    // 完整 create 与 unload 必须存在（此前未绑定）。
    expect(b.engine_create_opt, isNotNull, reason: 'gs3d_engine_create 未导出');
    expect(
      b.engine_unload_dataset_opt,
      isNotNull,
      reason: 'gs3d_engine_unload_dataset 未导出',
    );
    // 交互与帧回读能力。
    expect(b.engine_pick_point_opt, isNotNull);
    expect(b.engine_gizmo_axis_opt, isNotNull);
    expect(b.engine_read_frame_png_opt, isNotNull);
    expect(b.engine_clear_tile_cache_opt, isNotNull);

    // hasEngineApi 要求 create/load/render/camera 四项齐全。
    expect(b.hasEngineApi, isTrue);
  });

  test('calculateStats 优先走原生全量区域统计', () {
    final service = GeoScatter3dService();
    final dll = _findDll();
    expect(dll, isNotNull);
    expect(service.initialize(dll), isTrue);

    final reloaded = service.loadDataset(_findBundle()!);
    expect(reloaded, isTrue);

    final stats = service.calculateStats();
    expect(stats['source'], equals('native_region_stats'));
    expect((stats['count'] as num).toInt(), equals(25));
    expect((stats['histogram'] as List).length, equals(5));

    // 动作路由与直接调用必须一致（同一后端路径）。
    final viaAction =
        service.executeAction('workbench.stats.get')['data'] as Map;
    expect(viaAction['source'], equals('native_region_stats'));

    service.shutdown();
  });

  test('无引擎时截图接口显式降级而非静默伪成功', () {
    final service = GeoScatter3dService();
    expect(service.isEngineNative, isFalse);
    // 未启动 engine：引擎帧 PNG 路径应返回 null。
    expect(service.captureEngineFramePng(), completion(isNull));
  });
}

