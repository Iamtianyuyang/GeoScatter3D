import 'dart:convert';
import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';

import '../ui_flutter/lib/src/ffi/geoscatter3d_bindings.dart';
import '../ui_flutter/lib/src/native/viewer_control_client.dart';

/// FCI 统一通道烟测：ViewerBridge 启动 → 类型化 FFI → 截图 → 导出。
/// 产物写入 tmp/ 并自清理。
Future<void> main(List<String> args) async {
  if (args.length != 1) {
    throw ArgumentError(
      'Usage: dart scripts/smoke_ffi_channel.dart <runtime-directory>',
    );
  }
  final runtime = Directory(args.single).absolute;
  final root = File.fromUri(Platform.script).parent.parent;
  final tempRoot = Directory('${root.path}/tmp')..createSync(recursive: true);
  final temp = tempRoot.createTempSync('ffi-channel-smoke-');
  final client = NativeViewerControlClient();
  GeoScatter3dBindings? bindings;

  try {
    final bundle = '${temp.path}/sample.gs3d.bundle';
    final preprocess = await Process.run(
      '${runtime.path}/GeoScatter3DPreprocess.exe',
      [
        '--config',
        '${runtime.path}/config/sample-viewer.toml',
        '--csv',
        '${root.path}/examples/sample-points.csv',
        '--bundle',
        bundle,
      ],
      workingDirectory: temp.path,
    );
    if (preprocess.exitCode != 0) {
      throw StateError('Preprocessing failed: ${preprocess.stderr}');
    }

    // 复制 DLL 到临时目录旁侧，保证 openLibrary 能加载带 Viewer 桥符号的库。
    final dllSource = File('${runtime.path}/gs3d_ffi.dll');
    if (!dllSource.existsSync()) {
      throw StateError('gs3d_ffi.dll not found in ${runtime.path}');
    }
    bindings = GeoScatter3dBindings.openLibrary();
    if (!bindings.hasViewerBridge) {
      throw StateError('gs3d_ffi.dll missing Viewer bridge symbols');
    }

    if (!await client.start(
      executablePath: '${runtime.path}/GeoScatter3D.exe',
      datasetPath: bundle,
    )) {
      throw StateError('Native viewer did not start: ${client.startupDiagnostics}');
    }

    // 类型化 FFI：渲染参数 / 相机 / 主题 / 面板 / 诊断
    final setRender = bindings.set_render_settings_json_opt;
    if (setRender == null) throw StateError('missing set_render_settings_json');
    final params = '{"point_size":4.5,"colormap_index":6}'.toNativeUtf8();
    try {
      final rc = setRender(params);
      if (rc != 0) throw StateError('set_render_settings_json rc=$rc');
    } finally {
      calloc.free(params);
    }

    final resetCam = bindings.reset_camera_opt;
    if (resetCam == null) throw StateError('missing reset_camera');
    if (resetCam() != 0) throw StateError('reset_camera failed');

    final camIn = bindings.camera_input_opt;
    if (camIn == null) throw StateError('missing camera_input');
    camIn(12.0, 4.0, 0, 0, 0);

    final setTheme = bindings.set_theme_opt;
    if (setTheme == null) throw StateError('missing set_theme');
    setTheme(1);

    final setPanel = bindings.set_panel_visible_opt;
    if (setPanel == null) throw StateError('missing set_panel_visible');
    final panelId = 'panel.performance'.toNativeUtf8();
    try {
      setPanel(panelId, 1);
    } finally {
      calloc.free(panelId);
    }

    // 快照与诊断 JSON
    final snapFn = bindings.get_interaction_snapshot_json_opt;
    final diagFn = bindings.get_runtime_diagnostics_json_opt;
    if (snapFn == null || diagFn == null) {
      throw StateError('missing snapshot/diagnostics FFI');
    }
    final snapRaw = snapFn().toDartString();
    final diagRaw = diagFn().toDartString();
    final snap = jsonDecode(snapRaw) as Map<String, dynamic>;
    final diag = jsonDecode(diagRaw) as Map<String, dynamic>;
    if (snap['ready'] != true) {
      throw StateError('interaction snapshot not ready: $snapRaw');
    }

    // 帧同步截图
    final png = await client.capturePng();
    if (png.length < 8 ||
        png.take(8).join(',') != '137,80,78,71,13,10,26,10' ||
        client.lastCaptureWidth <= 0 ||
        client.lastCaptureHeight <= 0) {
      throw StateError('Invalid native viewport PNG');
    }

    // 类型化导出
    final exportPath = '${temp.path}/export_ffi.ply';
    final exportFn = bindings.export_via_viewer_opt;
    if (exportFn == null) throw StateError('missing export_via_viewer');
    final pOut = exportPath.toNativeUtf8();
    final pFmt = 'ply'.toNativeUtf8();
    try {
      final rc = exportFn(pOut, pFmt);
      if (rc != 0) throw StateError('export_via_viewer rc=$rc');
    } finally {
      calloc.free(pOut);
      calloc.free(pFmt);
    }
    final exported = File(exportPath);
    if (!exported.existsSync() || exported.lengthSync() < 32) {
      throw StateError('export file missing or empty');
    }

    // list_components
    final listFn = bindings.list_components_json_opt;
    if (listFn == null) throw StateError('missing list_components');
    final components = jsonDecode(listFn().toDartString()) as Map<String, dynamic>;
    final list = (components['components'] as List?) ?? const [];
    if (list.isEmpty) throw StateError('empty component registry');

    stdout.writeln(
      jsonEncode({
        'result': 'OK',
        'png_bytes': png.length,
        'width': client.lastCaptureWidth,
        'height': client.lastCaptureHeight,
        'export_bytes': exported.lengthSync(),
        'components': list.length,
        'snapshot_ready': snap['ready'],
        'diag_keys': diag.keys.toList(),
      }),
    );
  } finally {
    await client.stop();
    bindings?.shutdown();
    try {
      temp.deleteSync(recursive: true);
    } catch (_) {}
  }
}
