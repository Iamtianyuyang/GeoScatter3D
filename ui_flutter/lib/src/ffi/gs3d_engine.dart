import 'dart:async';
import 'dart:convert';
import 'dart:ffi' as ffi;
import 'dart:io';
import 'dart:ui' as ui;
import 'package:ffi/ffi.dart';
import 'package:flutter/services.dart';
import 'gs3d_bindings.dart';

/// GeoScatter3D 面向对象引擎封装 (纯净 Dart FFI)
class Gs3dEngine {
  final Gs3dBindings _bindings;
  Gs3dEnginePtr _ptr;
  int _width;
  int _height;
  bool _disposed = false;

  ffi.Pointer<ffi.Uint8>? _rgbaBuffer;
  int _rgbaBufferSize = 0;
  final ffi.Pointer<ffi.Uint32> _outW = calloc<ffi.Uint32>();
  final ffi.Pointer<ffi.Uint32> _outH = calloc<ffi.Uint32>();

  Gs3dEngine._(this._bindings, this._ptr, this._width, this._height);

  /// 将 APK 中的着色器准备为原生核心可读取的文件。
  static Future<Gs3dEngine> createForPlatform({
    int width = 1024,
    int height = 1024,
  }) async {
    String? shaderDir;
    if (Platform.isAndroid) {
      shaderDir = await const MethodChannel(
        'com.geoscatter3d.ui_flutter/documents',
      ).invokeMethod<String>('prepareEngineShaders');
      if (shaderDir == null || shaderDir.isEmpty) {
        throw StateError('Android 着色器目录不可用');
      }
    }
    return create(width: width, height: height, shaderDir: shaderDir);
  }

  static Gs3dEngine create({
    int width = 1024,
    int height = 1024,
    String? shaderDir,
    String? preferredGpu,
    bool enableValidation = false,
  }) {
    final bindings = Gs3dBindings(Gs3dBindings.openLibrary());
    final cfg = calloc<Gs3dEngineConfigNative>();
    try {
      cfg.ref.viewportWidth = width;
      cfg.ref.viewportHeight = height;
      cfg.ref.preferredGpuUuid = preferredGpu != null
          ? preferredGpu.toNativeUtf8()
          : ffi.Pointer<Utf8>.fromAddress(0);
      cfg.ref.shaderDir = shaderDir != null
          ? shaderDir.toNativeUtf8()
          : ffi.Pointer<Utf8>.fromAddress(0);
      cfg.ref.enableValidation = enableValidation ? 1 : 0;

      final ptr = bindings.create(cfg);
      if (ptr.address == 0) {
        final err = bindings.lastGlobalError().toDartString();
        throw StateError('单进程引擎启动失败: $err');
      }
      return Gs3dEngine._(bindings, ptr, width, height);
    } finally {
      if (cfg.ref.preferredGpuUuid.address != 0) {
        calloc.free(cfg.ref.preferredGpuUuid);
      }
      if (cfg.ref.shaderDir.address != 0) {
        calloc.free(cfg.ref.shaderDir);
      }
      calloc.free(cfg);
    }
  }

  int get width => _width;
  int get height => _height;
  bool get isDisposed => _disposed;

  String get version => _bindings.version().toDartString();

  int get pointCount {
    if (_disposed) return 0;
    return _bindings.pointCount(_ptr);
  }

  String get lastError {
    if (_disposed) return '';
    return _bindings.lastError(_ptr).toDartString();
  }

  Map<String, dynamic> get diagnostics {
    if (_disposed) return {};
    final jsonStr = _bindings.getDiagnosticsJson(_ptr).toDartString();
    if (jsonStr.isEmpty) return {};
    try {
      return json.decode(jsonStr) as Map<String, dynamic>;
    } catch (_) {
      return {};
    }
  }

  int loadSample() {
    _checkDisposed();
    return _bindings.loadSample(_ptr);
  }

  int loadDataset(String path) {
    _checkDisposed();
    final pUtf8 = path.toNativeUtf8();
    try {
      return _bindings.loadDataset(_ptr, pUtf8);
    } finally {
      calloc.free(pUtf8);
    }
  }

  void unloadDataset() {
    if (_disposed) return;
    _bindings.unloadDataset(_ptr);
  }

  void resize(int w, int h) {
    _checkDisposed();
    if (w <= 0 || h <= 0) return;
    if (_width == w && _height == h) return;
    final rc = _bindings.resize(_ptr, w, h);
    if (rc != 0) {
      throw StateError('视口调整失败 (rc=$rc): $lastError');
    }
    _width = w;
    _height = h;
  }

  void cameraInput({
    double rotDx = 0.0,
    double rotDy = 0.0,
    double panDx = 0.0,
    double panDy = 0.0,
    double scrollY = 0.0,
  }) {
    _checkDisposed();
    final rc = _bindings.cameraInput(_ptr, rotDx, rotDy, panDx, panDy, scrollY);
    if (rc != 0) throw StateError('相机输入失败 (rc=$rc): $lastError');
  }

  void resetCamera() {
    _checkDisposed();
    final rc = _bindings.resetCamera(_ptr);
    if (rc != 0) throw StateError('相机复位失败 (rc=$rc): $lastError');
  }

  void setPointSize(double size) {
    _checkDisposed();
    _bindings.setPointSize(_ptr, size);
  }

  void setColormap(int index) {
    _checkDisposed();
    _bindings.setColormap(_ptr, index);
  }

  void setPointShape(int shape) {
    _checkDisposed();
    _bindings.setPointShape(_ptr, shape);
  }

  void setBackground(double r, double g, double b, [double a = 1.0]) {
    _checkDisposed();
    _bindings.setBackground(_ptr, r, g, b, a);
  }

  /// 渲染一帧离屏 RGBA8，并异步转码为 ui.Image
  Future<ui.Image?> renderFrame() async {
    if (_disposed || _width <= 0 || _height <= 0) return null;

    final neededBytes = _width * _height * 4;
    if (_rgbaBuffer == null || _rgbaBufferSize < neededBytes) {
      if (_rgbaBuffer != null) {
        calloc.free(_rgbaBuffer!);
      }
      _rgbaBufferSize = neededBytes;
      _rgbaBuffer = calloc<ffi.Uint8>(neededBytes);
    }

    final rc = _bindings.renderRgba(
      _ptr,
      _rgbaBuffer!,
      _rgbaBufferSize,
      _outW,
      _outH,
    );
    if (rc != 0) {
      throw StateError('原生帧渲染失败 (rc=$rc): $lastError');
    }

    final rw = _outW.value;
    final rh = _outH.value;
    if (rw <= 0 || rh <= 0 || rw * rh * 4 > _rgbaBufferSize) {
      throw StateError('原生帧尺寸无效: ${rw}x$rh');
    }

    final byteData = _rgbaBuffer!.asTypedList(rw * rh * 4);
    // 拷贝至 Dart 管理的 Uint8List 以供 decodeImageFromPixels 使用
    final copy = Uint8List.fromList(byteData);

    final completer = Completer<ui.Image>();
    ui.decodeImageFromPixels(
      copy,
      rw,
      rh,
      ui.PixelFormat.rgba8888,
      (image) => completer.complete(image),
    );
    return completer.future;
  }

  void dispose() {
    if (_disposed) return;
    _disposed = true;
    _bindings.destroy(_ptr);
    _ptr = ffi.Pointer.fromAddress(0);
    if (_rgbaBuffer != null) {
      calloc.free(_rgbaBuffer!);
      _rgbaBuffer = null;
    }
    calloc.free(_outW);
    calloc.free(_outH);
  }

  void _checkDisposed() {
    if (_disposed) {
      throw StateError('Gs3dEngine has already been disposed.');
    }
  }
}
