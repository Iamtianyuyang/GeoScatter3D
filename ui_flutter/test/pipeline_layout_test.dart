import 'dart:ffi' as ffi;
import 'dart:io';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/ffi/pipeline_bindings.dart';

void main() {
  test('pipeline ABI layout matches the native 64-bit structs', () {
    expect(ffi.sizeOf<ffi.IntPtr>(), 8);
    expect(ffi.sizeOf<PipelineConfigNative>(), 4);
    expect(ffi.sizeOf<PipelineJobNative>(), 40);
    expect(ffi.sizeOf<PipelineStatusNative>(), 328);
    final status = calloc<PipelineStatusNative>();
    try {
      status.ref.taskId = 42;
      status.ref.stage[0] = 65;
      status.ref.error[0] = 66;
      status.ref.points = 123;
      final bytes = status.cast<ffi.Uint8>().asTypedList(328);
      final data = ByteData.sublistView(bytes);
      expect(data.getUint64(8, Endian.host), 42);
      expect(bytes[20], 65);
      expect(bytes[52], 66);
      expect(data.getUint64(312, Endian.host), 123);
    } finally {
      calloc.free(status);
    }
  }, skip: ffi.sizeOf<ffi.IntPtr>() != 8 ? '64-bit ABI layout' : false);

  final libraryPath = Platform.environment['GS3D_PIPELINE_LIBRARY'];
  test(
    'Dart can run a pipeline job through the native ABI',
    () async {
      final lib = ffi.DynamicLibrary.open(libraryPath!);
      final create = lib.lookupFunction<PipelineCreateNative, PipelineCreate>(
        'gs3d_pipeline_create',
      );
      final start = lib.lookupFunction<PipelineStartNative, PipelineStart>(
        'gs3d_pipeline_start',
      );
      final poll = lib.lookupFunction<PipelinePollNative, PipelinePoll>(
        'gs3d_pipeline_poll',
      );
      final destroy = lib
          .lookupFunction<PipelineDestroyNative, PipelineDestroy>(
            'gs3d_pipeline_destroy',
          );
      final dir = await Directory.systemTemp.createTemp('gs3d-dart-pipeline-');
      final source = File('${dir.path}/input.csv');
      await source.writeAsString('X,Y,Z,elevation\n1,2,3,4\n');
      final sourcePath = source.path.toNativeUtf8();
      final bundlePath = '${dir.path}/bundle'.toNativeUtf8();
      final config = calloc<PipelineConfigNative>();
      final job = calloc<PipelineJobNative>();
      final status = calloc<PipelineStatusNative>();
      final task = calloc<ffi.Uint64>();
      config.ref.structSize = ffi.sizeOf<PipelineConfigNative>();
      final pipeline = create(config);
      expect(pipeline, isNot(ffi.nullptr));
      try {
        job.ref.structSize = ffi.sizeOf<PipelineJobNative>();
        job.ref.sourcePath = sourcePath;
        job.ref.bundlePath = bundlePath;
        job.ref.pointsPerChunk = 8;
        expect(start(pipeline, job, task), 0);
        status.ref.structSize = ffi.sizeOf<PipelineStatusNative>();
        final deadline = DateTime.now().add(const Duration(seconds: 10));
        do {
          expect(poll(pipeline, status), 0);
          if (status.ref.state != 1) break;
          await Future<void>.delayed(const Duration(milliseconds: 2));
        } while (DateTime.now().isBefore(deadline));
        expect(status.ref.taskId, task.value);
        expect(status.ref.state, 2);
        expect(status.ref.points, 1);
      } finally {
        destroy(pipeline);
        calloc.free(task);
        calloc.free(status);
        calloc.free(job);
        calloc.free(config);
        calloc.free(bundlePath);
        calloc.free(sourcePath);
        await dir.delete(recursive: true);
      }
    },
    skip: libraryPath == null
        ? 'Set GS3D_PIPELINE_LIBRARY to test native calls'
        : false,
  );
}
