import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/ffi/pipeline_history.dart';

void main() {
  test(
    'persists the last source and bundle, including Unicode paths',
    () async {
      final directory = await Directory.systemTemp.createTemp('gs3d-history-');
      final file = File('${directory.path}/state/last-job.json');
      try {
        expect(await readPipelineHistory(file), isNull);
        await savePipelineHistory(file, '点云.csv', '工程.bundle');
        expect(await readPipelineHistory(file), ('点云.csv', '工程.bundle'));
        await savePipelineHistory(file, 'other.dat', 'next.bundle');
        expect(await readPipelineHistory(file), ('other.dat', 'next.bundle'));
        await file.writeAsString('{"source": 7}');
        await expectLater(readPipelineHistory(file), throwsFormatException);
      } finally {
        await directory.delete(recursive: true);
      }
    },
  );
}
