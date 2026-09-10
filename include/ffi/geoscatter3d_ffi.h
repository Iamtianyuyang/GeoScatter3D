#pragma once

#include <stdint.h>
#include <stdbool.h>

#if defined(_WIN32) || defined(__CYGWIN__)
    #ifdef GS3D_FFI_EXPORTS
        #define GS3D_FFI_API __declspec(dllexport)
    #else
        #define GS3D_FFI_API __declspec(dllimport)
    #endif
#else
    #if __GNUC__ >= 4
        #define GS3D_FFI_API __attribute__((visibility("default")))
    #else
        #define GS3D_FFI_API
    #endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * GeoScatter3D C-ABI 契约（Flutter / 外部绑定唯一入口）
 *
 * A 组（本地引擎，DLL 内状态，无 Viewer 也可用）：§1–§17
 * B 组（Viewer 桥与运行时控制，需 viewer_start 成功）：§18–§19
 *   旧 DLL 可能缺失 B 组符号；Dart 侧必须 optLookup。
 *
 * 返回码（int32_t）：
 *   0 成功
 *  -1 空指针/非法参数
 *  -2 目标不存在 / Viewer 未连接
 *  -3 内部失败或 JSON-RPC error
 *  -4 状态冲突（如同时 rotate+pan、导出占用）
 *  -5 超时 / CreateProcess 失败
 *  -6 子进程提前退出
 *  -7 控制面连接超时
 *  -8 句柄无效
 *
 * 字符串：
 *   - 入参 UTF-8，仅调用期间有效，实现必须拷贝
 *   - 出参 const char*：调用方读取后立即拷贝；若实现返回 malloc 块，
 *     必须用 gs3d_ffi_free_string 释放。禁止缓存裸指针。
 *
 * 线程：A 组内部加锁；B 组 RPC 同步阻塞（约 5s 超时）。
 * 生命周期：init → [viewer_start … viewer_stop/quit_viewer] → shutdown。
 */

// ============================================================================
// 1. 引擎生命周期与版本
// ============================================================================

GS3D_FFI_API int32_t gs3d_ffi_init(void);
GS3D_FFI_API void gs3d_ffi_shutdown(void);
GS3D_FFI_API const char* gs3d_ffi_get_version(void);

// ============================================================================
// 2. 数据集加载与状态查询
// ============================================================================

GS3D_FFI_API int32_t gs3d_ffi_load_dataset(const char* path);
GS3D_FFI_API int32_t gs3d_ffi_is_dataset_loaded(void);
GS3D_FFI_API const char* gs3d_ffi_get_dataset_name(void);
GS3D_FFI_API uint64_t gs3d_ffi_get_point_count(void);
GS3D_FFI_API const char* gs3d_ffi_get_file_size_string(void);
GS3D_FFI_API void gs3d_ffi_get_bounding_box(float* out_min_xyz, float* out_max_xyz);
GS3D_FFI_API void gs3d_ffi_get_value_range(float* out_min_val, float* out_max_val);

// ============================================================================
// 3. LOD 细节层级查询
// ============================================================================

GS3D_FFI_API int32_t gs3d_ffi_get_lod_enabled(void);
GS3D_FFI_API int32_t gs3d_ffi_get_lod_level_count(void);
GS3D_FFI_API const char* gs3d_ffi_get_lod_level_detail(int32_t level_index);

// ============================================================================
// 4. 属性字段查询
// ============================================================================

GS3D_FFI_API int32_t gs3d_ffi_get_attribute_count(void);
GS3D_FFI_API const char* gs3d_ffi_get_attribute_name(int32_t index);

// ============================================================================
// 5. 控制面与万能 JSON-RPC 命令通道 (支持扩展任何新特性，返回 JSON 字符串)
// ============================================================================

GS3D_FFI_API const char* gs3d_ffi_execute_command(const char* json_request);

// ============================================================================
// 6. 内存管理辅助
// ============================================================================

GS3D_FFI_API void gs3d_ffi_free_string(char* ptr);

// ============================================================================
// 7. 最近项目记录 (Recent Projects)
// ============================================================================

GS3D_FFI_API int32_t gs3d_ffi_get_recent_project_count(void);
GS3D_FFI_API const char* gs3d_ffi_get_recent_project_path(int32_t index);
GS3D_FFI_API int64_t gs3d_ffi_get_recent_project_timestamp(int32_t index);
GS3D_FFI_API void gs3d_ffi_remember_recent_project(const char* path);
GS3D_FFI_API void gs3d_ffi_remove_recent_project(const char* path);
GS3D_FFI_API void gs3d_ffi_clear_recent_projects(void);

// ============================================================================
// 8. 图形硬件设备信息 (GPU Info)
// ============================================================================

GS3D_FFI_API int32_t gs3d_ffi_get_gpu_count(void);
GS3D_FFI_API const char* gs3d_ffi_get_gpu_name(int32_t index);
GS3D_FFI_API const char* gs3d_ffi_get_gpu_type(int32_t index);
GS3D_FFI_API int32_t gs3d_ffi_get_active_gpu_index(void);
GS3D_FFI_API void gs3d_ffi_set_preferred_gpu(int32_t index);

// ============================================================================
// 9. 测点数据与空间几何获取 (Point Cloud Stream)
// ============================================================================

/// 获取当前载入数据集的点数据（x, y, z, scalar），最多填充 max_points 个点
/// out_buffer 连续存放：[x0, y0, z0, val0, x1, y1, z1, val1, ...]
/// 返回实际填充的点数量
GS3D_FFI_API int32_t gs3d_ffi_get_points(float* out_buffer, int32_t max_points);

// ============================================================================
// 10. 视口与渲染交互控制 (Render & Viewport Settings)
// ============================================================================

GS3D_FFI_API void gs3d_ffi_set_point_size(float size);
GS3D_FFI_API float gs3d_ffi_get_point_size(void);

GS3D_FFI_API void gs3d_ffi_set_colormap(const char* colormap_name);
GS3D_FFI_API const char* gs3d_ffi_get_colormap(void);

GS3D_FFI_API void gs3d_ffi_set_scalar_range(float min_val, float max_val);
GS3D_FFI_API void gs3d_ffi_get_scalar_range(float* out_min, float* out_max);

// ============================================================================
// 11. 原生操作系统文件与目录选择对话框 (Native File & Folder Dialog)
// ============================================================================

GS3D_FFI_API const char* gs3d_ffi_pick_file(const char* filter_type);
GS3D_FFI_API const char* gs3d_ffi_pick_folder(void);

// ============================================================================
// 12. 桌面原生文件拖拽 (Desktop Drag & Drop)
// ============================================================================

GS3D_FFI_API void gs3d_ffi_init_drag_drop(void);
GS3D_FFI_API const char* gs3d_ffi_poll_dropped_file(void);
GS3D_FFI_API void gs3d_ffi_set_dropped_file(const char* path);

// ============================================================================
// 13. 性能诊断与流式瓦片指标 (Performance & Diagnostics)
// ============================================================================

GS3D_FFI_API void gs3d_ffi_get_performance_metrics(
    float* out_fps,
    float* out_frame_time_ms,
    uint64_t* out_visible_points,
    uint64_t* out_gpu_mem_bytes,
    uint32_t* out_loaded_tiles,
    uint32_t* out_pending_tiles,
    float* out_cache_hit_rate
);

GS3D_FFI_API const char* gs3d_ffi_get_camera_coords_string(void);
GS3D_FFI_API void gs3d_ffi_clear_cache(void);

// ============================================================================
// 14. 视口三维空间点拾取 (3D Point Picking & Raycasting)
// ============================================================================

/// 在当前视口投影几何下，通过屏幕坐标 (screen_x, screen_y) 寻找最近点
/// out_point_xyzv 连续存放 [x, y, z, val]
/// 若拾取成功返回 1，未命中返回 0
GS3D_FFI_API int32_t gs3d_ffi_pick_point(
    float screen_x,
    float screen_y,
    float viewport_width,
    float viewport_height,
    float azimuth_deg,
    float elevation_deg,
    float zoom,
    float pan_x,
    float pan_y,
    float* out_point_xyzv
);

// ============================================================================
// 15. 局部选区统计分析 (Region Stats Computation)
// ============================================================================

/// 针对指定的世界坐标矩形选区计算统计指标
GS3D_FFI_API int32_t gs3d_ffi_calculate_region_stats(
    float min_x,
    float max_x,
    float min_y,
    float max_y,
    uint64_t* out_count,
    float* out_mean,
    float* out_min,
    float* out_max,
    float* out_std_dev,
    int32_t* out_hist_5bins
);

// ============================================================================
// 16. 2D 鸟瞰导航图位图生成 (Navigation Map Thumbnail)
// ============================================================================

/// 生成点云在 XY 投影平面的 2D 密度/高程缩略图位图 (RGBA 格式，宽*高*4 字节)
/// 返回 0 表示成功，负数表示失败
GS3D_FFI_API int32_t gs3d_ffi_get_nav_map_thumbnail(
    uint8_t* out_rgba,
    int32_t width,
    int32_t height
);

// ============================================================================
// 17. 点云数据导出 (Point Cloud Export: PLY / CSV)
// ============================================================================

/// 导出当前点云或裁剪过滤后点云至目标文件 (格式: "ply" 或 "csv")
GS3D_FFI_API int32_t gs3d_ffi_export_dataset(
    const char* target_path,
    const char* format_type
);

// ============================================================================
// 18. Viewer 进程桥 (Full Control-Plane Bridge via FFI)
// ============================================================================

/// 启动 headless Viewer 并连接控制面。dataset 为 .gs3d 或 .gs3d.bundle。
/// 返回 0 成功；负数见错误。
GS3D_FFI_API int32_t gs3d_ffi_viewer_start(
    const char* executable_path,
    const char* dataset_path
);
GS3D_FFI_API void gs3d_ffi_viewer_stop(void);
GS3D_FFI_API int32_t gs3d_ffi_viewer_is_connected(void);
GS3D_FFI_API int32_t gs3d_ffi_viewer_last_capture_width(void);
GS3D_FFI_API int32_t gs3d_ffi_viewer_last_capture_height(void);
GS3D_FFI_API const char* gs3d_ffi_viewer_last_error(void);
GS3D_FFI_API const char* gs3d_ffi_viewer_startup_log(void);

/// 发送一行 JSON-RPC 到控制面，返回一行 JSON 响应。
GS3D_FFI_API const char* gs3d_ffi_viewer_rpc(const char* json_request_line);

/// 帧同步截图：返回 JSON {"data":"<base64 png>","width":w,"height":h}
GS3D_FFI_API const char* gs3d_ffi_viewer_screenshot_json(void);

/// execute(component, command, params_json) 便捷封装
GS3D_FFI_API const char* gs3d_ffi_viewer_execute(
    const char* component,
    const char* command,
    const char* params_json
);
/// get_state(component) 便捷封装
GS3D_FFI_API const char* gs3d_ffi_viewer_get_state(const char* component);

// ============================================================================
// 19. 主题 / 面板 / 相机联动 / 工作区 等完整运行时控制 (经 Viewer 桥)
// ============================================================================

GS3D_FFI_API int32_t gs3d_ffi_set_theme(int32_t theme_index);
GS3D_FFI_API int32_t gs3d_ffi_cycle_theme(void);
GS3D_FFI_API int32_t gs3d_ffi_set_camera_linked(int32_t linked);
GS3D_FFI_API int32_t gs3d_ffi_request_new_view(void);
GS3D_FFI_API int32_t gs3d_ffi_request_new_workspace(void);
GS3D_FFI_API int32_t gs3d_ffi_restore_workspace(void);
GS3D_FFI_API int32_t gs3d_ffi_set_panel_visible(const char* panel_id, int32_t visible);
GS3D_FFI_API int32_t gs3d_ffi_open_native_file_dialog(void);
GS3D_FFI_API int32_t gs3d_ffi_open_native_bundle_dialog(void);
GS3D_FFI_API int32_t gs3d_ffi_request_native_screenshot(void);
GS3D_FFI_API const char* gs3d_ffi_get_status_bar_json(void);
GS3D_FFI_API const char* gs3d_ffi_get_interaction_snapshot_json(void);
GS3D_FFI_API const char* gs3d_ffi_get_runtime_diagnostics_json(void);
GS3D_FFI_API const char* gs3d_ffi_get_render_settings_json(void);
GS3D_FFI_API int32_t gs3d_ffi_set_render_settings_json(const char* params_json);
GS3D_FFI_API int32_t gs3d_ffi_camera_input(
    float rotate_dx, float rotate_dy,
    float pan_dx, float pan_dy,
    float scroll_y
);
GS3D_FFI_API int32_t gs3d_ffi_reset_camera(void);
GS3D_FFI_API int32_t gs3d_ffi_gizmo_axis(int32_t axis);
GS3D_FFI_API int32_t gs3d_ffi_pick_input(const char* kind, const char* params_json);
GS3D_FFI_API int32_t gs3d_ffi_measurement_op(const char* op, const char* params_json);
GS3D_FFI_API int32_t gs3d_ffi_clear_tile_cache(void);
GS3D_FFI_API int32_t gs3d_ffi_export_via_viewer(
    const char* output_path,
    const char* format
);
GS3D_FFI_API const char* gs3d_ffi_list_components_json(void);
GS3D_FFI_API int32_t gs3d_ffi_quit_viewer(void);

// ============================================================================
// 20. 单进程渲染内核 (gs3d_engine*) — 多端可移植，无 spawn Viewer
// ============================================================================

typedef struct gs3d_engine gs3d_engine;

typedef struct gs3d_engine_config {
    const char* preferred_gpu_uuid; /* 可空 / "auto" */
    uint32_t viewport_width;
    uint32_t viewport_height;
    const char* shader_dir;         /* 可空：含 point.vert.spv / point.frag.spv */
    int32_t enable_validation;
} gs3d_engine_config;

/// 创建失败返回 NULL；错误见 gs3d_engine_last_global_error()。
GS3D_FFI_API const char* gs3d_engine_last_global_error(void);
GS3D_FFI_API gs3d_engine* gs3d_engine_create(const gs3d_engine_config* cfg);
/// 简化入口：默认 GPU / 默认 shader 相对路径 / 关闭 validation。
GS3D_FFI_API gs3d_engine* gs3d_engine_create_simple(uint32_t width, uint32_t height);
GS3D_FFI_API void gs3d_engine_destroy(gs3d_engine* e);

GS3D_FFI_API int32_t gs3d_engine_load_dataset(gs3d_engine* e, const char* path);
GS3D_FFI_API int32_t gs3d_engine_unload_dataset(gs3d_engine* e);
GS3D_FFI_API int32_t gs3d_engine_resize(gs3d_engine* e, uint32_t w, uint32_t h);

GS3D_FFI_API int32_t gs3d_engine_set_point_size(gs3d_engine* e, float size);
GS3D_FFI_API int32_t gs3d_engine_set_colormap_index(gs3d_engine* e, int32_t index);
GS3D_FFI_API int32_t gs3d_engine_set_point_shape(gs3d_engine* e, int32_t shape);
GS3D_FFI_API int32_t gs3d_engine_set_value_clip(
    gs3d_engine* e, int32_t enabled, float min_v, float max_v);

GS3D_FFI_API int32_t gs3d_engine_set_camera_look_at(
    gs3d_engine* e,
    float pos_x, float pos_y, float pos_z,
    float tgt_x, float tgt_y, float tgt_z,
    float up_x, float up_y, float up_z);
GS3D_FFI_API int32_t gs3d_engine_reset_camera(gs3d_engine* e);
/// 增量相机：rotate 与 pan 互斥；scroll_y 为滚轮刻度。
GS3D_FFI_API int32_t gs3d_engine_camera_input(
    gs3d_engine* e,
    float rotate_dx, float rotate_dy,
    float pan_dx, float pan_dy, float scroll_y);

/// 渲染一帧离屏 RGBA8。成功后 out_w/out_h 有效。
GS3D_FFI_API int32_t gs3d_engine_render_rgba(
    gs3d_engine* e, uint8_t* out_rgba, uint32_t max_bytes,
    uint32_t* out_w, uint32_t* out_h);

GS3D_FFI_API const char* gs3d_engine_get_snapshot_json(gs3d_engine* e);
GS3D_FFI_API const char* gs3d_engine_get_diagnostics_json(gs3d_engine* e);
GS3D_FFI_API const char* gs3d_engine_last_error(gs3d_engine* e);
GS3D_FFI_API uint64_t gs3d_engine_point_count(gs3d_engine* e);

#ifdef __cplusplus
}
#endif
