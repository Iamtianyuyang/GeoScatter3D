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

#ifdef __cplusplus
}
#endif

