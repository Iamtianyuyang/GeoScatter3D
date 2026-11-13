import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:ui_flutter/src/ffi/geoscatter3d_service.dart';
import 'support/fake_engine.dart';

void main() {
  test('exports points and the current frame into the export directory', () async {
    final directory = await Directory.systemTemp.createTemp('gs3d-export-');
    addTearDown(() => directory.delete(recursive: true));
    final engine = FakeEngine()..writeExportFile = true;
    final service = GeoScatter3dService(
      engine: engine,
      exportDirectory: () async => directory,
    );
    await service.initialize();
    addTearDown(service.dispose);

    expect(service.canExport, isTrue);
    final ply = await service.exportPoints();
    expect(ply, isNotNull);
    expect(ply!.endsWith('.ply'), isTrue);
    expect(File(ply).readAsStringSync(), 'fake-points');
    expect(engine.exports.single.$2, 'ply');

    final csv = await service.exportPoints(format: 'csv');
    expect(csv, isNotNull);
    expect(csv!.endsWith('.csv'), isTrue);
    expect(engine.exports.last.$2, 'csv');

    final frame = await service.saveScreenshot();
    expect(frame, isNotNull);
    expect(frame!.endsWith('.png'), isTrue);
    expect(File(frame).readAsBytesSync(), engine.framePng);
    expect(service.lastExportPath, frame);
    expect(service.lastExportError, isNull);
    expect(service.exporting, isFalse);
  });

  test('refuses datasets above the synchronous export limit', () async {
    final directory = await Directory.systemTemp.createTemp('gs3d-export-');
    addTearDown(() => directory.delete(recursive: true));
    final engine = FakeEngine()
      ..setLodScenario(source: 5000000, drawn: 100000);
    final service = GeoScatter3dService(
      engine: engine,
      exportDirectory: () async => directory,
    );
    await service.initialize();
    addTearDown(service.dispose);

    expect(service.canExport, isFalse);
    expect(await service.exportPoints(), isNull);
    expect(service.lastExportError, contains('尚未实现'));
    expect(engine.exports, isEmpty);
    expect(await service.saveScreenshot(), isNull);
    expect(engine.exports, isEmpty);
  });

  test('reports native export and missing-frame failures', () async {
    final directory = await Directory.systemTemp.createTemp('gs3d-export-');
    addTearDown(() => directory.delete(recursive: true));
    final engine = FakeEngine()..exportResult = -2;
    final service = GeoScatter3dService(
      engine: engine,
      exportDirectory: () async => directory,
    );
    await service.initialize();
    addTearDown(service.dispose);

    expect(await service.exportPoints(), isNull);
    expect(service.lastExportError, contains('导出失败'));
    expect(service.lastExportPath, isNull);

    engine
      ..exportResult = 0
      ..framePng = null;
    expect(await service.saveScreenshot(), isNull);
    expect(service.lastExportError, contains('没有可保存的帧'));
    expect(service.exporting, isFalse);
  });
}
