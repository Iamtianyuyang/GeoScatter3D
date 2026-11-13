import 'dart:ffi' as ffi;

import 'package:ffi/ffi.dart';

final class PipelineNative extends ffi.Opaque {}

typedef PipelinePtr = ffi.Pointer<PipelineNative>;

final class PipelineConfigNative extends ffi.Struct {
  @ffi.Uint32()
  external int structSize;
}

final class PipelineJobNative extends ffi.Struct {
  @ffi.Uint32()
  external int structSize;
  external ffi.Pointer<Utf8> sourcePath;
  external ffi.Pointer<Utf8> bundlePath;
  @ffi.Uint32()
  external int pointsPerChunk;
  external ffi.Pointer<Utf8> valueField;
}

final class PipelineStatusNative extends ffi.Struct {
  @ffi.Uint32()
  external int structSize;
  @ffi.Uint64()
  external int taskId;
  @ffi.Uint32()
  external int state;
  @ffi.Array(32)
  external ffi.Array<ffi.Char> stage;
  @ffi.Array(256)
  external ffi.Array<ffi.Char> error;
  @ffi.Uint64()
  external int points;
  @ffi.Uint64()
  external int chunks;
}

typedef PipelineCreateNative =
    PipelinePtr Function(ffi.Pointer<PipelineConfigNative>);
typedef PipelineCreate =
    PipelinePtr Function(ffi.Pointer<PipelineConfigNative>);
typedef PipelineStartNative =
    ffi.Int32 Function(
      PipelinePtr,
      ffi.Pointer<PipelineJobNative>,
      ffi.Pointer<ffi.Uint64>,
    );
typedef PipelineStart =
    int Function(
      PipelinePtr,
      ffi.Pointer<PipelineJobNative>,
      ffi.Pointer<ffi.Uint64>,
    );
typedef PipelineResumeNative =
    ffi.Int32 Function(PipelinePtr, ffi.Pointer<Utf8>, ffi.Pointer<ffi.Uint64>);
typedef PipelineResume =
    int Function(PipelinePtr, ffi.Pointer<Utf8>, ffi.Pointer<ffi.Uint64>);
typedef PipelinePollNative =
    ffi.Int32 Function(PipelinePtr, ffi.Pointer<PipelineStatusNative>);
typedef PipelinePoll =
    int Function(PipelinePtr, ffi.Pointer<PipelineStatusNative>);
typedef PipelineCancelNative = ffi.Int32 Function(PipelinePtr, ffi.Uint64);
typedef PipelineCancel = int Function(PipelinePtr, int);
typedef PipelineDestroyNative = ffi.Void Function(PipelinePtr);
typedef PipelineDestroy = void Function(PipelinePtr);
