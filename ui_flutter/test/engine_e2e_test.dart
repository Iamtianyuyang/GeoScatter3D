import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/ffi/geoscatter3d_bindings.dart';

/// 单进程渲染内核 e2e。设置 GS3D_ENGINE_E2E=1 启用。
void main() {
  test('single-process gs3d_engine renders sample frame', () {
    if (Platform.environment['GS3D_ENGINE_E2E'] != '1') {
      markTestSkipped('Set GS3D_ENGINE_E2E=1 to run engine e2e');
      return;
    }
    if (!Platform.isWindows) {
      markTestSkipped('Windows only');
      return;
    }
    final runtime =
        Platform.environment['GS3D_RUNTIME_DIR'] ??
        '${Directory.current.parent.path}${Platform.pathSeparator}tmp${Platform.pathSeparator}build-win${Platform.pathSeparator}src${Platform.pathSeparator}ffi${Platform.pathSeparator}Release';
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

    // shader 默认相对路径 assets/shaders；切到 runtime 目录加载
    final prevCwd = Directory.current;
    Directory.current = Directory(runtime);
    addTearDown(() => Directory.current = prevCwd);

    final create = bindings.engine_create_simple_opt!;
    final engine = create(256, 256);
    expect(engine.address, isNot(equals(0)),
        reason: bindings.engine_last_global_error_opt?.call().toDartString() ?? '');

    addTearDown(() => bindings.engine_destroy_opt?.call(engine));

    final source = File(
      '${prevCwd.parent.path}${Platform.pathSeparator}data${Platform.pathSeparator}sample-points.gs3d.bundle${Platform.pathSeparator}source.gs3d',
    ).absolute.path;
    expect(File(source).existsSync(), isTrue, reason: source);
    final pathPtr = source.toNativeUtf8();
    try {
      expect(bindings.engine_load_dataset_opt!(engine, pathPtr), 0);
    } finally {
      calloc.free(pathPtr);
    }

    bindings.engine_set_point_size_opt!(engine, 3.0);
    bindings.engine_reset_camera_opt!(engine);

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
      // 至少部分像素非清屏色
      var nonzero = 0;
      for (var i = 0; i < 256 * 256 * 4; i += 4) {
        if (buf[i] != 0 || buf[i + 1] != 0 || buf[i + 2] != 0) nonzero++;
      }
      expect(nonzero, greaterThan(0));

      final snap = bindings.engine_get_snapshot_json_opt!(engine).toDartString();
      expect(snap, contains('ready'));
      expect(snap, contains('point_count'));
    } finally {
      calloc.free(w);
      calloc.free(h);
      calloc.free(buf);
    }
  });
}
