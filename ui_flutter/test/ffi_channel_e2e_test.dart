import 'dart:convert';
import 'dart:io';

import 'package:ffi/ffi.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/ffi/geoscatter3d_bindings.dart';
import 'package:ui_flutter/src/native/viewer_control_client.dart';

/// 由 scripts/test_welcome_backend.ps1 或 GS3D_FFI_E2E=1 显式启用。
/// 覆盖：ViewerBridge 启动 → 类型化 FFI 渲染/相机/主题/面板 → 快照 → 截图 → 导出。
void main() {
  final runtimeDir =
      Platform.environment['GS3D_RUNTIME_DIR'] ??
      '${Directory.current.parent.path}${Platform.pathSeparator}tmp${Platform.pathSeparator}build-win${Platform.pathSeparator}Release';

  test('FFI unified channel e2e with real Viewer', () async {
    if (!Platform.isWindows) {
      markTestSkipped('Windows only');
      return;
    }
    if (Platform.environment['GS3D_FFI_E2E'] != '1') {
      markTestSkipped('Set GS3D_FFI_E2E=1 to run native FFI e2e');
      return;
    }
    final runtime = Directory(runtimeDir).absolute;
    final exe = File(
      '${runtime.path}${Platform.pathSeparator}GeoScatter3D.exe',
    );
    final dll = File(
      '${runtime.path}${Platform.pathSeparator}gs3d_ffi.dll',
    );
    final bundleDir = Directory(
      '${Directory.current.parent.path}${Platform.pathSeparator}data${Platform.pathSeparator}sample-points.gs3d.bundle',
    );
    expect(exe.existsSync(), isTrue, reason: exe.path);
    expect(dll.existsSync(), isTrue, reason: dll.path);
    expect(bundleDir.existsSync(), isTrue, reason: bundleDir.path);

    final bindings = GeoScatter3dBindings.openLibrary(dll.path);
    expect(bindings.hasViewerBridge, isTrue);

    final client = NativeViewerControlClient();
    addTearDown(() async {
      await client.stop();
      bindings.shutdown();
    });

    final started = await client.start(
      executablePath: exe.path,
      datasetPath: bundleDir.path,
    );
    expect(started, isTrue, reason: client.startupDiagnostics);

    // 类型化渲染参数
    final setRender = bindings.set_render_settings_json_opt!;
    final paramsPtr = '{"point_size":4.5,"colormap_index":6}'.toNativeUtf8();
    try {
      expect(setRender(paramsPtr), 0);
    } finally {
      calloc.free(paramsPtr);
    }

    expect(bindings.reset_camera_opt!(), 0);
    bindings.camera_input_opt!(12.0, 4.0, 0, 0, 0);
    bindings.set_theme_opt!(1);

    final panelPtr = 'panel.performance'.toNativeUtf8();
    try {
      bindings.set_panel_visible_opt!(panelPtr, 1);
    } finally {
      calloc.free(panelPtr);
    }

    final snapRaw = bindings.get_interaction_snapshot_json_opt!().toDartString();
    final snap = jsonDecode(snapRaw) as Map<String, dynamic>;
    expect(snap['ready'], isTrue, reason: snapRaw);

    final png = await client.capturePng();
    expect(png.length, greaterThan(8));
    expect(png.take(8).toList(), equals([137, 80, 78, 71, 13, 10, 26, 10]));
    expect(client.lastCaptureWidth, greaterThan(0));
    expect(client.lastCaptureHeight, greaterThan(0));

    final exportPath =
        '${Directory.systemTemp.path}${Platform.pathSeparator}gs3d_ffi_e2e_export.ply';
    // 生产路径：异步 execute + 轮询，避免在 isolate 上调用阻塞式 export_via_viewer。
    await client.executeComponent('runtime.dataset_export', 'export', {
      'output_path': exportPath,
      'format': 'ply',
    });
    var exportOk = false;
    final exportDeadline = DateTime.now().add(const Duration(seconds: 60));
    while (DateTime.now().isBefore(exportDeadline)) {
      final st = await client.request('get_state', {
        'component': 'runtime.dataset_export',
      });
      final s = st['result'];
      if (s is Map<String, dynamic> && s['in_progress'] != true) {
        exportOk = s['success'] == true;
        break;
      }
      await Future<void>.delayed(const Duration(milliseconds: 100));
    }
    expect(exportOk, isTrue);
    final exported = File(exportPath);
    expect(exported.existsSync(), isTrue);
    expect(exported.lengthSync(), greaterThan(32));
    exported.deleteSync();

    final components =
        jsonDecode(bindings.list_components_json_opt!().toDartString())
            as Map<String, dynamic>;
    expect((components['components'] as List).length, greaterThan(10));

    final diag =
        jsonDecode(
              bindings.get_runtime_diagnostics_json_opt!().toDartString(),
            )
            as Map<String, dynamic>;
    expect(diag.containsKey('id'), isTrue);
  }, timeout: const Timeout(Duration(seconds: 90)));
}
