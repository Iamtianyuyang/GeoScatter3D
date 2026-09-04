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

#ifdef __cplusplus
}
#endif
