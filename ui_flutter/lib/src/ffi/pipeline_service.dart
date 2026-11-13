import 'dart:async';
import 'dart:convert';
import 'dart:ffi' as ffi;
import 'dart:io';
import 'dart:isolate';

import 'package:ffi/ffi.dart';
import 'package:flutter/foundation.dart';

import 'pipeline_bindings.dart';

enum PipelineState { idle, running, done, failed, cancelled }

class PipelineStatus {
  const PipelineStatus({
    required this.id,
    required this.state,
    required this.stage,
    required this.error,
    required this.points,
    required this.chunks,
  });

  final int id;
  final PipelineState state;
  final String stage;
  final String error;
  final int points;
  final int chunks;
}

ffi.DynamicLibrary _openPipelineLibrary(String? path) {
  if (path != null) return ffi.DynamicLibrary.open(path);
  if (Platform.isAndroid) {
    return ffi.DynamicLibrary.open('libgs3d_pipeline_ffi.so');
  }
  final directory = File(Platform.resolvedExecutable).parent.path;
  if (Platform.isLinux) {
    final bundled = File('$directory/lib/libgs3d_pipeline_ffi.so');
    return ffi.DynamicLibrary.open(
      bundled.existsSync() ? bundled.path : 'libgs3d_pipeline_ffi.so',
    );
  }
  if (Platform.isWindows) {
    final bundled = File('$directory\\gs3d_pipeline_ffi.dll');
    return ffi.DynamicLibrary.open(
      bundled.existsSync() ? bundled.path : 'gs3d_pipeline_ffi.dll',
    );
  }
  throw UnsupportedError('Unsupported platform: ${Platform.operatingSystem}');
}

String _nativeText(ffi.Array<ffi.Char> chars, int capacity) {
  final bytes = <int>[];
  for (var i = 0; i < capacity; i++) {
    final byte = chars[i] & 0xff;
    if (byte == 0) break;
    bytes.add(byte);
  }
  return utf8.decode(bytes, allowMalformed: true);
}

/// Owns one native background pipeline and publishes snapshots to the UI.
class PipelineService extends ChangeNotifier {
  PipelineService({String? libraryPath}) : _libraryPath = libraryPath {
    final library = _openPipelineLibrary(libraryPath);
    final create = library.lookupFunction<PipelineCreateNative, PipelineCreate>(
      'gs3d_pipeline_create',
    );
    _start = library.lookupFunction<PipelineStartNative, PipelineStart>(
      'gs3d_pipeline_start',
    );
    _resume = library.lookupFunction<PipelineResumeNative, PipelineResume>(
      'gs3d_pipeline_resume',
    );
    _poll = library.lookupFunction<PipelinePollNative, PipelinePoll>(
      'gs3d_pipeline_poll',
    );
    _cancel = library.lookupFunction<PipelineCancelNative, PipelineCancel>(
      'gs3d_pipeline_cancel',
    );
    final config = calloc<PipelineConfigNative>();
    try {
      config.ref.structSize = ffi.sizeOf<PipelineConfigNative>();
      _pipeline = create(config);
    } finally {
      calloc.free(config);
    }
    if (_pipeline == ffi.nullptr) throw StateError('无法创建预处理任务');
  }

  final String? _libraryPath;
  late final PipelinePtr _pipeline;
  late final PipelineStart _start;
  late final PipelineResume _resume;
  late final PipelinePoll _poll;
  late final PipelineCancel _cancel;
  Timer? _timer;
  Future<void>? _closing;
  bool _disposed = false;
  PipelineStatus _status = const PipelineStatus(
    id: 0,
    state: PipelineState.idle,
    stage: '',
    error: '',
    points: 0,
    chunks: 0,
  );

  PipelineStatus get status => _status;
  Future<void> get closed => _closing ?? Future.value();

  void start(String source, String bundle, {int pointsPerChunk = 65536}) {
    _ensureOpen();
    final job = calloc<PipelineJobNative>();
    final task = calloc<ffi.Uint64>();
    final sourcePath = source.toNativeUtf8();
    final bundlePath = bundle.toNativeUtf8();
    try {
      job.ref.structSize = ffi.sizeOf<PipelineJobNative>();
      job.ref.sourcePath = sourcePath;
      job.ref.bundlePath = bundlePath;
      job.ref.pointsPerChunk = pointsPerChunk;
      _check(_start(_pipeline, job, task));
      _watch();
    } finally {
      calloc.free(bundlePath);
      calloc.free(sourcePath);
      calloc.free(task);
      calloc.free(job);
    }
  }

  void resume(String bundle) {
    _ensureOpen();
    final path = bundle.toNativeUtf8();
    final task = calloc<ffi.Uint64>();
    try {
      _check(_resume(_pipeline, path, task));
      _watch();
    } finally {
      calloc.free(task);
      calloc.free(path);
    }
  }

  void cancel() {
    _ensureOpen();
    _check(_cancel(_pipeline, _status.id));
  }

  void _watch() {
    _timer?.cancel();
    _timer = Timer.periodic(
      const Duration(milliseconds: 250),
      (_) => _refresh(),
    );
    _refresh();
  }

  void _refresh() {
    final native = calloc<PipelineStatusNative>();
    try {
      native.ref.structSize = ffi.sizeOf<PipelineStatusNative>();
      _check(_poll(_pipeline, native));
      final value = native.ref;
      if (value.state >= PipelineState.values.length) {
        throw StateError('无法识别预处理状态');
      }
      _status = PipelineStatus(
        id: value.taskId,
        state: PipelineState.values[value.state],
        stage: _nativeText(value.stage, 32),
        error: _nativeText(value.error, 256),
        points: value.points,
        chunks: value.chunks,
      );
      if (_status.state != PipelineState.running) _timer?.cancel();
      notifyListeners();
    } finally {
      calloc.free(native);
    }
  }

  void _ensureOpen() {
    if (_disposed) throw StateError('预处理服务已关闭');
  }

  static void _check(int result) {
    if (result != 0) throw StateError('预处理操作失败 ($result)');
  }

  @override
  void dispose() {
    if (_disposed) return;
    _disposed = true;
    _timer?.cancel();
    final address = _pipeline.address;
    final libraryPath = _libraryPath;
    // Native destruction waits for the current stage; never join on the UI isolate.
    _closing = Isolate.run(() {
      final destroy = _openPipelineLibrary(libraryPath)
          .lookupFunction<PipelineDestroyNative, PipelineDestroy>(
            'gs3d_pipeline_destroy',
          );
      destroy(ffi.Pointer<PipelineNative>.fromAddress(address));
    });
    super.dispose();
  }
}
