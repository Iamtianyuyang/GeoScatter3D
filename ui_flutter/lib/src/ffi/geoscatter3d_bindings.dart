// ignore_for_file: camel_case_types, non_constant_identifier_names, library_private_types_in_public_api

import 'dart:ffi' as ffi;
import 'dart:io';
import 'package:ffi/ffi.dart';

/// 渲染内核动态库加载失败异常（带人类可读原因）。
///
/// Android / Linux 上 `DynamicLibrary.open` 抛出的原始异常被包装为本类型，
/// `GeoScatter3dService.initialize()` 捕获后写入显式「引擎不可用」状态
/// （R3：不静默、不假装可用）。
class Gs3dEngineLoadException implements Exception {
  const Gs3dEngineLoadException(this.reason);

  /// 加载失败原因（面向用户可展示）。
  final String reason;

  @override
  String toString() => 'Gs3dEngineLoadException: $reason';
}

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

typedef _c_get_bounding_box =
    ffi.Void Function(
      ffi.Pointer<ffi.Float> out_min,
      ffi.Pointer<ffi.Float> out_max,
    );
typedef _dart_get_bounding_box =
    void Function(
      ffi.Pointer<ffi.Float> out_min,
      ffi.Pointer<ffi.Float> out_max,
    );

typedef _c_get_value_range =
    ffi.Void Function(
      ffi.Pointer<ffi.Float> out_min,
      ffi.Pointer<ffi.Float> out_max,
    );
typedef _dart_get_value_range =
    void Function(
      ffi.Pointer<ffi.Float> out_min,
      ffi.Pointer<ffi.Float> out_max,
    );

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

typedef _c_execute_command =
    ffi.Pointer<Utf8> Function(ffi.Pointer<Utf8> json_req);
typedef _dart_execute_command =
    ffi.Pointer<Utf8> Function(ffi.Pointer<Utf8> json_req);

typedef _c_free_string = ffi.Void Function(ffi.Pointer<Utf8> ptr);
typedef _dart_free_string = void Function(ffi.Pointer<Utf8> ptr);

typedef _c_get_recent_project_count = ffi.Int32 Function();
typedef _dart_get_recent_project_count = int Function();

typedef _c_get_recent_project_path =
    ffi.Pointer<Utf8> Function(ffi.Int32 index);
typedef _dart_get_recent_project_path = ffi.Pointer<Utf8> Function(int index);

typedef _c_get_recent_project_timestamp = ffi.Int64 Function(ffi.Int32 index);
typedef _dart_get_recent_project_timestamp = int Function(int index);

typedef _c_remember_recent_project = ffi.Void Function(ffi.Pointer<Utf8> path);
typedef _dart_remember_recent_project = void Function(ffi.Pointer<Utf8> path);

typedef _c_remove_recent_project = ffi.Void Function(ffi.Pointer<Utf8> path);
typedef _dart_remove_recent_project = void Function(ffi.Pointer<Utf8> path);

typedef _c_clear_recent_projects = ffi.Void Function();
typedef _dart_clear_recent_projects = void Function();

typedef _c_init_drag_drop = ffi.Void Function();
typedef _dart_init_drag_drop = void Function();

typedef _c_poll_dropped_file = ffi.Pointer<Utf8> Function();
typedef _dart_poll_dropped_file = ffi.Pointer<Utf8> Function();

typedef _c_set_dropped_file = ffi.Void Function(ffi.Pointer<Utf8> path);
typedef _dart_set_dropped_file = void Function(ffi.Pointer<Utf8> path);

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

typedef _c_get_points =
    ffi.Int32 Function(ffi.Pointer<ffi.Float> out_buffer, ffi.Int32 max_points);
typedef _dart_get_points =
    int Function(ffi.Pointer<ffi.Float> out_buffer, int max_points);

typedef _c_set_point_size = ffi.Void Function(ffi.Float size);
typedef _dart_set_point_size = void Function(double size);

typedef _c_get_point_size = ffi.Float Function();
typedef _dart_get_point_size = double Function();

typedef _c_set_colormap = ffi.Void Function(ffi.Pointer<Utf8> colormap_name);
typedef _dart_set_colormap = void Function(ffi.Pointer<Utf8> colormap_name);

typedef _c_get_colormap = ffi.Pointer<Utf8> Function();
typedef _dart_get_colormap = ffi.Pointer<Utf8> Function();

typedef _c_set_scalar_range =
    ffi.Void Function(ffi.Float min_val, ffi.Float max_val);
typedef _dart_set_scalar_range = void Function(double min_val, double max_val);

typedef _c_get_scalar_range =
    ffi.Void Function(
      ffi.Pointer<ffi.Float> out_min,
      ffi.Pointer<ffi.Float> out_max,
    );
typedef _dart_get_scalar_range =
    void Function(
      ffi.Pointer<ffi.Float> out_min,
      ffi.Pointer<ffi.Float> out_max,
    );

typedef _c_pick_file =
    ffi.Pointer<Utf8> Function(ffi.Pointer<Utf8> filter_type);
typedef _dart_pick_file =
    ffi.Pointer<Utf8> Function(ffi.Pointer<Utf8> filter_type);

typedef _c_pick_folder = ffi.Pointer<Utf8> Function();
typedef _dart_pick_folder = ffi.Pointer<Utf8> Function();

typedef _c_get_performance_metrics =
    ffi.Void Function(
      ffi.Pointer<ffi.Float> out_fps,
      ffi.Pointer<ffi.Float> out_frame_time_ms,
      ffi.Pointer<ffi.Uint64> out_visible_points,
      ffi.Pointer<ffi.Uint64> out_gpu_mem_bytes,
      ffi.Pointer<ffi.Uint32> out_loaded_tiles,
      ffi.Pointer<ffi.Uint32> out_pending_tiles,
      ffi.Pointer<ffi.Float> out_cache_hit_rate,
    );
typedef _dart_get_performance_metrics =
    void Function(
      ffi.Pointer<ffi.Float> out_fps,
      ffi.Pointer<ffi.Float> out_frame_time_ms,
      ffi.Pointer<ffi.Uint64> out_visible_points,
      ffi.Pointer<ffi.Uint64> out_gpu_mem_bytes,
      ffi.Pointer<ffi.Uint32> out_loaded_tiles,
      ffi.Pointer<ffi.Uint32> out_pending_tiles,
      ffi.Pointer<ffi.Float> out_cache_hit_rate,
    );

typedef _c_get_camera_coords_string = ffi.Pointer<Utf8> Function();
typedef _dart_get_camera_coords_string = ffi.Pointer<Utf8> Function();

typedef _c_clear_cache = ffi.Void Function();
typedef _dart_clear_cache = void Function();

typedef _c_pick_point =
    ffi.Int32 Function(
      ffi.Float screen_x,
      ffi.Float screen_y,
      ffi.Float viewport_width,
      ffi.Float viewport_height,
      ffi.Float azimuth_deg,
      ffi.Float elevation_deg,
      ffi.Float zoom,
      ffi.Float pan_x,
      ffi.Float pan_y,
      ffi.Pointer<ffi.Float> out_point_xyzv,
    );
typedef _dart_pick_point =
    int Function(
      double screen_x,
      double screen_y,
      double viewport_width,
      double viewport_height,
      double azimuth_deg,
      double elevation_deg,
      double zoom,
      double pan_x,
      double pan_y,
      ffi.Pointer<ffi.Float> out_point_xyzv,
    );

typedef _c_calculate_region_stats =
    ffi.Int32 Function(
      ffi.Float min_x,
      ffi.Float max_x,
      ffi.Float min_y,
      ffi.Float max_y,
      ffi.Pointer<ffi.Uint64> out_count,
      ffi.Pointer<ffi.Float> out_mean,
      ffi.Pointer<ffi.Float> out_min,
      ffi.Pointer<ffi.Float> out_max,
      ffi.Pointer<ffi.Float> out_std_dev,
      ffi.Pointer<ffi.Int32> out_hist_5bins,
    );
typedef _dart_calculate_region_stats =
    int Function(
      double min_x,
      double max_x,
      double min_y,
      double max_y,
      ffi.Pointer<ffi.Uint64> out_count,
      ffi.Pointer<ffi.Float> out_mean,
      ffi.Pointer<ffi.Float> out_min,
      ffi.Pointer<ffi.Float> out_max,
      ffi.Pointer<ffi.Float> out_std_dev,
      ffi.Pointer<ffi.Int32> out_hist_5bins,
    );

typedef _c_get_nav_map_thumbnail =
    ffi.Int32 Function(
      ffi.Pointer<ffi.Uint8> out_rgba,
      ffi.Int32 width,
      ffi.Int32 height,
    );
typedef _dart_get_nav_map_thumbnail =
    int Function(ffi.Pointer<ffi.Uint8> out_rgba, int width, int height);

typedef _c_export_dataset =
    ffi.Int32 Function(
      ffi.Pointer<Utf8> target_path,
      ffi.Pointer<Utf8> format_type,
    );
typedef _dart_export_dataset =
    int Function(ffi.Pointer<Utf8> target_path, ffi.Pointer<Utf8> format_type);

// CSV 预处理接口（移动端直接复用编译进 .so 的 C-ABI）。
typedef _c_preprocess_csv =
    ffi.Int32 Function(
      ffi.Pointer<Utf8> csv_path,
      ffi.Pointer<Utf8> output_gs3d_path,
      ffi.Uint32 threads,
      ffi.Uint32 finest_target_points,
    );
typedef _dart_preprocess_csv =
    int Function(
      ffi.Pointer<Utf8> csv_path,
      ffi.Pointer<Utf8> output_gs3d_path,
      int threads,
      int finest_target_points,
    );

typedef _c_get_last_error = ffi.Pointer<Utf8> Function();
typedef _dart_get_last_error = ffi.Pointer<Utf8> Function();

// ── 单进程渲染内核 ─────────────────────────────────────────────────
typedef _c_engine_create_simple =
    ffi.Pointer<ffi.Void> Function(ffi.Uint32 width, ffi.Uint32 height);
typedef _dart_engine_create_simple =
    ffi.Pointer<ffi.Void> Function(int width, int height);

/// `gs3d_engine_config` 原生结构体。
/// 字段顺序/宽度必须与 `include/ffi/geoscatter3d_ffi.h` §20 严格一致。
final class Gs3dEngineConfig extends ffi.Struct {
  /// 首选 GPU UUID；可空或 "auto"。
  external ffi.Pointer<Utf8> preferred_gpu_uuid;

  @ffi.Uint32()
  external int viewport_width;

  @ffi.Uint32()
  external int viewport_height;

  /// 含 point.vert.spv / point.frag.spv 的着色器目录；可空。
  external ffi.Pointer<Utf8> shader_dir;

  @ffi.Int32()
  external int enable_validation;
}

typedef _c_engine_create =
    ffi.Pointer<ffi.Void> Function(ffi.Pointer<Gs3dEngineConfig> cfg);
typedef _dart_engine_create =
    ffi.Pointer<ffi.Void> Function(ffi.Pointer<Gs3dEngineConfig> cfg);

typedef _c_engine_unload = ffi.Int32 Function(ffi.Pointer<ffi.Void>);
typedef _dart_engine_unload = int Function(ffi.Pointer<ffi.Void>);
typedef _c_engine_ptr = ffi.Void Function(ffi.Pointer<ffi.Void>);
typedef _dart_engine_ptr = void Function(ffi.Pointer<ffi.Void>);
typedef _c_engine_ptr_ret = ffi.Int32 Function(ffi.Pointer<ffi.Void>);
typedef _dart_engine_ptr_ret = int Function(ffi.Pointer<ffi.Void>);
typedef _c_engine_load =
    ffi.Int32 Function(ffi.Pointer<ffi.Void>, ffi.Pointer<Utf8>);
typedef _dart_engine_load =
    int Function(ffi.Pointer<ffi.Void>, ffi.Pointer<Utf8>);
typedef _c_engine_resize =
    ffi.Int32 Function(ffi.Pointer<ffi.Void>, ffi.Uint32, ffi.Uint32);
typedef _dart_engine_resize = int Function(ffi.Pointer<ffi.Void>, int, int);
typedef _c_engine_set_point_size =
    ffi.Int32 Function(ffi.Pointer<ffi.Void>, ffi.Float);
typedef _dart_engine_set_point_size = int Function(ffi.Pointer<ffi.Void>, double);
typedef _c_engine_set_int =
    ffi.Int32 Function(ffi.Pointer<ffi.Void>, ffi.Int32);
typedef _dart_engine_set_int = int Function(ffi.Pointer<ffi.Void>, int);
typedef _c_engine_render_rgba =
    ffi.Int32 Function(
      ffi.Pointer<ffi.Void>,
      ffi.Pointer<ffi.Uint8>,
      ffi.Uint32,
      ffi.Pointer<ffi.Uint32>,
      ffi.Pointer<ffi.Uint32>,
    );
typedef _dart_engine_render_rgba =
    int Function(
      ffi.Pointer<ffi.Void>,
      ffi.Pointer<ffi.Uint8>,
      int,
      ffi.Pointer<ffi.Uint32>,
      ffi.Pointer<ffi.Uint32>,
    );
typedef _c_engine_set_camera_look_at =
    ffi.Int32 Function(
      ffi.Pointer<ffi.Void>,
      ffi.Float,
      ffi.Float,
      ffi.Float,
      ffi.Float,
      ffi.Float,
      ffi.Float,
      ffi.Float,
      ffi.Float,
      ffi.Float,
    );
typedef _dart_engine_set_camera_look_at =
    int Function(
      ffi.Pointer<ffi.Void>,
      double,
      double,
      double,
      double,
      double,
      double,
      double,
      double,
      double,
    );
typedef _c_engine_camera_input =
    ffi.Int32 Function(
      ffi.Pointer<ffi.Void>,
      ffi.Float,
      ffi.Float,
      ffi.Float,
      ffi.Float,
      ffi.Float,
    );
typedef _dart_engine_camera_input =
    int Function(
      ffi.Pointer<ffi.Void>,
      double,
      double,
      double,
      double,
      double,
    );
typedef _c_engine_set_src =
    ffi.Int32 Function(ffi.Pointer<ffi.Void>, ffi.Int32);
typedef _dart_engine_set_src = int Function(ffi.Pointer<ffi.Void>, int);
typedef _c_engine_set_f =
    ffi.Int32 Function(ffi.Pointer<ffi.Void>, ffi.Float);
typedef _dart_engine_set_f = int Function(ffi.Pointer<ffi.Void>, double);
typedef _c_engine_set_clip =
    ffi.Int32 Function(
      ffi.Pointer<ffi.Void>,
      ffi.Int32,
      ffi.Float,
      ffi.Float,
    );
typedef _dart_engine_set_clip =
    int Function(ffi.Pointer<ffi.Void>, int, double, double);
typedef _c_engine_set_bg =
    ffi.Int32 Function(
      ffi.Pointer<ffi.Void>,
      ffi.Float,
      ffi.Float,
      ffi.Float,
      ffi.Float,
    );
typedef _dart_engine_set_bg =
    int Function(ffi.Pointer<ffi.Void>, double, double, double, double);
typedef _c_engine_export =
    ffi.Int32 Function(
      ffi.Pointer<ffi.Void>,
      ffi.Pointer<Utf8>,
      ffi.Pointer<Utf8>,
    );
typedef _dart_engine_export =
    int Function(ffi.Pointer<ffi.Void>, ffi.Pointer<Utf8>, ffi.Pointer<Utf8>);
typedef _c_engine_cstr = ffi.Pointer<Utf8> Function(ffi.Pointer<ffi.Void>);
typedef _dart_engine_cstr = ffi.Pointer<Utf8> Function(ffi.Pointer<ffi.Void>);
typedef _c_engine_point_count = ffi.Uint64 Function(ffi.Pointer<ffi.Void>);
typedef _dart_engine_point_count = int Function(ffi.Pointer<ffi.Void>);
typedef _c_engine_pick_point =
    ffi.Int32 Function(
      ffi.Pointer<ffi.Void>,
      ffi.Float,
      ffi.Float,
      ffi.Float,
      ffi.Pointer<ffi.Float>,
    );
typedef _dart_engine_pick_point =
    int Function(
      ffi.Pointer<ffi.Void>,
      double,
      double,
      double,
      ffi.Pointer<ffi.Float>,
    );
typedef _c_engine_read_frame_png =
    ffi.Int32 Function(
      ffi.Pointer<ffi.Void>,
      ffi.Pointer<ffi.Uint8>,
      ffi.Uint32,
      ffi.Pointer<ffi.Uint32>,
    );
typedef _dart_engine_read_frame_png =
    int Function(
      ffi.Pointer<ffi.Void>,
      ffi.Pointer<ffi.Uint8>,
      int,
      ffi.Pointer<ffi.Uint32>,
    );
typedef _c_engine_cstr_ret = ffi.Pointer<Utf8> Function();
typedef _dart_engine_cstr_ret = ffi.Pointer<Utf8> Function();

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
  late final _dart_remove_recent_project remove_recent_project;
  late final _dart_clear_recent_projects clear_recent_projects;
  late final _dart_init_drag_drop init_drag_drop;
  late final _dart_poll_dropped_file poll_dropped_file;
  late final _dart_set_dropped_file set_dropped_file;

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
  late final _dart_pick_folder pick_folder;

  late final _dart_get_performance_metrics get_performance_metrics;
  late final _dart_get_camera_coords_string get_camera_coords_string;
  late final _dart_clear_cache clear_cache;
  late final _dart_pick_point pick_point;
  late final _dart_calculate_region_stats calculate_region_stats;
  late final _dart_get_nav_map_thumbnail get_nav_map_thumbnail;
  late final _dart_export_dataset export_dataset;
  late final _dart_preprocess_csv _preprocessCsv;
  late final _dart_get_last_error _getLastPreprocessError;

  /// 引擎交互内核是否可用：create/load/render/camera 四项必须齐全，
  /// 否则视为不具备单进程渲染能力（R3：不静默降级为 Dart 预览）。
  bool get hasEngineApi =>
      engine_create_simple_opt != null &&
      engine_load_dataset_opt != null &&
      engine_render_rgba_opt != null &&
      engine_camera_input_opt != null;

  _dart_engine_cstr_ret? engine_last_global_error_opt;
  _dart_engine_create_simple? engine_create_simple_opt;
  _dart_engine_create? engine_create_opt;
  _dart_engine_unload? engine_unload_dataset_opt;

  _dart_engine_ptr? engine_destroy_opt;
  _dart_engine_load? engine_load_dataset_opt;
  _dart_engine_resize? engine_resize_opt;
  _dart_engine_set_point_size? engine_set_point_size_opt;
  _dart_engine_set_int? engine_set_colormap_index_opt;
  _dart_engine_set_int? engine_set_point_shape_opt;
  _dart_engine_set_clip? engine_set_value_clip_opt;
  _dart_engine_set_src? engine_set_height_source_opt;
  _dart_engine_set_src? engine_set_color_source_opt;
  _dart_engine_set_f? engine_set_height_exaggeration_opt;
  _dart_engine_set_bg? engine_set_background_opt;
  _dart_engine_export? engine_export_opt;
  _dart_engine_ptr_ret? engine_reset_camera_opt;
  _dart_engine_camera_input? engine_camera_input_opt;
  _dart_engine_set_camera_look_at? engine_set_camera_look_at_opt;
  _dart_engine_render_rgba? engine_render_rgba_opt;
  _dart_engine_cstr? engine_get_snapshot_json_opt;
  _dart_engine_cstr? engine_get_diagnostics_json_opt;
  _dart_engine_cstr? engine_last_error_opt;
  _dart_engine_point_count? engine_point_count_opt;
  _dart_engine_pick_point? engine_pick_point_opt;
  _dart_engine_set_int? engine_gizmo_axis_opt;
  _dart_engine_ptr_ret? engine_clear_tile_cache_opt;
  _dart_engine_read_frame_png? engine_read_frame_png_opt;

  GeoScatter3dBindings(this.dylib) {
    init = dylib.lookupFunction<_c_init, _dart_init>('gs3d_ffi_init');
    shutdown = dylib.lookupFunction<_c_shutdown, _dart_shutdown>(
      'gs3d_ffi_shutdown',
    );
    get_version = dylib.lookupFunction<_c_get_version, _dart_get_version>(
      'gs3d_ffi_get_version',
    );
    load_dataset = dylib.lookupFunction<_c_load_dataset, _dart_load_dataset>(
      'gs3d_ffi_load_dataset',
    );
    is_dataset_loaded = dylib
        .lookupFunction<_c_is_dataset_loaded, _dart_is_dataset_loaded>(
          'gs3d_ffi_is_dataset_loaded',
        );
    get_dataset_name = dylib
        .lookupFunction<_c_get_dataset_name, _dart_get_dataset_name>(
          'gs3d_ffi_get_dataset_name',
        );
    get_point_count = dylib
        .lookupFunction<_c_get_point_count, _dart_get_point_count>(
          'gs3d_ffi_get_point_count',
        );
    get_file_size_string = dylib
        .lookupFunction<_c_get_file_size_string, _dart_get_file_size_string>(
          'gs3d_ffi_get_file_size_string',
        );
    get_bounding_box = dylib
        .lookupFunction<_c_get_bounding_box, _dart_get_bounding_box>(
          'gs3d_ffi_get_bounding_box',
        );
    get_value_range = dylib
        .lookupFunction<_c_get_value_range, _dart_get_value_range>(
          'gs3d_ffi_get_value_range',
        );
    get_lod_enabled = dylib
        .lookupFunction<_c_get_lod_enabled, _dart_get_lod_enabled>(
          'gs3d_ffi_get_lod_enabled',
        );
    get_lod_level_count = dylib
        .lookupFunction<_c_get_lod_level_count, _dart_get_lod_level_count>(
          'gs3d_ffi_get_lod_level_count',
        );
    get_lod_level_detail = dylib
        .lookupFunction<_c_get_lod_level_detail, _dart_get_lod_level_detail>(
          'gs3d_ffi_get_lod_level_detail',
        );
    get_attribute_count = dylib
        .lookupFunction<_c_get_attribute_count, _dart_get_attribute_count>(
          'gs3d_ffi_get_attribute_count',
        );
    get_attribute_name = dylib
        .lookupFunction<_c_get_attribute_name, _dart_get_attribute_name>(
          'gs3d_ffi_get_attribute_name',
        );
    execute_command = dylib
        .lookupFunction<_c_execute_command, _dart_execute_command>(
          'gs3d_ffi_execute_command',
        );
    free_string = dylib.lookupFunction<_c_free_string, _dart_free_string>(
      'gs3d_ffi_free_string',
    );

    get_recent_project_count = dylib
        .lookupFunction<
          _c_get_recent_project_count,
          _dart_get_recent_project_count
        >('gs3d_ffi_get_recent_project_count');
    get_recent_project_path = dylib
        .lookupFunction<
          _c_get_recent_project_path,
          _dart_get_recent_project_path
        >('gs3d_ffi_get_recent_project_path');
    get_recent_project_timestamp = dylib
        .lookupFunction<
          _c_get_recent_project_timestamp,
          _dart_get_recent_project_timestamp
        >('gs3d_ffi_get_recent_project_timestamp');
    remember_recent_project = dylib
        .lookupFunction<
          _c_remember_recent_project,
          _dart_remember_recent_project
        >('gs3d_ffi_remember_recent_project');
    remove_recent_project = dylib
        .lookupFunction<_c_remove_recent_project, _dart_remove_recent_project>(
          'gs3d_ffi_remove_recent_project',
        );
    clear_recent_projects = dylib
        .lookupFunction<_c_clear_recent_projects, _dart_clear_recent_projects>(
          'gs3d_ffi_clear_recent_projects',
        );
    init_drag_drop = dylib
        .lookupFunction<_c_init_drag_drop, _dart_init_drag_drop>(
          'gs3d_ffi_init_drag_drop',
        );
    poll_dropped_file = dylib
        .lookupFunction<_c_poll_dropped_file, _dart_poll_dropped_file>(
          'gs3d_ffi_poll_dropped_file',
        );
    set_dropped_file = dylib
        .lookupFunction<_c_set_dropped_file, _dart_set_dropped_file>(
          'gs3d_ffi_set_dropped_file',
        );

    get_gpu_count = dylib.lookupFunction<_c_get_gpu_count, _dart_get_gpu_count>(
      'gs3d_ffi_get_gpu_count',
    );
    get_gpu_name = dylib.lookupFunction<_c_get_gpu_name, _dart_get_gpu_name>(
      'gs3d_ffi_get_gpu_name',
    );
    get_gpu_type = dylib.lookupFunction<_c_get_gpu_type, _dart_get_gpu_type>(
      'gs3d_ffi_get_gpu_type',
    );
    get_active_gpu_index = dylib
        .lookupFunction<_c_get_active_gpu_index, _dart_get_active_gpu_index>(
          'gs3d_ffi_get_active_gpu_index',
        );
    set_preferred_gpu = dylib
        .lookupFunction<_c_set_preferred_gpu, _dart_set_preferred_gpu>(
          'gs3d_ffi_set_preferred_gpu',
        );

    get_points = dylib.lookupFunction<_c_get_points, _dart_get_points>(
      'gs3d_ffi_get_points',
    );
    set_point_size = dylib
        .lookupFunction<_c_set_point_size, _dart_set_point_size>(
          'gs3d_ffi_set_point_size',
        );
    get_point_size = dylib
        .lookupFunction<_c_get_point_size, _dart_get_point_size>(
          'gs3d_ffi_get_point_size',
        );
    set_colormap = dylib.lookupFunction<_c_set_colormap, _dart_set_colormap>(
      'gs3d_ffi_set_colormap',
    );
    get_colormap = dylib.lookupFunction<_c_get_colormap, _dart_get_colormap>(
      'gs3d_ffi_get_colormap',
    );
    set_scalar_range = dylib
        .lookupFunction<_c_set_scalar_range, _dart_set_scalar_range>(
          'gs3d_ffi_set_scalar_range',
        );
    get_scalar_range = dylib
        .lookupFunction<_c_get_scalar_range, _dart_get_scalar_range>(
          'gs3d_ffi_get_scalar_range',
        );
    pick_file = dylib.lookupFunction<_c_pick_file, _dart_pick_file>(
      'gs3d_ffi_pick_file',
    );
    pick_folder = dylib.lookupFunction<_c_pick_folder, _dart_pick_folder>(
      'gs3d_ffi_pick_folder',
    );

    get_performance_metrics = dylib
        .lookupFunction<
          _c_get_performance_metrics,
          _dart_get_performance_metrics
        >('gs3d_ffi_get_performance_metrics');
    get_camera_coords_string = dylib
        .lookupFunction<
          _c_get_camera_coords_string,
          _dart_get_camera_coords_string
        >('gs3d_ffi_get_camera_coords_string');
    clear_cache = dylib.lookupFunction<_c_clear_cache, _dart_clear_cache>(
      'gs3d_ffi_clear_cache',
    );
    pick_point = dylib.lookupFunction<_c_pick_point, _dart_pick_point>(
      'gs3d_ffi_pick_point',
    );
    calculate_region_stats = dylib
        .lookupFunction<
          _c_calculate_region_stats,
          _dart_calculate_region_stats
        >('gs3d_ffi_calculate_region_stats');
    get_nav_map_thumbnail = dylib
        .lookupFunction<_c_get_nav_map_thumbnail, _dart_get_nav_map_thumbnail>(
          'gs3d_ffi_get_nav_map_thumbnail',
        );
    export_dataset = dylib
        .lookupFunction<_c_export_dataset, _dart_export_dataset>(
          'gs3d_ffi_export_dataset',
        );
    _preprocessCsv = dylib
        .lookupFunction<_c_preprocess_csv, _dart_preprocess_csv>(
          'gs3d_ffi_preprocess_csv',
        );
    _getLastPreprocessError = dylib
        .lookupFunction<_c_get_last_error, _dart_get_last_error>(
          'gs3d_ffi_get_last_error',
        );

    T? optLookup<T extends Function>(
      String name,
      T Function(ffi.DynamicLibrary lib, String symbol) resolve,
    ) {
      try {
        return resolve(dylib, name);
      } catch (_) {
        return null;
      }
    }

    // 单进程渲染内核（可选；旧 DLL 缺失时 hasEngineApi=false）
    engine_last_global_error_opt = optLookup(
      'gs3d_engine_last_global_error',
      (lib, n) => lib.lookupFunction<_c_engine_cstr_ret, _dart_engine_cstr_ret>(n),
    );
    engine_create_simple_opt = optLookup(
      'gs3d_engine_create_simple',
      (lib, n) => lib
          .lookupFunction<_c_engine_create_simple, _dart_engine_create_simple>(
        n,
      ),
    );
    engine_create_opt = optLookup(
      'gs3d_engine_create',
      (lib, n) =>
          lib.lookupFunction<_c_engine_create, _dart_engine_create>(n),
    );
    engine_unload_dataset_opt = optLookup(
      'gs3d_engine_unload_dataset',
      (lib, n) => lib.lookupFunction<_c_engine_unload, _dart_engine_unload>(n),
    );
    engine_destroy_opt = optLookup(
      'gs3d_engine_destroy',
      (lib, n) => lib.lookupFunction<_c_engine_ptr, _dart_engine_ptr>(n),
    );
    engine_load_dataset_opt = optLookup(
      'gs3d_engine_load_dataset',
      (lib, n) => lib
          .lookupFunction<_c_engine_load, _dart_engine_load>(n),
    );
    engine_resize_opt = optLookup(
      'gs3d_engine_resize',
      (lib, n) => lib.lookupFunction<_c_engine_resize, _dart_engine_resize>(n),
    );
    engine_set_point_size_opt = optLookup(
      'gs3d_engine_set_point_size',
      (lib, n) => lib
          .lookupFunction<_c_engine_set_point_size, _dart_engine_set_point_size>(
        n,
      ),
    );
    engine_set_colormap_index_opt = optLookup(
      'gs3d_engine_set_colormap_index',
      (lib, n) => lib.lookupFunction<_c_engine_set_int, _dart_engine_set_int>(n),
    );
    engine_set_point_shape_opt = optLookup(
      'gs3d_engine_set_point_shape',
      (lib, n) => lib.lookupFunction<_c_engine_set_int, _dart_engine_set_int>(n),
    );
    engine_set_value_clip_opt = optLookup(
      'gs3d_engine_set_value_clip',
      (lib, n) =>
          lib.lookupFunction<_c_engine_set_clip, _dart_engine_set_clip>(n),
    );
    engine_set_height_source_opt = optLookup(
      'gs3d_engine_set_height_source',
      (lib, n) =>
          lib.lookupFunction<_c_engine_set_src, _dart_engine_set_src>(n),
    );
    engine_set_color_source_opt = optLookup(
      'gs3d_engine_set_color_source',
      (lib, n) =>
          lib.lookupFunction<_c_engine_set_src, _dart_engine_set_src>(n),
    );
    engine_set_height_exaggeration_opt = optLookup(
      'gs3d_engine_set_height_exaggeration',
      (lib, n) => lib.lookupFunction<_c_engine_set_f, _dart_engine_set_f>(n),
    );
    engine_set_background_opt = optLookup(
      'gs3d_engine_set_background',
      (lib, n) => lib.lookupFunction<_c_engine_set_bg, _dart_engine_set_bg>(n),
    );
    engine_export_opt = optLookup(
      'gs3d_engine_export',
      (lib, n) => lib.lookupFunction<_c_engine_export, _dart_engine_export>(n),
    );
    engine_reset_camera_opt = optLookup(
      'gs3d_engine_reset_camera',
      (lib, n) => lib.lookupFunction<_c_engine_ptr_ret, _dart_engine_ptr_ret>(n),
    );
    engine_camera_input_opt = optLookup(
      'gs3d_engine_camera_input',
      (lib, n) =>
          lib.lookupFunction<_c_engine_camera_input, _dart_engine_camera_input>(
            n,
          ),
    );
    engine_set_camera_look_at_opt = optLookup(
      'gs3d_engine_set_camera_look_at',
      (lib, n) => lib.lookupFunction<
        _c_engine_set_camera_look_at,
        _dart_engine_set_camera_look_at
      >(n),
    );
    engine_render_rgba_opt = optLookup(
      'gs3d_engine_render_rgba',
      (lib, n) =>
          lib.lookupFunction<_c_engine_render_rgba, _dart_engine_render_rgba>(n),
    );
    engine_get_snapshot_json_opt = optLookup(
      'gs3d_engine_get_snapshot_json',
      (lib, n) =>
          lib.lookupFunction<_c_engine_cstr, _dart_engine_cstr>(n),
    );
    engine_get_diagnostics_json_opt = optLookup(
      'gs3d_engine_get_diagnostics_json',
      (lib, n) =>
          lib.lookupFunction<_c_engine_cstr, _dart_engine_cstr>(n),
    );
    engine_last_error_opt = optLookup(
      'gs3d_engine_last_error',
      (lib, n) => lib.lookupFunction<_c_engine_cstr, _dart_engine_cstr>(n),
    );
    engine_point_count_opt = optLookup(
      'gs3d_engine_point_count',
      (lib, n) =>
          lib.lookupFunction<_c_engine_point_count, _dart_engine_point_count>(n),
    );
    engine_pick_point_opt = optLookup(
      'gs3d_engine_pick_point',
      (lib, n) =>
          lib.lookupFunction<_c_engine_pick_point, _dart_engine_pick_point>(n),
    );
    engine_gizmo_axis_opt = optLookup(
      'gs3d_engine_gizmo_axis',
      (lib, n) => lib.lookupFunction<_c_engine_set_int, _dart_engine_set_int>(n),
    );
    engine_clear_tile_cache_opt = optLookup(
      'gs3d_engine_clear_tile_cache',
      (lib, n) =>
          lib.lookupFunction<_c_engine_ptr_ret, _dart_engine_ptr_ret>(n),
    );
    engine_read_frame_png_opt = optLookup(
      'gs3d_engine_read_frame_png',
      (lib, n) => lib.lookupFunction<
        _c_engine_read_frame_png,
        _dart_engine_read_frame_png
      >(n),
    );
  }

  /// Windows：LoadLibrary 默认不搜索 DLL 同目录依赖（glfw3 等）。
  /// 打开前调用 SetDllDirectoryA，使依赖与 gs3d_ffi.dll 同目录可被找到。
  ///
  /// 注意：`SetDllDirectoryA` 会把**相对路径**的解析基准重定向到该目录，
  /// 因此必须先把待加载路径规范化为绝对路径再打开，否则含 `..` 的相对路径
  /// 会解析失败（Windows error 126）。
  static ffi.DynamicLibrary _openWithLocalDeps(String path) {
    final absolutePath = File(path).absolute.path;
    if (Platform.isWindows) {
      final kernel32 = ffi.DynamicLibrary.open('kernel32.dll');
      final setDllDirA = kernel32.lookupFunction<
        ffi.Int32 Function(ffi.Pointer<Utf8>),
        int Function(ffi.Pointer<Utf8>)
      >('SetDllDirectoryA');
      final dir = File(absolutePath).parent.path.toNativeUtf8();
      try {
        setDllDirA(dir);
      } finally {
        calloc.free(dir);
      }
    }
    return ffi.DynamicLibrary.open(absolutePath);
  }

  /// Android / Linux：`DynamicLibrary.open('libgs3d_ffi.so')` 失败（典型为
  /// 引擎未做安卓移植、找不到 .so）时包装为带原因的 [Gs3dEngineLoadException]，
  /// 供 service 捕获归类为显式「引擎不可用」状态。**不改加载路径本身**。
  static ffi.DynamicLibrary _openPosixSo() {
    try {
      return ffi.DynamicLibrary.open('libgs3d_ffi.so');
    } catch (error) {
      throw Gs3dEngineLoadException(
        '无法加载 libgs3d_ffi.so（渲染内核未随平台部署）: $error',
      );
    }
  }

  /// 自动探测并加载动态库
  static GeoScatter3dBindings openLibrary([String? customPath]) {
    if (customPath != null && File(customPath).existsSync()) {
      return GeoScatter3dBindings(_openWithLocalDeps(customPath));
    }

    if (Platform.isWindows) {
      final exeDir = File(Platform.resolvedExecutable).parent.path;
      final candidates = [
        if (Platform.environment['GS3D_RUNTIME_DIR'] != null)
          '${Platform.environment['GS3D_RUNTIME_DIR']}/gs3d_ffi.dll',
        // 优先使用仓库内最新构建产物，避免误载旧部署副本。
        '${Directory.current.path}/../tmp/build-win/src/ffi/Release/gs3d_ffi.dll',
        '${Directory.current.path}/../tmp/build-win/Release/gs3d_ffi.dll',
        '${Directory.current.path}/gs3d_ffi.dll',
        '$exeDir/gs3d_ffi.dll',
        'gs3d_ffi.dll',
        '../tmp/build-win/src/ffi/Release/gs3d_ffi.dll',
        '../tmp/build-win/Release/gs3d_ffi.dll',
        'tmp/build-win/src/ffi/Release/gs3d_ffi.dll',
        'tmp/build-win/Release/gs3d_ffi.dll',
      ];
      for (final p in candidates) {
        if (File(p).existsSync()) {
          return GeoScatter3dBindings(_openWithLocalDeps(p));
        }
      }
      return GeoScatter3dBindings(_openWithLocalDeps('gs3d_ffi.dll'));
    } else if (Platform.isAndroid || Platform.isLinux) {
      return GeoScatter3dBindings(_openPosixSo());
    } else if (Platform.isMacOS || Platform.isIOS) {
      return GeoScatter3dBindings(ffi.DynamicLibrary.process());
    }
    throw UnsupportedError('Unsupported platform: ${Platform.operatingSystem}');
  }

  /// CSV 预处理（编译入 .so，供移动端免外部可执行文件）。
  /// 返回 0 成功；错误经 [lastPreprocessError] 获取。
  int preprocessCsv(String csvPath, String outputPath,
      {int threads = 2, int finestTargetPoints = 50000}) {
    final csvPtr = csvPath.toNativeUtf8();
    final outPtr = outputPath.toNativeUtf8();
    try {
      return _preprocessCsv(csvPtr, outPtr, threads, finestTargetPoints);
    } finally {
      calloc.free(csvPtr);
      calloc.free(outPtr);
    }
  }

  String get lastPreprocessError {
    final ptr = _getLastPreprocessError();
    return ptr == ffi.nullptr ? '' : ptr.toDartString();
  }
}
