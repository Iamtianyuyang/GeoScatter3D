#ifndef GS3D_PIPELINE_H
#define GS3D_PIPELINE_H

#include <stdint.h>

#ifdef _WIN32
#ifdef GS3D_PIPELINE_EXPORTS
#define GS3D_PIPELINE_API __declspec(dllexport)
#else
#define GS3D_PIPELINE_API __declspec(dllimport)
#endif
#else
#define GS3D_PIPELINE_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gs3d_pipeline gs3d_pipeline;

typedef struct gs3d_pipeline_config {
    uint32_t struct_size;
} gs3d_pipeline_config;

typedef struct gs3d_pipeline_job {
    uint32_t struct_size;
    const char* source_path;
    const char* bundle_path;
    uint32_t points_per_chunk;
    const char* value_field;
} gs3d_pipeline_job;

enum gs3d_pipeline_task_state {
    GS3D_PIPELINE_IDLE = 0,
    GS3D_PIPELINE_RUNNING = 1,
    GS3D_PIPELINE_DONE = 2,
    GS3D_PIPELINE_FAILED = 3,
    GS3D_PIPELINE_CANCELLED = 4
};

typedef struct gs3d_pipeline_status {
    uint32_t struct_size;
    uint64_t task_id;
    uint32_t state;
    char stage[32];
    char error[256];
    uint64_t points;
    uint64_t chunks;
} gs3d_pipeline_status;

/* All strings are UTF-8 and copied during the call. One job may run at a time.
 * Returns 0 on success, -1 for invalid arguments, -2 if the task is not active,
 * and -3 on failure (including a busy runner). Poll returns the last status.
 * Destroy requests cancellation and joins the worker, so it can block. */
GS3D_PIPELINE_API gs3d_pipeline* gs3d_pipeline_create(const gs3d_pipeline_config* config);
GS3D_PIPELINE_API int32_t gs3d_pipeline_start(gs3d_pipeline*, const gs3d_pipeline_job*, uint64_t* task);
GS3D_PIPELINE_API int32_t gs3d_pipeline_resume(gs3d_pipeline*, const char* bundle_dir, uint64_t* task);
GS3D_PIPELINE_API int32_t gs3d_pipeline_poll(gs3d_pipeline*, gs3d_pipeline_status*);
GS3D_PIPELINE_API int32_t gs3d_pipeline_cancel(gs3d_pipeline*, uint64_t task);
GS3D_PIPELINE_API void gs3d_pipeline_destroy(gs3d_pipeline*);

#ifdef __cplusplus
}
#endif
#endif
