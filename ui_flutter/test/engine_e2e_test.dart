import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/ffi/geoscatter3d_bindings.dart';

/// 单进程渲染内核 e2e。设置 GS3D_ENGINE_E2E=1 启用。
/// 覆盖 T7a/T7b：create→load→render、export、destroy、重建无泄漏。
void main() {
  late String runtime;
  late String sampleSource;
  late Directory exportDir;

  setUpAll(() {
    if (Platform.environment['GS3D_ENGINE_E2E'] != '1') {
      return;
    }
    if (!Platform.isWindows) return;
    runtime =
        Platform.environment['GS3D_RUNTIME_DIR'] ??
        '${Directory.current.parent.path}${Platform.pathSeparator}tmp${Platform.pathSeparator}build-win${Platform.pathSeparator}src${Platform.pathSeparator}ffi${Platform.pathSeparator}Release';
    sampleSource =
        '${Directory.current.parent.path}${Platform.pathSeparator}data${Platform.pathSeparator}sample-points.gs3d.bundle${Platform.pathSeparator}source.gs3d';
    exportDir = Directory(
      '${Directory.current.parent.path}${Platform.pathSeparator}tmp${Platform.pathSeparator}engine-e2e-export',
    );
  });

  test('single-process gs3d_engine full gate (render/export/destroy)', () {
    if (Platform.environment['GS3D_ENGINE_E2E'] != '1') {
      markTestSkipped('Set GS3D_ENGINE_E2E=1 to run engine e2e');
      return;
    }
    if (!Platform.isWindows) {
      markTestSkipped('Windows only');
      return;
    }

    final dll = '$runtime${Platform.pathSeparator}gs3d_ffi.dll';
    expect(File(dll).existsSync(), isTrue, reason: dll);

    // Windows 依赖搜索：让 gs3d_ffi 找到同目录 glfw3.dll 等
    final kernel32 = DynamicLibrary.open('kernel32.dll');
    final setDllDirA = kernel32.lookupFunction<
      Int32 Function(Pointer<Utf8>),
      int Function(Pointer<Utf8>)
    >('SetDllDirectoryA');
    final dirPtr = runtime.toNativeUtf8();
    try {
      final rc = setDllDirA(dirPtr);
      expect(rc, isNot(equals(0)), reason: 'SetDllDirectoryA failed');
    } finally {
      calloc.free(dirPtr);
    }

    final bindings = GeoScatter3dBindings.openLibrary(dll);
    expect(bindings.hasEngineApi, isTrue, reason: 'engine symbols missing');
    expect(
      bindings.engine_export_opt,
      isNotNull,
      reason: 'engine_export symbol missing (T7b)',
    );

    // shader 默认相对路径 assets/shaders；切到 runtime 目录加载
    final prevCwd = Directory.current;
    Directory.current = Directory(runtime);
    addTearDown(() => Directory.current = prevCwd);

    if (exportDir.existsSync()) {
      try {
        exportDir.deleteSync(recursive: true);
      } catch (_) {}
    }
    exportDir.createSync(recursive: true);
    addTearDown(() {
      if (exportDir.existsSync()) {
        try {
          exportDir.deleteSync(recursive: true);
        } catch (_) {}
      }
    });

    final create = bindings.engine_create_simple_opt!;
    var engineLive = true;
    final engine = create(256, 256);
    expect(engine.address, isNot(equals(0)),
        reason: bindings.engine_last_global_error_opt?.call().toDartString() ?? '');
    addTearDown(() {
      if (engineLive) {
        engineLive = false;
        bindings.engine_destroy_opt?.call(engine);
      }
    });

    expect(File(sampleSource).existsSync(), isTrue, reason: sampleSource);
    final pathPtr = sampleSource.toNativeUtf8();
    try {
      expect(bindings.engine_load_dataset_opt!(engine, pathPtr), 0);
    } finally {
      calloc.free(pathPtr);
    }

    bindings.engine_set_point_size_opt!(engine, 3.0);
    bindings.engine_reset_camera_opt!(engine);
    // 渲染外观设置应成功（T3 附带路径）
    expect(bindings.engine_set_height_source_opt?.call(engine, 1), 0);
    expect(bindings.engine_set_color_source_opt?.call(engine, 1), 0);
    expect(bindings.engine_set_height_exaggeration_opt?.call(engine, 1.5), 0);
    expect(bindings.engine_set_background_opt?.call(engine, 0.1, 0.1, 0.1, 1.0), 0);
    expect(bindings.engine_camera_input_opt?.call(engine, 0.2, 0.1, 0, 0, 0), 0);

    // --- T7a: render ---
    final w = calloc<Uint32>();
    final h = calloc<Uint32>();
    final buf = calloc<Uint8>(256 * 256 * 4);
    try {
      final rc = bindings.engine_render_rgba_opt!(
        engine,
        buf,
        256 * 256 * 4,
        w,
        h,
      );
      expect(rc, 0, reason: bindings.engine_last_error_opt?.call(engine).toDartString() ?? '');
      expect(w.value, 256);
      expect(h.value, 256);
      var nonzero = 0;
      for (var i = 0; i < 256 * 256 * 4; i += 4) {
        if (buf[i] != 0 || buf[i + 1] != 0 || buf[i + 2] != 0) nonzero++;
      }
      expect(nonzero, greaterThan(0), reason: 'frame is empty/black');

      final snap = bindings.engine_get_snapshot_json_opt!(engine).toDartString();
      expect(snap, contains('ready'));
      expect(snap, contains('point_count'));
      expect(snap, contains('camera'));
      expect(snap, contains('render'));
      expect(bindings.engine_point_count_opt!(engine), greaterThan(0));

      // --- pick / gizmo / clear / png ---
      expect(bindings.engine_pick_point_opt, isNotNull,
          reason: 'engine_pick_point missing');
      expect(bindings.engine_gizmo_axis_opt, isNotNull);
      expect(bindings.engine_clear_tile_cache_opt, isNotNull);
      expect(bindings.engine_read_frame_png_opt, isNotNull);

      final pickOut = calloc<Float>(4);
      try {
        // 极小容差 + 角点：应未命中（容差判定必须生效）
        final missRc = bindings.engine_pick_point_opt!(
          engine,
          1.0,
          1.0,
          2.0,
          pickOut,
        );
        expect(missRc, 0, reason: 'tiny tolerance at corner must miss');

        // 视口中心附近应能命中 sample 点（容差放宽）
        var hit = 0;
        for (var ty = 40; ty <= 216 && hit == 0; ty += 20) {
          for (var tx = 40; tx <= 216; tx += 20) {
            final rc = bindings.engine_pick_point_opt!(
              engine,
              tx.toDouble(),
              ty.toDouble(),
              40.0,
              pickOut,
            );
            if (rc == 1) {
              hit = 1;
              break;
            }
          }
        }
        expect(hit, 1, reason: 'pick_point should hit sample cloud in viewport');
      } finally {
        calloc.free(pickOut);
      }

      final camBefore =
          bindings.engine_get_snapshot_json_opt!(engine).toDartString();
      expect(bindings.engine_gizmo_axis_opt!(engine, 4), 0); // top
      final camAfter =
          bindings.engine_get_snapshot_json_opt!(engine).toDartString();
      expect(camAfter, isNot(equals(camBefore)),
          reason: 'gizmo_axis must change camera state in snapshot');
      expect(bindings.engine_clear_tile_cache_opt!(engine), 0);

      // clear 后需重渲再编码 PNG
      expect(
        bindings.engine_render_rgba_opt!(
          engine,
          buf,
          256 * 256 * 4,
          w,
          h,
        ),
        0,
      );
      final pngLen = calloc<Uint32>();
      final pngBuf = calloc<Uint8>(256 * 256 * 4 + 65536);
      try {
        final pngRc = bindings.engine_read_frame_png_opt!(
          engine,
          pngBuf,
          256 * 256 * 4 + 65536,
          pngLen,
        );
        expect(pngRc, 0,
            reason: bindings.engine_last_error_opt?.call(engine).toDartString() ??
                '');
        expect(pngLen.value, greaterThan(100));
        // PNG magic
        expect(pngBuf[0], 0x89);
        expect(pngBuf[1], 0x50);
      } finally {
        calloc.free(pngLen);
        calloc.free(pngBuf);
      }
    } finally {
      calloc.free(w);
      calloc.free(h);
      calloc.free(buf);
    }

    // --- T7b: export ply / csv 到 tmp ---
    final plyPath =
        '${exportDir.path}${Platform.pathSeparator}engine_export.ply';
    final csvPath =
        '${exportDir.path}${Platform.pathSeparator}engine_export.csv';
    final plyPtr = plyPath.toNativeUtf8();
    final fmtPly = 'ply'.toNativeUtf8();
    final csvPtr = csvPath.toNativeUtf8();
    final fmtCsv = 'csv'.toNativeUtf8();
    try {
      expect(
        bindings.engine_export_opt!(engine, plyPtr, fmtPly),
        0,
        reason: bindings.engine_last_error_opt?.call(engine).toDartString() ?? '',
      );
      expect(
        bindings.engine_export_opt!(engine, csvPtr, fmtCsv),
        0,
        reason: bindings.engine_last_error_opt?.call(engine).toDartString() ?? '',
      );
    } finally {
      calloc.free(plyPtr);
      calloc.free(fmtPly);
      calloc.free(csvPtr);
      calloc.free(fmtCsv);
    }

    final plyFile = File(plyPath);
    final csvFile = File(csvPath);
    expect(plyFile.existsSync(), isTrue, reason: plyPath);
    expect(csvFile.existsSync(), isTrue, reason: csvPath);
    expect(plyFile.lengthSync(), greaterThan(100));
    expect(csvFile.lengthSync(), greaterThan(20));
    final plyHead = plyFile.readAsStringSync();
    expect(plyHead.startsWith('ply'), isTrue);
    final csvLines = csvFile.readAsLinesSync();
    expect(csvLines.first, contains('x'));
    expect(csvLines.length, greaterThan(1));

    // --- T7b: destroy 后无残留句柄（重建可再次成功）---
    engineLive = false;
    bindings.engine_destroy_opt!(engine);
    // 立即重建：若 destroy 未释放 Vulkan/文件句柄，create/load 会失败
    final engine2 = create(128, 128);
    expect(engine2.address, isNot(equals(0)),
        reason: 'recreate after destroy failed: '
            '${bindings.engine_last_global_error_opt?.call().toDartString() ?? ''}');
    try {
      final pathPtr2 = sampleSource.toNativeUtf8();
      try {
        expect(bindings.engine_load_dataset_opt!(engine2, pathPtr2), 0,
            reason: 'reload after destroy failed: sample bundle still locked?');
      } finally {
        calloc.free(pathPtr2);
      }
      final w2 = calloc<Uint32>();
      final h2 = calloc<Uint32>();
      final buf2 = calloc<Uint8>(128 * 128 * 4);
      try {
        expect(
          bindings.engine_render_rgba_opt!(engine2, buf2, 128 * 128 * 4, w2, h2),
          0,
        );
      } finally {
        calloc.free(w2);
        calloc.free(h2);
        calloc.free(buf2);
      }
    } finally {
      bindings.engine_destroy_opt!(engine2);
    }

    // 本测试进程不应再 spawn 双进程 Viewer（R5：无孤儿）
    // engine-only 路径不调用 viewer_start；此处仅作文档性断言占位
    expect(bindings.engine_create_simple_opt, isNotNull);
  });
}
