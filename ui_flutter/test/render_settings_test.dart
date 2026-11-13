import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/ffi/render_settings.dart';

void main() {
  test('round-trips display settings and rejects invalid values', () async {
    final directory = await Directory.systemTemp.createTemp('gs3d-settings-');
    final file = File('${directory.path}/render-settings.json');
    try {
      expect(await readRenderSettings(file), isNull);
      await writeRenderSettings(
        file,
        const RenderSettings(colormapIndex: 8, pointSize: 3.25),
      );
      final saved = await readRenderSettings(file);
      expect(saved?.colormapIndex, 8);
      expect(saved?.pointSize, 3.25);

      await file.writeAsString('{"colormap_index": 9, "point_size": 1.5}');
      await expectLater(readRenderSettings(file), throwsFormatException);
      await file.writeAsString('{"colormap_index": 1, "point_size": 42}');
      await expectLater(readRenderSettings(file), throwsFormatException);
      await file.writeAsString('{"colormap_index": "6"}');
      await expectLater(readRenderSettings(file), throwsFormatException);
      // 未写完的临时文件不会被读到：写入是「临时文件 + 改名」。
      expect(
        directory.listSync().where((e) => e.path.endsWith('.tmp')),
        isEmpty,
      );
    } finally {
      await directory.delete(recursive: true);
    }
  });
}
