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

typedef _c_get_recent_project_count = ffi.Int32 Function();
typedef _dart_get_recent_project_count = int Function();

typedef _c_get_recent_project_path = ffi.Pointer<Utf8> Function(ffi.Int32 index);
typedef _dart_get_recent_project_path = ffi.Pointer<Utf8> Function(int index);

typedef _c_get_recent_project_timestamp = ffi.Int64 Function(ffi.Int32 index);
typedef _dart_get_recent_project_timestamp = int Function(int index);

typedef _c_remember_recent_project = ffi.Void Function(ffi.Pointer<Utf8> path);
typedef _dart_remember_recent_project = void Function(ffi.Pointer<Utf8> path);

typedef _c_clear_recent_projects = ffi.Void Function();
typedef _dart_clear_recent_projects = void Function();

typedef _c_get_gpu_count = ffi.Int32 Function();
typedef _dart_get_gpu_count = int Function();

typedef _c_get_gpu_name = ffi.Pointer<Utf8> Function(ffi.Int32 index);
typedef _dart_get_gpu_name = ffi.Pointer<Utf8> Function(int index);

typedef _c_get_gpu_type = ffi.Pointer<Utf8> Function(ffi.Int32 index);
typedef _dart_get_gpu_type = ffi.Pointer<Utf8> Function(int index);

typedef _c_get_active_gpu_index = ffi.Int32 Function();
typedef _dart_get_active_gpu_index = int Function();

typedef _c_set_preferred_gpu = ffi.Void Function(ffi.Int32 index);
typedef _dart_set_preferred_gpu = void Function(int index);

typedef _c_get_points = ffi.Int32 Function(ffi.Pointer<ffi.Float> out_buffer, ffi.Int32 max_points);
typedef _dart_get_points = int Function(ffi.Pointer<ffi.Float> out_buffer, int max_points);

typedef _c_set_point_size = ffi.Void Function(ffi.Float size);
typedef _dart_set_point_size = void Function(double size);

typedef _c_get_point_size = ffi.Float Function();
typedef _dart_get_point_size = double Function();

typedef _c_set_colormap = ffi.Void Function(ffi.Pointer<Utf8> colormap_name);
typedef _dart_set_colormap = void Function(ffi.Pointer<Utf8> colormap_name);

typedef _c_get_colormap = ffi.Pointer<Utf8> Function();
typedef _dart_get_colormap = ffi.Pointer<Utf8> Function();

typedef _c_set_scalar_range = ffi.Void Function(ffi.Float min_val, ffi.Float max_val);
typedef _dart_set_scalar_range = void Function(double min_val, double max_val);

typedef _c_get_scalar_range = ffi.Void Function(ffi.Pointer<ffi.Float> out_min, ffi.Pointer<ffi.Float> out_max);
typedef _dart_get_scalar_range = void Function(ffi.Pointer<ffi.Float> out_min, ffi.Pointer<ffi.Float> out_max);

typedef _c_pick_file = ffi.Pointer<Utf8> Function(ffi.Pointer<Utf8> filter_type);
typedef _dart_pick_file = ffi.Pointer<Utf8> Function(ffi.Pointer<Utf8> filter_type);

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

  late final _dart_get_recent_project_count get_recent_project_count;
  late final _dart_get_recent_project_path get_recent_project_path;
  late final _dart_get_recent_project_timestamp get_recent_project_timestamp;
  late final _dart_remember_recent_project remember_recent_project;
  late final _dart_clear_recent_projects clear_recent_projects;

  late final _dart_get_gpu_count get_gpu_count;
  late final _dart_get_gpu_name get_gpu_name;
  late final _dart_get_gpu_type get_gpu_type;
  late final _dart_get_active_gpu_index get_active_gpu_index;
  late final _dart_set_preferred_gpu set_preferred_gpu;

  late final _dart_get_points get_points;
  late final _dart_set_point_size set_point_size;
  late final _dart_get_point_size get_point_size;
  late final _dart_set_colormap set_colormap;
  late final _dart_get_colormap get_colormap;
  late final _dart_set_scalar_range set_scalar_range;
  late final _dart_get_scalar_range get_scalar_range;
  late final _dart_pick_file pick_file;

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

    get_recent_project_count = dylib.lookupFunction<_c_get_recent_project_count, _dart_get_recent_project_count>('gs3d_ffi_get_recent_project_count');
    get_recent_project_path = dylib.lookupFunction<_c_get_recent_project_path, _dart_get_recent_project_path>('gs3d_ffi_get_recent_project_path');
    get_recent_project_timestamp = dylib.lookupFunction<_c_get_recent_project_timestamp, _dart_get_recent_project_timestamp>('gs3d_ffi_get_recent_project_timestamp');
    remember_recent_project = dylib.lookupFunction<_c_remember_recent_project, _dart_remember_recent_project>('gs3d_ffi_remember_recent_project');
    clear_recent_projects = dylib.lookupFunction<_c_clear_recent_projects, _dart_clear_recent_projects>('gs3d_ffi_clear_recent_projects');

    get_gpu_count = dylib.lookupFunction<_c_get_gpu_count, _dart_get_gpu_count>('gs3d_ffi_get_gpu_count');
    get_gpu_name = dylib.lookupFunction<_c_get_gpu_name, _dart_get_gpu_name>('gs3d_ffi_get_gpu_name');
    get_gpu_type = dylib.lookupFunction<_c_get_gpu_type, _dart_get_gpu_type>('gs3d_ffi_get_gpu_type');
    get_active_gpu_index = dylib.lookupFunction<_c_get_active_gpu_index, _dart_get_active_gpu_index>('gs3d_ffi_get_active_gpu_index');
    set_preferred_gpu = dylib.lookupFunction<_c_set_preferred_gpu, _dart_set_preferred_gpu>('gs3d_ffi_set_preferred_gpu');

    get_points = dylib.lookupFunction<_c_get_points, _dart_get_points>('gs3d_ffi_get_points');
    set_point_size = dylib.lookupFunction<_c_set_point_size, _dart_set_point_size>('gs3d_ffi_set_point_size');
    get_point_size = dylib.lookupFunction<_c_get_point_size, _dart_get_point_size>('gs3d_ffi_get_point_size');
    set_colormap = dylib.lookupFunction<_c_set_colormap, _dart_set_colormap>('gs3d_ffi_set_colormap');
    get_colormap = dylib.lookupFunction<_c_get_colormap, _dart_get_colormap>('gs3d_ffi_get_colormap');
    set_scalar_range = dylib.lookupFunction<_c_set_scalar_range, _dart_set_scalar_range>('gs3d_ffi_set_scalar_range');
    get_scalar_range = dylib.lookupFunction<_c_get_scalar_range, _dart_get_scalar_range>('gs3d_ffi_get_scalar_range');
    pick_file = dylib.lookupFunction<_c_pick_file, _dart_pick_file>('gs3d_ffi_pick_file');
  }

  /// 自动探测并加载动态库
  static GeoScatter3dBindings openLibrary([String? customPath]) {
    if (customPath != null && File(customPath).existsSync()) {
      return GeoScatter3dBindings(ffi.DynamicLibrary.open(customPath));
    }

    if (Platform.isWindows) {
      final exeDir = File(Platform.resolvedExecutable).parent.path;
      final candidates = [
        '$exeDir/gs3d_ffi.dll',
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
