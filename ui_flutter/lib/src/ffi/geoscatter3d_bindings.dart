// ignore_for_file: camel_case_types, non_constant_identifier_names, library_private_types_in_public_api

import 'dart:ffi' as ffi;
import 'dart:io';
import 'package:ffi/ffi.dart';

// Native function typedefs
typedef _c_init = ffi.Int32 Function();
typedef _dart_init = int Function();

typedef _c_shutdown = ffi.Void Function();
typedef _dart_shutdown = void Function();

typedef _c_get_version = ffi.Pointer<Utf8> Function();
typedef _dart_get_version = ffi.Pointer<Utf8> Function();

typedef _c_load_dataset = ffi.Int32 Function(ffi.Pointer<Utf8> path);
typedef _dart_load_dataset = int Function(ffi.Pointer<Utf8> path);

typedef _c_is_dataset_loaded = ffi.Int32 Function();
typedef _dart_is_dataset_loaded = int Function();

typedef _c_get_dataset_name = ffi.Pointer<Utf8> Function();
typedef _dart_get_dataset_name = ffi.Pointer<Utf8> Function();

typedef _c_get_point_count = ffi.Uint64 Function();
typedef _dart_get_point_count = int Function();

typedef _c_get_file_size_string = ffi.Pointer<Utf8> Function();
typedef _dart_get_file_size_string = ffi.Pointer<Utf8> Function();

typedef _c_get_bounding_box = ffi.Void Function(
    ffi.Pointer<ffi.Float> out_min, ffi.Pointer<ffi.Float> out_max);
typedef _dart_get_bounding_box = void Function(
    ffi.Pointer<ffi.Float> out_min, ffi.Pointer<ffi.Float> out_max);

typedef _c_get_value_range = ffi.Void Function(
    ffi.Pointer<ffi.Float> out_min, ffi.Pointer<ffi.Float> out_max);
typedef _dart_get_value_range = void Function(
    ffi.Pointer<ffi.Float> out_min, ffi.Pointer<ffi.Float> out_max);

typedef _c_get_lod_enabled = ffi.Int32 Function();
typedef _dart_get_lod_enabled = int Function();

typedef _c_get_lod_level_count = ffi.Int32 Function();
typedef _dart_get_lod_level_count = int Function();

typedef _c_get_lod_level_detail = ffi.Pointer<Utf8> Function(ffi.Int32 index);
typedef _dart_get_lod_level_detail = ffi.Pointer<Utf8> Function(int index);

typedef _c_get_attribute_count = ffi.Int32 Function();
typedef _dart_get_attribute_count = int Function();

typedef _c_get_attribute_name = ffi.Pointer<Utf8> Function(ffi.Int32 index);
typedef _dart_get_attribute_name = ffi.Pointer<Utf8> Function(int index);

typedef _c_execute_command = ffi.Pointer<Utf8> Function(ffi.Pointer<Utf8> json_req);
typedef _dart_execute_command = ffi.Pointer<Utf8> Function(ffi.Pointer<Utf8> json_req);

typedef _c_free_string = ffi.Void Function(ffi.Pointer<Utf8> ptr);
typedef _dart_free_string = void Function(ffi.Pointer<Utf8> ptr);

/// 底层 C-ABI 动态链接库直接绑定
class GeoScatter3dBindings {
  final ffi.DynamicLibrary dylib;

  late final _dart_init init;
  late final _dart_shutdown shutdown;
  late final _dart_get_version get_version;
  late final _dart_load_dataset load_dataset;
  late final _dart_is_dataset_loaded is_dataset_loaded;
  late final _dart_get_dataset_name get_dataset_name;
  late final _dart_get_point_count get_point_count;
  late final _dart_get_file_size_string get_file_size_string;
  late final _dart_get_bounding_box get_bounding_box;
  late final _dart_get_value_range get_value_range;
  late final _dart_get_lod_enabled get_lod_enabled;
  late final _dart_get_lod_level_count get_lod_level_count;
  late final _dart_get_lod_level_detail get_lod_level_detail;
  late final _dart_get_attribute_count get_attribute_count;
  late final _dart_get_attribute_name get_attribute_name;
  late final _dart_execute_command execute_command;
  late final _dart_free_string free_string;

  GeoScatter3dBindings(this.dylib) {
    init = dylib.lookupFunction<_c_init, _dart_init>('gs3d_ffi_init');
    shutdown = dylib.lookupFunction<_c_shutdown, _dart_shutdown>('gs3d_ffi_shutdown');
    get_version = dylib.lookupFunction<_c_get_version, _dart_get_version>('gs3d_ffi_get_version');
    load_dataset = dylib.lookupFunction<_c_load_dataset, _dart_load_dataset>('gs3d_ffi_load_dataset');
    is_dataset_loaded = dylib.lookupFunction<_c_is_dataset_loaded, _dart_is_dataset_loaded>('gs3d_ffi_is_dataset_loaded');
    get_dataset_name = dylib.lookupFunction<_c_get_dataset_name, _dart_get_dataset_name>('gs3d_ffi_get_dataset_name');
    get_point_count = dylib.lookupFunction<_c_get_point_count, _dart_get_point_count>('gs3d_ffi_get_point_count');
    get_file_size_string = dylib.lookupFunction<_c_get_file_size_string, _dart_get_file_size_string>('gs3d_ffi_get_file_size_string');
    get_bounding_box = dylib.lookupFunction<_c_get_bounding_box, _dart_get_bounding_box>('gs3d_ffi_get_bounding_box');
    get_value_range = dylib.lookupFunction<_c_get_value_range, _dart_get_value_range>('gs3d_ffi_get_value_range');
    get_lod_enabled = dylib.lookupFunction<_c_get_lod_enabled, _dart_get_lod_enabled>('gs3d_ffi_get_lod_enabled');
    get_lod_level_count = dylib.lookupFunction<_c_get_lod_level_count, _dart_get_lod_level_count>('gs3d_ffi_get_lod_level_count');
    get_lod_level_detail = dylib.lookupFunction<_c_get_lod_level_detail, _dart_get_lod_level_detail>('gs3d_ffi_get_lod_level_detail');
    get_attribute_count = dylib.lookupFunction<_c_get_attribute_count, _dart_get_attribute_count>('gs3d_ffi_get_attribute_count');
    get_attribute_name = dylib.lookupFunction<_c_get_attribute_name, _dart_get_attribute_name>('gs3d_ffi_get_attribute_name');
    execute_command = dylib.lookupFunction<_c_execute_command, _dart_execute_command>('gs3d_ffi_execute_command');
    free_string = dylib.lookupFunction<_c_free_string, _dart_free_string>('gs3d_ffi_free_string');
  }

  /// 自动探测并加载动态库
  static GeoScatter3dBindings openLibrary([String? customPath]) {
    if (customPath != null && File(customPath).existsSync()) {
      return GeoScatter3dBindings(ffi.DynamicLibrary.open(customPath));
    }

    if (Platform.isWindows) {
      final candidates = [
        'gs3d_ffi.dll',
        '../tmp/build-win/src/ffi/Release/gs3d_ffi.dll',
        'tmp/build-win/src/ffi/Release/gs3d_ffi.dll',
        'D:/code/GeoScatter3D/tmp/build-win/src/ffi/Release/gs3d_ffi.dll',
        'D:/code/GeoScatter3D/ui_flutter/gs3d_ffi.dll',
      ];
      for (final p in candidates) {
        if (File(p).existsSync()) {
          return GeoScatter3dBindings(ffi.DynamicLibrary.open(p));
        }
      }
      return GeoScatter3dBindings(ffi.DynamicLibrary.open('gs3d_ffi.dll'));
    } else if (Platform.isAndroid || Platform.isLinux) {
      return GeoScatter3dBindings(ffi.DynamicLibrary.open('libgs3d_ffi.so'));
    } else if (Platform.isMacOS || Platform.isIOS) {
      return GeoScatter3dBindings(ffi.DynamicLibrary.process());
    }
    throw UnsupportedError('Unsupported platform: ${Platform.operatingSystem}');
  }
}
