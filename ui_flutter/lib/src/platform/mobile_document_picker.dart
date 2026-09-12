import 'package:flutter/services.dart';

/// Android Storage Access Framework 的最小桥接。
///
/// 原生层会把 content:// 文档复制到应用私有持久目录后再返回普通路径，
/// 使后续工程加载仍然沿用 GeoScatter3dService + C-ABI FFI 路径。
abstract final class MobileDocumentPicker {
  static const MethodChannel _channel = MethodChannel(
    'com.geoscatter3d.ui_flutter/documents',
  );

  static Future<String?> pickFile() async {
    return _channel.invokeMethod<String>('pickFile');
  }

  static Future<String?> pickDirectory() async {
    return _channel.invokeMethod<String>('pickDirectory');
  }

  /// 返回 Android 应用的持久化 filesDir，避免把工程写入 cache/code_cache。
  static Future<String?> persistentDirectory() async {
    return _channel.invokeMethod<String>('getPersistentDirectory');
  }

  /// 打开 Android 系统保存面板，并把应用私有临时文件复制到用户选择的 Uri。
  static Future<bool> saveFile({
    required String sourcePath,
    required String suggestedName,
    required String mimeType,
  }) async {
    final saved = await _channel.invokeMethod<bool>('saveFile', {
      'sourcePath': sourcePath,
      'suggestedName': suggestedName,
      'mimeType': mimeType,
    });
    return saved == true;
  }
}
