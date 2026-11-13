import 'dart:convert';
import 'dart:io';

import 'package:flutter/services.dart';

/// 应用状态目录：任务记录、显示设置等可重建的本地文件都放这里。
Future<Directory> appStateDirectory() async {
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
    throw StateError('无法确定应用状态目录');
  }
  return Directory('$base${Platform.pathSeparator}GeoScatter3D');
}

/// 先写临时文件再改名：掉电或崩溃时不会留下半截 JSON。
Future<void> writeJsonAtomic(File file, Object value) async {
  await file.parent.create(recursive: true);
  final temp = File(
    '${file.path}.$pid.${DateTime.now().microsecondsSinceEpoch}.tmp',
  );
  try {
    await temp.writeAsString(jsonEncode(value), flush: true);
    await temp.rename(file.path);
  } finally {
    if (await temp.exists()) await temp.delete();
  }
}
