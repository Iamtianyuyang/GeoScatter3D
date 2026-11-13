import 'dart:convert';
import 'dart:io';

import 'package:flutter/services.dart';

Future<File> pipelineHistoryFile() async {
  String? base;
  if (Platform.isAndroid) {
    base = await const MethodChannel(
      'com.geoscatter3d.ui_flutter/documents',
    ).invokeMethod<String>('getPersistentDirectory');
  } else if (Platform.isWindows) {
    base = Platform.environment['APPDATA'];
  } else if (Platform.isLinux) {
    base = Platform.environment['XDG_STATE_HOME'];
    if (base == null || !base.startsWith('/')) {
      final home = Platform.environment['HOME'];
      if (home != null && home.isNotEmpty) base = '$home/.local/state';
    }
  }
  if (base == null || base.isEmpty) {
    throw StateError('无法确定任务记录目录');
  }
  return File(
    '$base${Platform.pathSeparator}GeoScatter3D${Platform.pathSeparator}last-job.json',
  );
}

Future<void> savePipelineHistory(
  File file,
  String source,
  String bundle,
) async {
  await file.parent.create(recursive: true);
  final temp = File(
    '${file.path}.$pid.${DateTime.now().microsecondsSinceEpoch}.tmp',
  );
  try {
    await temp.writeAsString(
      jsonEncode({'source': source, 'bundle': bundle}),
      flush: true,
    );
    await temp.rename(file.path);
  } finally {
    if (await temp.exists()) await temp.delete();
  }
}

Future<(String, String)?> readPipelineHistory(File file) async {
  if (!await file.exists()) return null;
  final value = jsonDecode(await file.readAsString());
  if (value is! Map ||
      value['source'] is! String ||
      value['bundle'] is! String) {
    throw const FormatException('任务记录格式无效');
  }
  return (value['source'] as String, value['bundle'] as String);
}
