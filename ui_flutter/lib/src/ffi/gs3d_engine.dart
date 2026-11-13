import 'dart:async';
import 'dart:convert';
import 'dart:ffi' as ffi;
import 'dart:io';
import 'dart:ui' as ui;
import 'package:ffi/ffi.dart';
import 'package:flutter/services.dart';
import 'gs3d_bindings.dart';

/// 拾取到的点：坐标与标量值，使用工程包的单位。
typedef PickedPoint = ({double x, double y, double z, double value});

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
  final ffi.Pointer<ffi.Uint32> _outStride = calloc<ffi.Uint32>();
  final ffi.Pointer<ffi.Uint64> _outSequence = calloc<ffi.Uint64>();

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
    } else if (Platform.isLinux) {
      final exeDir = File(Platform.resolvedExecutable).parent.path;
      shaderDir = '$exeDir/data/gs3d_shaders';
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

  /// 引擎句柄（不透明指针地址）：仅供平台层把它交回 JNI 附加 surface。
  int get nativeHandle => _disposed ? 0 : _ptr.address;

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

  /// 载入内置合成地形（基准/压力测试）：point_count 取接近的完全平方数。
  int loadSynthetic(int pointCount) {
    _checkDisposed();
    return _bindings.loadSynthetic(_ptr, pointCount);
  }

  /// 每帧绘制点预算（0 = 按视口自动）；决定 LOD 选层。
  void setPointBudget(int maxPoints) {
    _checkDisposed();
    _bindings.setPointBudget(_ptr, maxPoints);
  }

  /// 数据集总点数（源）。
  int get sourcePointCount {
    if (_disposed) return 0;
    return _bindings.sourcePointCount(_ptr);
  }

  /// 最近一帧实际绘制点数（LOD 后）。
  int get drawnPointCount {
    if (_disposed) return 0;
    return _bindings.drawnPointCount(_ptr);
  }

  /// 提交异步开包：读盘在引擎工作线程，主线程只做装载。返回任务号。
  int openDatasetAsync(String path) {
    _checkDisposed();
    if (path.isEmpty) {
      throw ArgumentError.value(path, 'path', '工程包路径不能为空');
    }
    final pPath = path.toNativeUtf8();
    final pTask = calloc<ffi.Uint64>();
    try {
      final rc = _bindings.openDataset(_ptr, pPath, pTask);
      if (rc != 0) {
        throw StateError('无法提交开包任务 (rc=$rc): $lastError');
      }
      return pTask.value;
    } finally {
      calloc.free(pTask);
      calloc.free(pPath);
    }
  }

  /// 请求取消；已终止的任务返回 -1。
  int cancelTask(int task) {
    if (_disposed) return -1;
    return _bindings.cancel(_ptr, task);
  }

  /// 入队一条普通命令（相机、点大小、配色、背景、视口、出帧）。
  /// 只表示已校验并入队，执行失败通过 [pollState] 的 error 发布。
  int submitCommand(ffi.Pointer<Gs3dCommandNative> command) {
    if (_disposed) return -2;
    command.ref.structSize = ffi.sizeOf<Gs3dCommandNative>();
    command.ref.flags = 0;
    return _bindings.submitCommand(_ptr, command, ffi.nullptr);
  }

  /// 读取一代状态。终态任务只在本次读取中出现，漏读就会永久丢失。
  Gs3dStateNative? pollState(ffi.Pointer<Gs3dStateNative> buffer) {
    if (_disposed) return null;
    buffer.ref.structSize = ffi.sizeOf<Gs3dStateNative>();
    final rc = _bindings.pollState(_ptr, buffer);
    if (rc == -2) return null;
    if (rc != 0) {
      throw StateError('读取引擎状态失败 (rc=$rc)');
    }
    return buffer.ref;
  }

  /// 异步打开工程包并等待终态。UI 线程只做轮询，不读盘。
  Future<void> loadDatasetAsync(
    String path, {
    Duration interval = const Duration(milliseconds: 200),
    void Function(int progress, String stage)? onProgress,
  }) async {
    final task = openDatasetAsync(path);
    final state = calloc<Gs3dStateNative>();
    try {
      while (true) {
        await Future<void>.delayed(interval);
        if (_disposed) return;
        final snapshot = pollState(state);
        if (snapshot == null) continue;
        for (var i = 0; i < snapshot.taskCount; ++i) {
          if (snapshot.tasks[i].id != task) continue;
          final taskState = snapshot.tasks[i].state;
          onProgress?.call(snapshot.tasks[i].progress, snapshot.stageAt(i));
          if (taskState == gs3dTaskDone) return;
          if (taskState == gs3dTaskFailed) {
            throw StateError('打开工程包失败: ${snapshot.errorText}');
          }
          if (taskState == gs3dTaskCancelled) {
            throw StateError('打开工程包已取消');
          }
        }
      }
    } finally {
      calloc.free(state);
    }
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

  /// 导出当前数据集：format 为 `ply`（ascii）或 `csv`；0 成功。
  int exportDataset(String path, {String format = 'ply'}) {
    _checkDisposed();
    final nativePath = path.toNativeUtf8();
    final nativeFormat = format.toNativeUtf8();
    try {
      return _bindings.exportDataset(_ptr, nativePath, nativeFormat);
    } finally {
      malloc.free(nativePath);
      malloc.free(nativeFormat);
    }
  }

  /// 当前帧编码成 PNG；没有渲染过返回 null，缓冲不够会自动放大重试。
  Uint8List? readFramePng({
    int initialBytes = 4 << 20,
    int maxBytes = 64 << 20,
  }) {
    if (_disposed) return null;
    var capacity = initialBytes;
    while (capacity <= maxBytes) {
      final buffer = malloc<ffi.Uint8>(capacity);
      final outLen = calloc<ffi.Uint32>();
      try {
        final rc = _bindings.readFramePng(_ptr, buffer, capacity, outLen);
        if (rc == 0) {
          return Uint8List.fromList(buffer.asTypedList(outLen.value));
        }
        if (rc != -1) return null; // -2 未渲染过、-3 编码失败
        capacity *= 4;
      } finally {
        malloc.free(buffer);
        calloc.free(outLen);
      }
    }
    return null;
  }

  /// 屏幕坐标拾取（逻辑像素）：命中返回点，未命中返回 null。
  ///
  /// shortcut: 内核在渲染线程上线性扫描已装载的点，50M 数据集一次拾取要数秒，
  /// 升级时机：接入 GPU id 缓冲或异步拾取任务时。
  PickedPoint? pickPoint(
    double screenX,
    double screenY, {
    double tolerancePx = 12,
  }) {
    if (_disposed) return null;
    final out = calloc<ffi.Float>(4);
    try {
      final rc = _bindings.pickPoint(
        _ptr,
        screenX,
        screenY,
        tolerancePx,
        out,
      );
      if (rc != 1) return null;
      return (x: out[0], y: out[1], z: out[2], value: out[3]);
    } finally {
      calloc.free(out);
    }
  }

  /// 点大小：只入队，不等渲染线程（界面拖动不会卡在 Vulkan 提交上）。
  int submitPointSize(double size) {
    if (_disposed) return -2;
    final command = calloc<Gs3dCommandNative>();
    try {
      command.ref.op = gs3dCmdPointSize;
      command.ref.args[0] = size;
      return submitCommand(command);
    } finally {
      calloc.free(command);
    }
  }

  /// 配色索引（0–8）：并集里的 colormap 与首个 float 共用 4 字节。
  int submitColormap(int index) {
    if (_disposed) return -2;
    final command = calloc<Gs3dCommandNative>();
    try {
      command.ref.op = gs3dCmdColormap;
      ffi.Pointer<ffi.Int32>.fromAddress(
        command.address + ffi.sizeOf<ffi.Uint32>() * 3,
      ).value = index;
      return submitCommand(command);
    } finally {
      calloc.free(command);
    }
  }

  /// 设置「渲染像素 / 逻辑像素」汇率（见 `Config::view_scale`）：
  /// 纹理按物理像素建立时必须告知引擎，否则内核的像素门槛（瓦片投影尺寸、
  /// 底图目标点距、自动点预算）整体偏大，会把瓦片层挤出预算。
  /// 回读路径（逻辑像素渲染）传 1.0。
  void setViewScale(double scale) {
    _checkDisposed();
    if (!scale.isFinite || scale <= 0) return;
    final rc = _bindings.setViewScale(_ptr, scale);
    if (rc != 0) throw StateError('视图像素汇率设置失败 (rc=$rc): $lastError');
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
  ///
  /// 同步兼容路径：会阻塞在原生渲染线程上直到这一帧回读完成。
  /// 交互式前端应改用 [requestFrame] + [readFrame]。
  Future<ui.Image?> renderFrame() async {
    if (_disposed || _width <= 0 || _height <= 0) return null;

    _ensureBuffer(_width * _height * 4);

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

    return _decode(rw, rh);
  }

  /// 已发布帧序号；每次成功渲染递增。渲染线程空闲时不变。
  int get frameSequence {
    if (_disposed) return 0;
    return _bindings.frameSequence(_ptr);
  }

  /// 最近一帧在渲染线程上的耗时（毫秒，含 GPU 提交与回读）。
  double get lastRenderMs {
    if (_disposed) return 0;
    return _bindings.lastRenderMs(_ptr);
  }

  /// 请求渲染线程异步绘制一帧（非阻塞，渲染中重复请求会被合并）。
  void requestFrame() {
    _checkDisposed();
    final rc = _bindings.requestFrame(_ptr);
    if (rc != 0) {
      throw StateError('异步帧请求失败 (rc=$rc): $lastError');
    }
  }

  /// 读取最近完成帧并转码为 ui.Image；尚无帧时返回 null。
  ///
  /// 该调用不触发 Vulkan、也不等待渲染线程，等待代价上限是一次 memcpy。
  Future<ui.Image?> readFrame() async {
    if (_disposed) return null;

    // 首次读取时按已知视口尺寸预分配，避免一次探测往返。
    if (_rgbaBuffer == null && _width > 0 && _height > 0) {
      _ensureBuffer(_width * _height * 4);
    }

    for (var attempt = 0; attempt < 2; attempt++) {
      final rc = _bindings.readFrameRgba(
        _ptr,
        _rgbaBuffer ?? ffi.nullptr.cast<ffi.Uint8>(),
        _rgbaBufferSize,
        _outW,
        _outH,
        _outStride,
        _outSequence,
      );
      if (rc == -2) return null; // 尚无已完成帧
      if (rc == -1) {
        // 容量不足：ABI 已回填实际宽高，扩容后重试一次。
        final needed = _outW.value * _outH.value * 4;
        if (needed <= 0) {
          throw StateError('原生帧尺寸无效: ${_outW.value}x${_outH.value}');
        }
        _ensureBuffer(needed);
        continue;
      }
      if (rc != 0) {
        throw StateError('读取原生帧失败 (rc=$rc): $lastError');
      }

      final rw = _outW.value;
      final rh = _outH.value;
      if (rw <= 0 || rh <= 0 || rw * rh * 4 > _rgbaBufferSize) {
        throw StateError('原生帧尺寸无效: ${rw}x$rh');
      }
      return _decode(rw, rh);
    }
    throw StateError('原生帧缓冲扩容后仍无法读取: $lastError');
  }

  void _ensureBuffer(int bytes) {
    if (_rgbaBuffer != null && _rgbaBufferSize >= bytes) return;
    if (_rgbaBuffer != null) {
      calloc.free(_rgbaBuffer!);
    }
    _rgbaBufferSize = bytes;
    _rgbaBuffer = calloc<ffi.Uint8>(bytes);
  }

  Future<ui.Image> _decode(int width, int height) {
    /*
     * 直接以本地内存视图交给 Flutter，不再做一次 Dart 侧整帧拷贝。
     *
     * 安全性：_rgbaBuffer 由本对象持有，ABI 的回读在帧通道锁内完成
     * （读到的始终是一帧完整、不会再被改写的图），而这里只是在锁外把
     * 同一块内存交给解码器；解码期间渲染线程写的是它自己的 scratch，
     * 不会碰到这块缓冲。
     */
    final pixels = _rgbaBuffer!.asTypedList(width * height * 4);

    final completer = Completer<ui.Image>();
    ui.decodeImageFromPixels(
      pixels,
      width,
      height,
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
    calloc.free(_outStride);
    calloc.free(_outSequence);
  }

  void _checkDisposed() {
    if (_disposed) {
      throw StateError('Gs3dEngine has already been disposed.');
    }
  }
}
