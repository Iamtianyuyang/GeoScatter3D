import 'dart:async';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/ffi/pipeline_service.dart';

void main() {
  final libraryPath = Platform.environment['GS3D_PIPELINE_LIBRARY'];

  test(
    'service publishes a completed job and resumes from its bundle',
    () async {
      final directory = await Directory.systemTemp.createTemp('gs3d-service-');
      final source = File('${directory.path}/source.csv');
      final bundle = '${directory.path}/bundle';
      await source.writeAsString('X,Y,Z,value\n1,2,3,4\n');
      final service = PipelineService(libraryPath: libraryPath);
      Future<PipelineStatus> waitForTerminal() async {
        final complete = Completer<PipelineStatus>();
        void listener() {
          if (service.status.state != PipelineState.running &&
              !complete.isCompleted) {
            complete.complete(service.status);
          }
        }

        service.addListener(listener);
        listener();
        try {
          return await complete.future.timeout(const Duration(seconds: 10));
        } finally {
          service.removeListener(listener);
        }
      }

      try {
        await service.start(source.path, bundle);
        final first = await waitForTerminal();
        expect(first.state, PipelineState.done);
        expect(first.points, 1);
        await service.resume(bundle);
        final resumed = await waitForTerminal();
        expect(resumed.state, PipelineState.done);
        expect(resumed.id, isNot(first.id));
        expect(resumed.points, 1);
      } finally {
        service.dispose();
        await service.closed;
        await directory.delete(recursive: true);
      }
    },
    skip: libraryPath == null ? 'Set GS3D_PIPELINE_LIBRARY' : false,
  );

  test('disposing a running service safely stops polling', () async {
    final directory = await Directory.systemTemp.createTemp('gs3d-dispose-');
    final source = File('${directory.path}/source.csv');
    await source.writeAsString('X,Y,Z,value\n1,2,3,4\n');
    final service = PipelineService(libraryPath: libraryPath);
    await service.start(source.path, '${directory.path}/bundle');
    service.dispose();
    await service.closed;
    await Future<void>.delayed(const Duration(milliseconds: 300));
    await directory.delete(recursive: true);
  }, skip: libraryPath == null ? 'Set GS3D_PIPELINE_LIBRARY' : false);
}
