import 'dart:convert';
import 'dart:io';

import '../ui_flutter/lib/src/native/viewer_control_client.dart';

// 复用 Flutter 实际控制客户端，在隐藏窗口中验证打包运行时；所有产物自清理。
Future<void> main(List<String> args) async {
  if (args.length != 1) {
    throw ArgumentError(
      'Usage: dart scripts/smoke_flutter_native.dart <runtime-directory>',
    );
  }
  final runtime = Directory(args.single).absolute;
  final root = File.fromUri(Platform.script).parent.parent;
  final tempRoot = Directory('${root.path}/tmp')..createSync(recursive: true);
  final temp = tempRoot.createTempSync('flutter-native-smoke-');
  final client = NativeViewerControlClient();
  try {
    final bundle = '${temp.path}/sample.gs3d.bundle';
    final preprocess = await Process.run(
      '${runtime.path}/GeoScatter3DPreprocess.exe',
      [
        '--config',
        '${runtime.path}/config/sample-viewer.toml',
        '--csv',
        '${root.path}/examples/sample-points.csv',
        '--bundle',
        bundle,
      ],
      workingDirectory: temp.path,
    );
    if (preprocess.exitCode != 0) {
      throw StateError('Preprocessing failed: ${preprocess.stderr}');
    }
    if (!await client.start(
      executablePath: '${runtime.path}/GeoScatter3D.exe',
      datasetPath: bundle,
    )) {
      throw StateError('Native viewer did not start');
    }
    await client.request('execute', {
      'component': 'runtime.render_settings',
      'command': 'set',
      'params': {'point_size': 4.0},
    });
    await client.request('execute', {
      'component': 'canvas.viewport',
      'command': 'reset_camera',
      'params': <String, dynamic>{},
    });
    final png = await client.capturePng();
    if (png.length < 8 ||
        png.take(8).join(',') != '137,80,78,71,13,10,26,10' ||
        client.lastCaptureWidth <= 0 ||
        client.lastCaptureHeight <= 0) {
      throw StateError('Invalid native viewport PNG');
    }
    final diagnostics = await client.request('get_state', {
      'component': 'runtime.diagnostics',
    });
    stdout.writeln(
      jsonEncode({
        'result': 'OK',
        'png_bytes': png.length,
        'width': client.lastCaptureWidth,
        'height': client.lastCaptureHeight,
        'diagnostics': diagnostics['result'],
      }),
    );
  } finally {
    await client.stop();
    temp.deleteSync(recursive: true);
  }
}
