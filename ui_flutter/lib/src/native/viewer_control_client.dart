import 'dart:async';
import 'dart:convert';
import 'dart:ffi';
import 'dart:io';
import 'dart:isolate';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import '../ffi/geoscatter3d_bindings.dart';

/// 在隔离线程打开 gs3d_ffi 并执行阻塞 C-ABI。
/// ViewerBridge 为进程级单例，跨 isolate 共享同一 C++ 状态。
String _resolveDllPath() {
  if (Platform.isWindows) {
    final env = Platform.environment['GS3D_RUNTIME_DIR'];
    if (env != null) {
      final p = '$env${Platform.pathSeparator}gs3d_ffi.dll';
      if (File(p).existsSync()) return p;
    }
    final candidates = [
      '${Directory.current.path}${Platform.pathSeparator}..${Platform.pathSeparator}tmp${Platform.pathSeparator}build-win${Platform.pathSeparator}src${Platform.pathSeparator}ffi${Platform.pathSeparator}Release${Platform.pathSeparator}gs3d_ffi.dll',
      '${Directory.current.path}${Platform.pathSeparator}..${Platform.pathSeparator}tmp${Platform.pathSeparator}build-win${Platform.pathSeparator}Release${Platform.pathSeparator}gs3d_ffi.dll',
      '${Directory.current.path}${Platform.pathSeparator}gs3d_ffi.dll',
      '${File(Platform.resolvedExecutable).parent.path}${Platform.pathSeparator}gs3d_ffi.dll',
    ];
    for (final p in candidates) {
      if (File(p).existsSync()) return File(p).absolute.path;
    }
  }
  return 'gs3d_ffi.dll';
}

GeoScatter3dBindings _openInIsolate(String path) {
  if (Platform.isWindows) {
    final kernel32 = DynamicLibrary.open('kernel32.dll');
    final setDllDirA = kernel32.lookupFunction<
      Int32 Function(Pointer<Utf8>),
      int Function(Pointer<Utf8>)
    >('SetDllDirectoryA');
    final dir = File(path).absolute.parent.path.toNativeUtf8();
    try {
      setDllDirA(dir);
    } finally {
      calloc.free(dir);
    }
  }
  return GeoScatter3dBindings(DynamicLibrary.open(path));
}

/// 阻塞 viewer_start（最多约 10s）。
@pragma('vm:entry-point')
int _blockingStart(String dllPath, String exe, String dataset) {
  final b = _openInIsolate(dllPath);
  final fn = b.viewer_start_opt;
  if (fn == null) return -100;
  final e = exe.toNativeUtf8();
  final d = dataset.toNativeUtf8();
  try {
    return fn(e, d);
  } finally {
    calloc.free(e);
    calloc.free(d);
  }
}

@pragma('vm:entry-point')
String _blockingRpc(String dllPath, String payload) {
  final b = _openInIsolate(dllPath);
  final fn = b.viewer_rpc_opt;
  if (fn == null) {
    return jsonEncode({
      'jsonrpc': '2.0',
      'id': null,
      'error': {'code': -32000, 'message': 'viewer_rpc missing'},
    });
  }
  final req = payload.toNativeUtf8();
  try {
    return fn(req).toDartString();
  } finally {
    calloc.free(req);
  }
}

@pragma('vm:entry-point')
String _blockingScreenshot(String dllPath) {
  final b = _openInIsolate(dllPath);
  final fn = b.viewer_screenshot_json_opt;
  if (fn == null) {
    return jsonEncode({'error': 'screenshot bridge missing'});
  }
  return fn().toDartString();
}

@pragma('vm:entry-point')
String _blockingExecute(
  String dllPath,
  String component,
  String command,
  String paramsJson,
) {
  final b = _openInIsolate(dllPath);
  final fn = b.viewer_execute_opt;
  if (fn == null) {
    return jsonEncode({
      'jsonrpc': '2.0',
      'id': null,
      'error': {'code': -32000, 'message': 'viewer_execute missing'},
    });
  }
  final c = component.toNativeUtf8();
  final cmd = command.toNativeUtf8();
  final p = paramsJson.toNativeUtf8();
  try {
    return fn(c, cmd, p).toDartString();
  } finally {
    calloc.free(c);
    calloc.free(cmd);
    calloc.free(p);
  }
}

@pragma('vm:entry-point')
void _blockingStop(String dllPath, bool force) {
  final b = _openInIsolate(dllPath);
  try {
    if (force) {
      b.viewer_stop_opt?.call();
    } else {
      b.quit_viewer_opt?.call();
    }
  } catch (_) {
    try {
      b.viewer_stop_opt?.call();
    } catch (_) {}
  }
}

@pragma('vm:entry-point')
List<int> _blockingCaptureDims(String dllPath) {
  final b = _openInIsolate(dllPath);
  return [
    b.viewer_last_capture_width_opt?.call() ?? 0,
    b.viewer_last_capture_height_opt?.call() ?? 0,
  ];
}

/// 单 Isolate 内完成相机输入 + 截图，避免拖拽时两次 Isolate 启动。
@pragma('vm:entry-point')
String _blockingCameraAndShot(
  String dllPath,
  double rotX,
  double rotY,
  double panX,
  double panY,
  double scrollY,
) {
  final b = _openInIsolate(dllPath);
  final camFn = b.camera_input_opt;
  final shotFn = b.viewer_screenshot_json_opt;
  if (camFn == null || shotFn == null) {
    return jsonEncode({'error': 'camera_input/screenshot bridge missing'});
  }
  final rc = camFn(rotX, rotY, panX, panY, scrollY);
  if (rc != 0) {
    return jsonEncode({'error': 'camera_input rc=$rc'});
  }
  return shotFn().toDartString();
}

/// Flutter 侧原生查看器控制客户端（经 C-ABI ViewerBridge）。
///
/// **阻塞 C-ABI 全部在 Isolate 中执行**，避免 UI isolate 被截图/RPC 卡死。
class NativeViewerControlClient {
  NativeViewerControlClient({this.port = 0});

  final int port;
  GeoScatter3dBindings? _bindings;
  String? _dllPath;
  String _startupLog = '';
  bool _connected = false;
  int _lastW = 0;
  int _lastH = 0;

  bool get isConnected => _connected;
  int get lastCaptureWidth => _lastW;
  int get lastCaptureHeight => _lastH;
  String get startupDiagnostics => _startupLog;

  String get _path {
    if (_dllPath != null) return _dllPath!;
    _dllPath = _resolveDllPath();
    return _dllPath!;
  }

  Future<bool> start({
    required String executablePath,
    required String datasetPath,
  }) async {
    if (_connected) return true;
    try {
      _bindings ??= GeoScatter3dBindings.openLibrary();
    } catch (error) {
      _startupLog = '无法加载 gs3d_ffi: $error';
      return false;
    }
    if (_bindings!.viewer_start_opt == null) {
      _startupLog = '当前 gs3d_ffi.dll 未包含 Viewer 桥符号，请更新动态库';
      return false;
    }

    try {
      final path = _path;
      final code = await Isolate.run(
        () => _blockingStart(path, executablePath, datasetPath),
      );
      if (code != 0) {
        _startupLog =
            _bindings!.viewer_last_error_opt?.call().toDartString() ?? '';
        if (_startupLog.isEmpty) _startupLog = 'viewer_start failed: $code';
        _connected = false;
        return false;
      }
      _connected = true;
      _startupLog =
          _bindings!.viewer_startup_log_opt?.call().toDartString() ?? '';
      await getViewportState();
      return true;
    } catch (error) {
      _startupLog = 'viewer_start 异常: $error';
      _connected = false;
      return false;
    }
  }

  Future<Map<String, dynamic>> getViewportState() {
    return request('get_state', {'component': 'viewport.main'});
  }

  Future<Uint8List> capturePng({bool includeDims = true}) async {
    final path = _path;
    final raw = await Isolate.run(() => _blockingScreenshot(path));
    final result = jsonDecode(raw);
    if (result is! Map<String, dynamic>) {
      throw StateError('原生查看器未返回截图结果');
    }
    if (result['error'] != null) {
      throw StateError('截图失败: ${result['error']}');
    }
    final data = result['data'];
    if (data is! String || data.isEmpty) {
      throw StateError('原生查看器截图为空');
    }
    if (includeDims) {
      _lastW = (result['width'] as num?)?.toInt() ?? _lastW;
      _lastH = (result['height'] as num?)?.toInt() ?? _lastH;
    }
    return base64Decode(data);
  }

  /// 合并相机增量后单次 Isolate：camera_input + screenshot。
  Future<Uint8List> cameraInputAndCapture({
    double rotateX = 0,
    double rotateY = 0,
    double panX = 0,
    double panY = 0,
    double scrollY = 0,
  }) async {
    if (!_connected) {
      throw StateError('原生查看器控制面尚未连接');
    }
    final path = _path;
    final raw = await Isolate.run(
      () => _blockingCameraAndShot(
        path,
        rotateX,
        rotateY,
        panX,
        panY,
        scrollY,
      ),
    );
    final result = jsonDecode(raw);
    if (result is! Map<String, dynamic>) {
      throw StateError('相机+截图响应格式错误');
    }
    if (result['error'] != null) {
      throw StateError(result['error'].toString());
    }
    final data = result['data'];
    if (data is! String || data.isEmpty) {
      throw StateError('相机后截图为空');
    }
    _lastW = (result['width'] as num?)?.toInt() ?? _lastW;
    _lastH = (result['height'] as num?)?.toInt() ?? _lastH;
    return base64Decode(data);
  }

  Future<Map<String, dynamic>> request(
    String method,
    Map<String, dynamic> params,
  ) async {
    if (!_connected) {
      throw StateError('原生查看器控制面尚未连接');
    }
    final payload = jsonEncode({
      'jsonrpc': '2.0',
      'id': DateTime.now().microsecondsSinceEpoch,
      'method': method,
      'params': params,
    });
    final path = _path;
    final raw = await Isolate.run(() => _blockingRpc(path, payload));
    final decoded = jsonDecode(raw);
    if (decoded is! Map<String, dynamic>) {
      throw StateError('控制面响应格式错误');
    }
    if (decoded['error'] != null) {
      throw StateError(decoded['error'].toString());
    }
    return decoded;
  }

  /// 经 FFI 执行 ComponentRegistry 命令。
  Future<Map<String, dynamic>> executeComponent(
    String component,
    String command, [
    Map<String, dynamic> params = const {},
  ]) async {
    if (!_connected) {
      throw StateError('原生查看器控制面尚未连接');
    }
    final path = _path;
    final paramsJson = jsonEncode(params);
    final raw = await Isolate.run(
      () => _blockingExecute(path, component, command, paramsJson),
    );
    final decoded = jsonDecode(raw);
    if (decoded is! Map<String, dynamic>) {
      throw StateError('execute 响应格式错误');
    }
    return decoded;
  }

  Future<void> stop({bool force = false}) async {
    if (_connected || force) {
      try {
        final path = _path;
        await Isolate.run(() => _blockingStop(path, force || !_connected));
      } catch (_) {}
    }
    _connected = false;
  }
}
