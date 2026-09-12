import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

/// 零拷贝呈现通路的平台绑定（Android：Kotlin `SurfaceProducer`）。
///
/// 引擎把交换链直接 present 到 Flutter 提供的 Surface，合成器把它当成普通
/// Flutter 纹理层合成，因此不再需要「GPU 回读 RGBA → 上传纹理」这两步
/// （真机 1M 点 + 瓦片时约 14 ms/帧的 UI 端开销）。
///
/// 失败必须显式返回不可用（[attach] 返回 0 或抛异常），由服务回退到回读路径；
/// 不允许静默地假装纹理通路可用。
abstract class PresentSurfaceBinder {
  /// 建立纹理通路，返回 Flutter 纹理 id；null 表示当前平台/设备不支持。
  ///
  /// 注意：**0 是合法的 Flutter 纹理 id**（注册表的首个纹理就是 0），
  /// 因此「不可用」必须用 null 表达，不能用 0 当哨兵值。
  Future<int?> attach({
    required int engineHandle,
    required int width,
    required int height,
  });

  /// 视口尺寸变化（旋转/分屏）；实现方负责让底层 Surface 与交换链跟上。
  Future<void> resize({
    required int textureId,
    required int width,
    required int height,
  });

  /// 释放纹理通路（引擎句柄必须仍然有效）。
  Future<void> detach({required int textureId});
}

/// Android 实现：走 `com.geoscatter3d.ui_flutter/surface` 通道。
class PlatformPresentSurfaceBinder implements PresentSurfaceBinder {
  const PlatformPresentSurfaceBinder();

  static const MethodChannel _channel = MethodChannel(
    'com.geoscatter3d.ui_flutter/surface',
  );

  @override
  Future<int?> attach({
    required int engineHandle,
    required int width,
    required int height,
  }) async {
    final result = await _channel.invokeMapMethod<Object?, Object?>('attach', {
      'engineHandle': engineHandle,
      'width': width,
      'height': height,
    });
    final id = result?['textureId'];
    if (id is! num) return null;
    return id.toInt();
  }

  @override
  Future<void> resize({
    required int textureId,
    required int width,
    required int height,
  }) async {
    await _channel.invokeMethod<void>('resize', {
      'textureId': textureId,
      'width': width,
      'height': height,
    });
  }

  @override
  Future<void> detach({required int textureId}) async {
    await _channel.invokeMethod<void>('detach', {'textureId': textureId});
  }
}

/// 桌面/测试等没有 Flutter Texture 通路的平台：始终不可用。
class UnsupportedPresentSurfaceBinder implements PresentSurfaceBinder {
  const UnsupportedPresentSurfaceBinder();

  @override
  Future<int?> attach({
    required int engineHandle,
    required int width,
    required int height,
  }) async =>
      null;

  @override
  Future<void> resize({
    required int textureId,
    required int width,
    required int height,
  }) async {}

  @override
  Future<void> detach({required int textureId}) async {}
}

/// 按平台选择默认绑定。
PresentSurfaceBinder defaultPresentSurfaceBinder() {
  if (kIsWeb) return const UnsupportedPresentSurfaceBinder();
  return Platform.isAndroid
      ? const PlatformPresentSurfaceBinder()
      : const UnsupportedPresentSurfaceBinder();
}
