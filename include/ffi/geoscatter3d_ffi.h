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
 * A 组（本地引擎，DLL 内状态）：§1–§17
 * §18–§19 双进程 Viewer 桥已删除；多端仅使用 A 组 + §20 单进程 engine。
 *
 * 返回码（int32_t）：
 *   0 成功
 *  -1 空指针/非法参数
 *  -2 目标不存在
 *  -3 内部失败
 *  -4 状态冲突
 *  -5 超时
 *  -8 句柄无效
 *
 * 线程：A 组内部加锁。
 * 生命周期：init → [engine_* …] → shutdown。
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
// 18-19. （已删除）双进程 Viewer 桥 — 请改用 §20 gs3d_engine_*
// ============================================================================

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
GS3D_FFI_API int32_t gs3d_engine_load_sample(gs3d_engine* e);

/// 生成内置合成地形（基准/压力测试）：point_count 取接近的完全平方数。
/// 用于在真机/桌面上验证大规模数据的 LOD 与帧预算，不参与生产数据流。
GS3D_FFI_API int32_t gs3d_engine_load_synthetic(
    gs3d_engine* e, uint64_t point_count);

/// 每帧绘制点预算（0 = 按视口自动）；决定 LOD 选层。
GS3D_FFI_API int32_t gs3d_engine_set_point_budget(
    gs3d_engine* e, uint64_t max_points);

/* ── 平台 surface（窗口呈现）：全部为 C ABI，不出现 Vulkan 类型 ──────────
 *
 * 平台适配层负责把 HWND / ANativeWindow 变成原生 surface 句柄：
 *   - instance_extensions 为创建 surface 所需的 instance 扩展名
 *     （如 "VK_KHR_win32_surface" / "VK_KHR_android_surface"）；
 *   - create_surface(instance, user_data, out_surface_handle) 返回 0 表示成功，
 *     out_surface_handle 填平台创建的 surface 句柄（内部会转成 VkSurfaceKHR）；
 *   - user_data 所有权属调用方，至少需活到回调返回。
 */
typedef struct gs3d_surface_provider {
    const char* const* instance_extensions;
    uint32_t instance_extension_count;
    int32_t (*create_surface)(
        void* instance, void* user_data, uint64_t* out_surface_handle);
    void* user_data;
} gs3d_surface_provider;

/// 附加平台 surface 并切到窗口呈现（内部重建上下文/交换链/present 管线）。
GS3D_FFI_API int32_t gs3d_engine_set_surface(
    gs3d_engine* e, const gs3d_surface_provider* provider,
    uint32_t width, uint32_t height);

/// 分离 surface，回到纯离屏回读路径。
GS3D_FFI_API int32_t gs3d_engine_clear_surface(gs3d_engine* e);

/// 渲染并呈递一帧到交换链（仅在附加 surface 后可用）。0 成功，-2 本帧跳过。
GS3D_FFI_API int32_t gs3d_engine_present_frame(gs3d_engine* e);

/// 当前是否处于窗口呈现模式。
GS3D_FFI_API int32_t gs3d_engine_has_surface(gs3d_engine* e);

/// 数据集总点数（源）与最近一帧实际绘制点数（LOD 后）。
GS3D_FFI_API uint64_t gs3d_engine_source_point_count(gs3d_engine* e);
GS3D_FFI_API uint64_t gs3d_engine_drawn_point_count(gs3d_engine* e);
GS3D_FFI_API int32_t gs3d_engine_unload_dataset(gs3d_engine* e);
GS3D_FFI_API int32_t gs3d_engine_resize(gs3d_engine* e, uint32_t w, uint32_t h);

GS3D_FFI_API int32_t gs3d_engine_set_point_size(gs3d_engine* e, float size);
/*
 * 渲染像素 / 逻辑像素之比（= 设备 dpr）。
 *
 * 零拷贝纹理通路按物理像素建交换链（点阵才能与屏幕 1:1、不吃合成器放大），
 * 但内核里所有「像素门槛」（瓦片最小投影尺寸、底图目标点距、自动点预算）都是
 * 按逻辑像素调优的。平台层拿到 dpr 后必须告知引擎，否则物理像素会让这些门槛
 * 整体偏大，选取的瓦片/层级越级（真机 10M fov=1.4° 实测瓦片层消失、整屏空）。
 * 回读路径（逻辑像素渲染）传 1.0。scale 非有限值或 <= 0 时返回非 0。
 */
GS3D_FFI_API int32_t gs3d_engine_set_view_scale(gs3d_engine* e, float scale);
GS3D_FFI_API int32_t gs3d_engine_set_colormap_index(gs3d_engine* e, int32_t index);
GS3D_FFI_API int32_t gs3d_engine_set_point_shape(gs3d_engine* e, int32_t shape);
GS3D_FFI_API int32_t gs3d_engine_set_value_clip(
    gs3d_engine* e, int32_t enabled, float min_v, float max_v);
/// source: 0=Z, 1=Value
GS3D_FFI_API int32_t gs3d_engine_set_height_source(gs3d_engine* e, int32_t source);
GS3D_FFI_API int32_t gs3d_engine_set_color_source(gs3d_engine* e, int32_t source);
GS3D_FFI_API int32_t gs3d_engine_set_height_exaggeration(gs3d_engine* e, float mult);
GS3D_FFI_API int32_t gs3d_engine_set_background(
    gs3d_engine* e, float r, float g, float b, float a);
/// format: "ply" | "csv"
GS3D_FFI_API int32_t gs3d_engine_export(
    gs3d_engine* e, const char* path, const char* format);

/// 屏幕坐标拾取：使用 engine 当前相机 VP。命中填 [x,y,z,value] 返回 1，未命中 0，错误负。
GS3D_FFI_API int32_t gs3d_engine_pick_point(
    gs3d_engine* e, float screen_x, float screen_y,
    float tolerance_px, float* out_point_xyzv);

/// Gizmo 预设轴 0..5（-X/+X/-Y/+Y/+Z/-Z）。
GS3D_FFI_API int32_t gs3d_engine_gizmo_axis(gs3d_engine* e, int32_t axis);

/// 清空帧缓存并重建点云上传（单进程软复位）。
GS3D_FFI_API int32_t gs3d_engine_clear_tile_cache(gs3d_engine* e);

/// 当前帧编码 PNG；未渲染过返回 -2。
GS3D_FFI_API int32_t gs3d_engine_read_frame_png(
    gs3d_engine* e, uint8_t* out, uint32_t max_bytes, uint32_t* out_len);

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

/// 渲染一帧离屏 RGBA8（同步兼容路径，调用线程阻塞在渲染线程上）。
/// 成功后 out_w/out_h 有效。新前端应改用 request_frame + read_frame_rgba。
GS3D_FFI_API int32_t gs3d_engine_render_rgba(
    gs3d_engine* e, uint8_t* out_rgba, uint32_t max_bytes,
    uint32_t* out_w, uint32_t* out_h);

/// 请求渲染线程异步绘制一帧；已有帧在渲染中时合并请求。
/// 返回 0 表示请求已受理或已被合并，负值为失败。
GS3D_FFI_API int32_t gs3d_engine_request_frame(gs3d_engine* e);

/// 拷贝最近一次完成的帧（不触发 Vulkan、不等待渲染）。
/// 0 成功；-1 容量不足（仍回填实际宽高，调用方扩容重试）；-2 尚无帧。
/// out_stride/out_sequence 可为 NULL。
GS3D_FFI_API int32_t gs3d_engine_read_frame_rgba(
    gs3d_engine* e, uint8_t* out_rgba, uint32_t max_bytes,
    uint32_t* out_w, uint32_t* out_h,
    uint32_t* out_stride, uint64_t* out_sequence);

/// 已发布帧序号（每次成功渲染递增）；无帧时为 0。
GS3D_FFI_API uint64_t gs3d_engine_frame_sequence(gs3d_engine* e);

/// 最近一帧在渲染线程上的耗时（毫秒，含 GPU 提交与回读）。
GS3D_FFI_API double gs3d_engine_last_render_ms(gs3d_engine* e);
GS3D_FFI_API const char* gs3d_engine_get_snapshot_json(gs3d_engine* e);
GS3D_FFI_API const char* gs3d_engine_get_diagnostics_json(gs3d_engine* e);
GS3D_FFI_API const char* gs3d_engine_last_error(gs3d_engine* e);
GS3D_FFI_API uint64_t gs3d_engine_point_count(gs3d_engine* e);

// ============================================================================
// 21. 异步任务与状态快照 (docs/architecture.md「状态快照」「命令」)
// ============================================================================

#define GS3D_ENGINE_ABI_VERSION 1u

#define GS3D_MAX_TASKS 4u
#define GS3D_TASK_STAGE_MAX 32u

enum {
    GS3D_TASK_FREE = 0,
    GS3D_TASK_QUEUED = 1,
    GS3D_TASK_RUNNING = 2,
    GS3D_TASK_DONE = 3,
    GS3D_TASK_FAILED = 4,
    GS3D_TASK_CANCELLED = 5
};

typedef struct gs3d_task_state {
    uint64_t id;                  /* 0 = 空槽 */
    uint32_t state;               /* GS3D_TASK_* */
    uint32_t progress;            /* 0..1000 千分比 */
    char stage[GS3D_TASK_STAGE_MAX]; /* NUL 结尾，UTF-8 */
} gs3d_task_state;

/* 主线程读取的完整一代快照：只由引擎写入，调用方不得回写。 */
typedef struct gs3d_state {
    uint32_t struct_size;         /* sizeof(gs3d_state)，用于版本协商 */
    uint64_t generation;          /* 每次发布 +1 */
    uint64_t point_count;         /* 已载入点数，未载入为 0 */
    uint32_t dataset_loaded;
    uint32_t task_count;          /* tasks 中非空槽位数 */
    char error[256];              /* 最近一次任务失败文本，UTF-8 */
    gs3d_task_state tasks[GS3D_MAX_TASKS];
} gs3d_state;

/* ABI 版本：与结构体布局绑定，不随构建变化。 */
GS3D_FFI_API uint32_t gs3d_engine_abi_version(void);

/* 拷贝最近发布的一代快照。out->struct_size 必须是 sizeof(gs3d_state)。
 * 终态任务在本次读取后释放；返回 0 成功、-1 参数错误、-2 尚未发布。 */
GS3D_FFI_API int32_t gs3d_engine_poll_state(gs3d_engine* e, gs3d_state* out);

/* 在工作线程读取工程包，完成后由渲染线程装载。立即返回任务号；
 * 路径按 UTF-8 深拷贝，不保留调用方指针。任务表满返回 -4。 */
GS3D_FFI_API int32_t gs3d_engine_open_dataset(
    gs3d_engine* e, const char* path_utf8, uint64_t* out_task);

/* 取消排队或运行中的任务：渲染线程在装载前观察到取消即丢弃结果。
 * 文件读取本身不可中断。0 成功、-1 未知或已结束的任务。 */
GS3D_FFI_API int32_t gs3d_engine_cancel(gs3d_engine* e, uint64_t task);

GS3D_FFI_API const char* gs3d_engine_version(void);
GS3D_FFI_API int32_t gs3d_engine_preprocess_csv(
    const char* csv_path,
    const char* output_gs3d_path,
    uint32_t threads,
    uint32_t finest_target_points);

/// CSV → .gs3d 单文件转换（单趟，含质心原点变换）。
/// 成功返回 0，失败返回负值；错误详情经 gs3d_ffi_get_last_error() 获取。
GS3D_FFI_API int32_t gs3d_ffi_preprocess_csv(
    const char* csv_path,
    const char* output_gs3d_path,
    uint32_t threads,
    uint32_t finest_target_points);

/// 获取最近一次预处理的错误信息。
GS3D_FFI_API const char* gs3d_ffi_get_last_error(void);

#ifdef __cplusplus
}
#endif
