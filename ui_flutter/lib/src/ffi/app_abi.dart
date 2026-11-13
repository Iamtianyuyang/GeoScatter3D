import 'dart:convert';
import 'dart:ffi' as ffi;

import 'package:ffi/ffi.dart';

import 'gs3d_bindings.dart';

/// 一块可用 GPU。
typedef GpuInfo = ({
  int index,
  String name,
  String type,
  String uuid,
  bool preferred,
  bool active,
});

/// 最近打开的工程包。
typedef RecentProject = ({String path, int lastOpened});

/// 应用态 ABI（GPU 枚举、最近项目）。
///
/// 这些能力与引擎实例无关：首次打开界面时就要能列出 GPU 与最近项目，
/// 因此单独持有一份动态库句柄，不依赖 [Gs3dEngine]。
class Gs3dAppAbi {
  Gs3dAppAbi._(this._bindings);

  static Gs3dAppAbi? _instance;

  /// 惰性打开动态库。失败（例如缺少 JNI 库）时返回 null，调用方按空列表处理。
  static Gs3dAppAbi? tryOpen() {
    if (_instance != null) return _instance;
    try {
      _instance = Gs3dAppAbi._(Gs3dBindings(Gs3dBindings.openLibrary()));
    } catch (_) {
      return null;
    }
    return _instance;
  }

  final Gs3dBindings _bindings;

  List<GpuInfo> gpus() {
    final raw = _decodeList(_bindings.gpusJson().toDartString());
    return [
      for (final item in raw)
        (
          index: (item['index'] as num?)?.toInt() ?? 0,
          name: item['name'] as String? ?? '未知设备',
          type: item['type'] as String? ?? '',
          uuid: item['uuid'] as String? ?? '',
          preferred: item['preferred'] as bool? ?? false,
          active: item['active'] as bool? ?? false,
        ),
    ];
  }

  /// 记住首选 GPU；-1 表示清除。下次创建引擎时生效。
  int setPreferredGpu(int index) {
    try {
      return _bindings.setPreferredGpu(index);
    } catch (_) {
      return -3;
    }
  }

  List<RecentProject> recentProjects() {
    final raw = _decodeList(_bindings.recentProjectsJson().toDartString());
    return [
      for (final item in raw)
        (
          path: item['path'] as String? ?? '',
          lastOpened: (item['last_opened'] as num?)?.toInt() ?? 0,
        ),
    ].where((entry) => entry.path.isNotEmpty).toList();
  }

  int remember(String path) => _withPath(path, _bindings.rememberProject);

  int forget(String path) => _withPath(path, _bindings.forgetProject);

  int clearRecent() {
    try {
      return _bindings.clearRecentProjects();
    } catch (_) {
      return -3;
    }
  }

  int _withPath(String path, int Function(ffi.Pointer<Utf8>) call) {
    if (path.isEmpty) return -1;
    final pointer = path.toNativeUtf8();
    try {
      return call(pointer);
    } finally {
      calloc.free(pointer);
    }
  }

  List<Map<String, dynamic>> _decodeList(String text) {
    if (text.isEmpty) return const [];
    try {
      final value = json.decode(text);
      if (value is List) {
        return [for (final item in value) item as Map<String, dynamic>];
      }
    } catch (_) {
      // 坏 JSON 时按空列表处理：列表不是关键路径。
    }
    return const [];
  }
}
