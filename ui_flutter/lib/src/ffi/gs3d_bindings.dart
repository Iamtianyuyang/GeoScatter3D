import 'dart:ffi' as ffi;
import 'dart:io';
import 'package:ffi/ffi.dart';

// Native struct for gs3d_engine_config
final class Gs3dEngineConfigNative extends ffi.Struct {
  external ffi.Pointer<Utf8> preferredGpuUuid;
  @ffi.Uint32()
  external int viewportWidth;
  @ffi.Uint32()
  external int viewportHeight;
  external ffi.Pointer<Utf8> shaderDir;
  @ffi.Int32()
  external int enableValidation;
}

// 任务状态（与 include/ffi/geoscatter3d_ffi.h 第 21 节一致）
const int gs3dMaxTasks = 4;
const int gs3dTaskStageMax = 32;
const int gs3dTaskQueued = 1;
const int gs3dTaskRunning = 2;
const int gs3dTaskDone = 3;
const int gs3dTaskFailed = 4;
const int gs3dTaskCancelled = 5;

final class Gs3dTaskStateNative extends ffi.Struct {
  @ffi.Uint64()
  external int id;
  @ffi.Uint32()
  external int state;
  @ffi.Uint32()
  external int progress;
  @ffi.Array(gs3dTaskStageMax)
  external ffi.Array<ffi.Char> stage;
}

final class Gs3dStateNative extends ffi.Struct {
  @ffi.Uint32()
  external int structSize;
  @ffi.Uint64()
  external int generation;
  @ffi.Uint64()
  external int pointCount;
  @ffi.Uint32()
  external int datasetLoaded;
  @ffi.Uint32()
  external int taskCount;
  @ffi.Array(256)
  external ffi.Array<ffi.Char> error;
  @ffi.Array(gs3dMaxTasks)
  external ffi.Array<Gs3dTaskStateNative> tasks;

  /// 读取第 [index] 个任务的阶段名（定长、可截断）。
  String stageAt(int index) => _readCString(tasks[index].stage, gs3dTaskStageMax);

  String get errorText => _readCString(error, 256);
}

String _readCString(ffi.Array<ffi.Char> buffer, int capacity) {
  final bytes = <int>[];
  for (var i = 0; i < capacity; ++i) {
    final code = buffer[i];
    if (code == 0) break;
    bytes.add(code);
  }
  return String.fromCharCodes(bytes);
}

// 普通命令（与 include/ffi/geoscatter3d_ffi.h 第 22 节一致）
const int gs3dCmdCameraInput = 1;
const int gs3dCmdPointSize = 2;
const int gs3dCmdColormap = 3;
const int gs3dCmdBackground = 4;
const int gs3dCmdResize = 5;
const int gs3dCmdRequestFrame = 6;
const int gs3dCmdUnloadDataset = 7;

/// 命令参数的并集用最大成员（camera = 5 个 float）表示，共 20 字节。
final class Gs3dCommandNative extends ffi.Struct {
  @ffi.Uint32()
  external int structSize;
  @ffi.Uint32()
  external int op;
  @ffi.Uint32()
  external int flags;
  @ffi.Array(5)
  external ffi.Array<ffi.Float> args;
}

// Opaque engine pointer
final class Gs3dEngineOpaque extends ffi.Opaque {}

typedef Gs3dEnginePtr = ffi.Pointer<Gs3dEngineOpaque>;

// C Function signatures
typedef CVersion = ffi.Pointer<Utf8> Function();
typedef DartVersion = ffi.Pointer<Utf8> Function();

typedef CLastGlobalError = ffi.Pointer<Utf8> Function();
typedef DartLastGlobalError = ffi.Pointer<Utf8> Function();

typedef CCreate =
    Gs3dEnginePtr Function(ffi.Pointer<Gs3dEngineConfigNative> cfg);
typedef DartCreate =
    Gs3dEnginePtr Function(ffi.Pointer<Gs3dEngineConfigNative> cfg);

typedef CCreateSimple =
    Gs3dEnginePtr Function(ffi.Uint32 width, ffi.Uint32 height);
typedef DartCreateSimple = Gs3dEnginePtr Function(int width, int height);

typedef CDestroy = ffi.Void Function(Gs3dEnginePtr e);
typedef DartDestroy = void Function(Gs3dEnginePtr e);

typedef CLoadDataset =
    ffi.Int32 Function(Gs3dEnginePtr e, ffi.Pointer<Utf8> path);
typedef DartLoadDataset = int Function(Gs3dEnginePtr e, ffi.Pointer<Utf8> path);

typedef CLoadSample = ffi.Int32 Function(Gs3dEnginePtr e);
typedef DartLoadSample = int Function(Gs3dEnginePtr e);

typedef CLoadSynthetic = ffi.Int32 Function(Gs3dEnginePtr e, ffi.Uint64 pointCount);
typedef DartLoadSynthetic = int Function(Gs3dEnginePtr e, int pointCount);

typedef CSetPointBudget = ffi.Int32 Function(Gs3dEnginePtr e, ffi.Uint64 maxPoints);
typedef DartSetPointBudget = int Function(Gs3dEnginePtr e, int maxPoints);

typedef CSourcePointCount = ffi.Uint64 Function(Gs3dEnginePtr e);
typedef DartSourcePointCount = int Function(Gs3dEnginePtr e);

typedef CDrawnPointCount = ffi.Uint64 Function(Gs3dEnginePtr e);
typedef DartDrawnPointCount = int Function(Gs3dEnginePtr e);

typedef CUnloadDataset = ffi.Int32 Function(Gs3dEnginePtr e);
typedef DartUnloadDataset = int Function(Gs3dEnginePtr e);

typedef CResize =
    ffi.Int32 Function(Gs3dEnginePtr e, ffi.Uint32 w, ffi.Uint32 h);
typedef DartResize = int Function(Gs3dEnginePtr e, int w, int h);

typedef CSetPointSize = ffi.Int32 Function(Gs3dEnginePtr e, ffi.Float size);
typedef DartSetPointSize = int Function(Gs3dEnginePtr e, double size);

typedef CSetViewScale = ffi.Int32 Function(Gs3dEnginePtr e, ffi.Float scale);
typedef DartSetViewScale = int Function(Gs3dEnginePtr e, double scale);

typedef CSetColormap = ffi.Int32 Function(Gs3dEnginePtr e, ffi.Int32 index);
typedef DartSetColormap = int Function(Gs3dEnginePtr e, int index);

typedef CSetPointShape = ffi.Int32 Function(Gs3dEnginePtr e, ffi.Int32 shape);
typedef DartSetPointShape = int Function(Gs3dEnginePtr e, int shape);

typedef CSetBackground =
    ffi.Int32 Function(
      Gs3dEnginePtr e,
      ffi.Float r,
      ffi.Float g,
      ffi.Float b,
      ffi.Float a,
    );
typedef DartSetBackground =
    int Function(Gs3dEnginePtr e, double r, double g, double b, double a);

typedef CRenderRgba =
    ffi.Int32 Function(
      Gs3dEnginePtr e,
      ffi.Pointer<ffi.Uint8> outRgba,
      ffi.Uint32 maxBytes,
      ffi.Pointer<ffi.Uint32> outW,
      ffi.Pointer<ffi.Uint32> outH,
    );
typedef DartRenderRgba =
    int Function(
      Gs3dEnginePtr e,
      ffi.Pointer<ffi.Uint8> outRgba,
      int maxBytes,
      ffi.Pointer<ffi.Uint32> outW,
      ffi.Pointer<ffi.Uint32> outH,
    );

typedef CCameraInput =    ffi.Int32 Function(
      Gs3dEnginePtr e,
      ffi.Float rotDx,
      ffi.Float rotDy,
      ffi.Float panDx,
      ffi.Float panDy,
      ffi.Float scrollY,
    );
typedef DartCameraInput =
    int Function(
      Gs3dEnginePtr e,
      double rotDx,
      double rotDy,
      double panDx,
      double panDy,
      double scrollY,
    );

typedef CResetCamera = ffi.Int32 Function(Gs3dEnginePtr e);
typedef DartResetCamera = int Function(Gs3dEnginePtr e);

typedef CRequestFrame = ffi.Int32 Function(Gs3dEnginePtr e);
typedef DartRequestFrame = int Function(Gs3dEnginePtr e);

typedef CReadFrameRgba =
    ffi.Int32 Function(
      Gs3dEnginePtr e,
      ffi.Pointer<ffi.Uint8> outRgba,
      ffi.Uint32 maxBytes,
      ffi.Pointer<ffi.Uint32> outW,
      ffi.Pointer<ffi.Uint32> outH,
      ffi.Pointer<ffi.Uint32> outStride,
      ffi.Pointer<ffi.Uint64> outSequence,
    );
typedef DartReadFrameRgba =
    int Function(
      Gs3dEnginePtr e,
      ffi.Pointer<ffi.Uint8> outRgba,
      int maxBytes,
      ffi.Pointer<ffi.Uint32> outW,
      ffi.Pointer<ffi.Uint32> outH,
      ffi.Pointer<ffi.Uint32> outStride,
      ffi.Pointer<ffi.Uint64> outSequence,
    );

typedef CFrameSequence = ffi.Uint64 Function(Gs3dEnginePtr e);
typedef DartFrameSequence = int Function(Gs3dEnginePtr e);

typedef CLastRenderMs = ffi.Double Function(Gs3dEnginePtr e);
typedef DartLastRenderMs = double Function(Gs3dEnginePtr e);

typedef CGetDiagnosticsJson = ffi.Pointer<Utf8> Function(Gs3dEnginePtr e);
typedef DartGetDiagnosticsJson = ffi.Pointer<Utf8> Function(Gs3dEnginePtr e);

typedef CLastError = ffi.Pointer<Utf8> Function(Gs3dEnginePtr e);
typedef DartLastError = ffi.Pointer<Utf8> Function(Gs3dEnginePtr e);

typedef CPointCount = ffi.Uint64 Function(Gs3dEnginePtr e);
typedef DartPointCount = int Function(Gs3dEnginePtr e);

typedef CEngineAbiVersion = ffi.Uint32 Function();
typedef DartEngineAbiVersion = int Function();

typedef CPollState =
    ffi.Int32 Function(Gs3dEnginePtr e, ffi.Pointer<Gs3dStateNative> out);
typedef DartPollState =
    int Function(Gs3dEnginePtr e, ffi.Pointer<Gs3dStateNative> out);

typedef COpenDataset =
    ffi.Int32 Function(
      Gs3dEnginePtr e,
      ffi.Pointer<Utf8> path,
      ffi.Pointer<ffi.Uint64> outTask,
    );
typedef DartOpenDataset =
    int Function(
      Gs3dEnginePtr e,
      ffi.Pointer<Utf8> path,
      ffi.Pointer<ffi.Uint64> outTask,
    );

typedef CCancel = ffi.Int32 Function(Gs3dEnginePtr e, ffi.Uint64 task);
typedef DartCancel = int Function(Gs3dEnginePtr e, int task);

typedef CPickPoint =
    ffi.Int32 Function(
      Gs3dEnginePtr e,
      ffi.Float screenX,
      ffi.Float screenY,
      ffi.Float tolerancePx,
      ffi.Pointer<ffi.Float> outPointXyzv,
    );
typedef DartPickPoint =
    int Function(
      Gs3dEnginePtr e,
      double screenX,
      double screenY,
      double tolerancePx,
      ffi.Pointer<ffi.Float> outPointXyzv,
    );

typedef CSubmitCommand =
    ffi.Int32 Function(
      Gs3dEnginePtr e,
      ffi.Pointer<Gs3dCommandNative> command,
      ffi.Pointer<ffi.Uint64> outTask,
    );
typedef DartSubmitCommand =
    int Function(
      Gs3dEnginePtr e,
      ffi.Pointer<Gs3dCommandNative> command,
      ffi.Pointer<ffi.Uint64> outTask,
    );

/// 低层动态链接库函数表
class Gs3dBindings {
  final ffi.DynamicLibrary lib;

  late final DartVersion version;
  late final DartLastGlobalError lastGlobalError;
  late final DartCreate create;
  late final DartCreateSimple createSimple;
  late final DartDestroy destroy;
  late final DartLoadDataset loadDataset;
  late final DartLoadSample loadSample;
  late final DartLoadSynthetic loadSynthetic;
  late final DartSetPointBudget setPointBudget;
  late final DartSourcePointCount sourcePointCount;
  late final DartDrawnPointCount drawnPointCount;
  late final DartUnloadDataset unloadDataset;
  late final DartResize resize;
  late final DartSetPointSize setPointSize;
  late final DartSetViewScale setViewScale;
  late final DartSetColormap setColormap;
  late final DartSetPointShape setPointShape;
  late final DartSetBackground setBackground;
  late final DartRenderRgba renderRgba;
  late final DartRequestFrame requestFrame;
  late final DartReadFrameRgba readFrameRgba;
  late final DartFrameSequence frameSequence;
  late final DartLastRenderMs lastRenderMs;
  late final DartCameraInput cameraInput;
  late final DartResetCamera resetCamera;
  late final DartGetDiagnosticsJson getDiagnosticsJson;
  late final DartLastError lastError;
  late final DartPointCount pointCount;
  late final DartEngineAbiVersion abiVersion;
  late final DartPollState pollState;
  late final DartOpenDataset openDataset;
  late final DartCancel cancel;
  late final DartSubmitCommand submitCommand;
  late final DartPickPoint pickPoint;

  Gs3dBindings(this.lib) {
    version = lib.lookupFunction<CVersion, DartVersion>('gs3d_engine_version');
    lastGlobalError = lib.lookupFunction<CLastGlobalError, DartLastGlobalError>(
      'gs3d_engine_last_global_error',
    );
    create = lib.lookupFunction<CCreate, DartCreate>('gs3d_engine_create');
    createSimple = lib.lookupFunction<CCreateSimple, DartCreateSimple>(
      'gs3d_engine_create_simple',
    );
    destroy = lib.lookupFunction<CDestroy, DartDestroy>('gs3d_engine_destroy');
    loadDataset = lib.lookupFunction<CLoadDataset, DartLoadDataset>(
      'gs3d_engine_load_dataset',
    );
    loadSample = lib.lookupFunction<CLoadSample, DartLoadSample>(
      'gs3d_engine_load_sample',
    );
    loadSynthetic = lib.lookupFunction<CLoadSynthetic, DartLoadSynthetic>(
      'gs3d_engine_load_synthetic',
    );
    setPointBudget = lib.lookupFunction<CSetPointBudget, DartSetPointBudget>(
      'gs3d_engine_set_point_budget',
    );
    sourcePointCount = lib.lookupFunction<CSourcePointCount, DartSourcePointCount>(
      'gs3d_engine_source_point_count',
    );
    drawnPointCount = lib.lookupFunction<CDrawnPointCount, DartDrawnPointCount>(
      'gs3d_engine_drawn_point_count',
    );
    unloadDataset = lib.lookupFunction<CUnloadDataset, DartUnloadDataset>(
      'gs3d_engine_unload_dataset',
    );
    resize = lib.lookupFunction<CResize, DartResize>('gs3d_engine_resize');
    setPointSize = lib.lookupFunction<CSetPointSize, DartSetPointSize>(
      'gs3d_engine_set_point_size',
    );
    setViewScale = lib.lookupFunction<CSetViewScale, DartSetViewScale>(
      'gs3d_engine_set_view_scale',
    );
    setColormap = lib.lookupFunction<CSetColormap, DartSetColormap>(
      'gs3d_engine_set_colormap_index',
    );
    setPointShape = lib.lookupFunction<CSetPointShape, DartSetPointShape>(
      'gs3d_engine_set_point_shape',
    );
    setBackground = lib.lookupFunction<CSetBackground, DartSetBackground>(
      'gs3d_engine_set_background',
    );
    renderRgba = lib.lookupFunction<CRenderRgba, DartRenderRgba>(
      'gs3d_engine_render_rgba',
    );
    requestFrame = lib.lookupFunction<CRequestFrame, DartRequestFrame>(
      'gs3d_engine_request_frame',
    );
    readFrameRgba = lib.lookupFunction<CReadFrameRgba, DartReadFrameRgba>(
      'gs3d_engine_read_frame_rgba',
    );
    frameSequence = lib.lookupFunction<CFrameSequence, DartFrameSequence>(
      'gs3d_engine_frame_sequence',
    );
    lastRenderMs = lib.lookupFunction<CLastRenderMs, DartLastRenderMs>(
      'gs3d_engine_last_render_ms',
    );
    cameraInput = lib.lookupFunction<CCameraInput, DartCameraInput>(
      'gs3d_engine_camera_input',
    );
    resetCamera = lib.lookupFunction<CResetCamera, DartResetCamera>(
      'gs3d_engine_reset_camera',
    );
    getDiagnosticsJson = lib
        .lookupFunction<CGetDiagnosticsJson, DartGetDiagnosticsJson>(
          'gs3d_engine_get_diagnostics_json',
        );
    lastError = lib.lookupFunction<CLastError, DartLastError>(
      'gs3d_engine_last_error',
    );
    pointCount = lib.lookupFunction<CPointCount, DartPointCount>(
      'gs3d_engine_point_count',
    );
    abiVersion = lib.lookupFunction<CEngineAbiVersion, DartEngineAbiVersion>(
      'gs3d_engine_abi_version',
    );
    pollState = lib.lookupFunction<CPollState, DartPollState>(
      'gs3d_engine_poll_state',
    );
    openDataset = lib.lookupFunction<COpenDataset, DartOpenDataset>(
      'gs3d_engine_open_dataset',
    );
    cancel = lib.lookupFunction<CCancel, DartCancel>('gs3d_engine_cancel');
    submitCommand = lib.lookupFunction<CSubmitCommand, DartSubmitCommand>(
      'gs3d_engine_submit',
    );
    pickPoint = lib.lookupFunction<CPickPoint, DartPickPoint>(
      'gs3d_engine_pick_point',
    );
  }

  static ffi.DynamicLibrary openLibrary() {
    if (Platform.isAndroid) {
      return ffi.DynamicLibrary.open('libgs3d_ffi.so');
    }
    if (Platform.isWindows) {
      final exeDir = File(Platform.resolvedExecutable).parent.path;
      final candidates = [
        '$exeDir\\gs3d_ffi.dll',
        'gs3d_ffi.dll',
        'build/windows/x64/runner/Release/gs3d_ffi.dll',
        '../tmp/build-win/src/ffi/Release/gs3d_ffi.dll',
        '../../tmp/build-win/src/ffi/Release/gs3d_ffi.dll',
        'tmp/build-win/src/ffi/Release/gs3d_ffi.dll',
      ];
      for (final path in candidates) {
        if (File(path).existsSync()) {
          try {
            return ffi.DynamicLibrary.open(path);
          } catch (_) {}
        }
      }
      return ffi.DynamicLibrary.open('gs3d_ffi.dll');
    }
    if (Platform.isLinux) {
      final exeDir = File(Platform.resolvedExecutable).parent.path;
      final bundled = File('$exeDir/lib/libgs3d_ffi.so');
      return ffi.DynamicLibrary.open(
        bundled.existsSync() ? bundled.path : 'libgs3d_ffi.so',
      );
    }
    if (Platform.isMacOS || Platform.isIOS) {
      return ffi.DynamicLibrary.open('libgs3d_ffi.dylib');
    }
    throw UnsupportedError('Unsupported platform: ${Platform.operatingSystem}');
  }
}
