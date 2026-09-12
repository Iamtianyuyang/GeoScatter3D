import 'package:ui_flutter/src/ffi/present_surface.dart';

/// 测试替身：记录纹理通路的调用序列，用于验证零拷贝接管/回退/释放。
class FakePresentSurfaceBinder implements PresentSurfaceBinder {
  FakePresentSurfaceBinder({this.textureId = 42});

  /// 返回的纹理 id；注意 0 是合法 id（Flutter 首个纹理就是 0）。
  int textureId;

  /// true 时 attach 抛异常（模拟设备/驱动不支持）。
  bool failAttach = false;

  /// true 时 attach 返回 null（平台层没有纹理通路）。
  bool unsupported = false;

  int attachCount = 0;
  int resizeCount = 0;
  int detachCount = 0;
  final List<int> engineHandles = [];
  final List<(int, int)> attachSizes = [];
  final List<(int, int)> resizeSizes = [];
  int? detachedTextureId;

  @override
  Future<int?> attach({
    required int engineHandle,
    required int width,
    required int height,
  }) async {
    attachCount++;
    engineHandles.add(engineHandle);
    if (failAttach) {
      throw StateError('surface fixture unavailable');
    }
    if (unsupported) return null;
    attachSizes.add((width, height));
    return textureId;
  }

  @override
  Future<void> resize({
    required int textureId,
    required int width,
    required int height,
  }) async {
    resizeCount++;
    resizeSizes.add((width, height));
  }

  @override
  Future<void> detach({required int textureId}) async {
    detachCount++;
    detachedTextureId = textureId;
  }
}
