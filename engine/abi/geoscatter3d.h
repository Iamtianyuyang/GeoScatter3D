#ifndef GEOSCATTER3D_ABI_H
#define GEOSCATTER3D_ABI_H

/*
 * geoscatter3d ABI v1 — the only seam between the Flutter UI and the C++ engine.
 *
 * Rules:
 *   - Structs start with gs3d_abi_header; a caller fills struct_size and abi_version.
 *     The engine rejects (GS3D_ERR_ABI) anything smaller or newer than it knows.
 *   - The engine never blocks the UI thread on the GPU: the UI polls gs3d_state and
 *     long work is submitted as a task that reports progress and can be cancelled.
 *   - Pointers passed in are borrowed for the call duration and copied by the engine.
 *   - Pointers returned (message) are owned by the engine and stay valid until the
 *     task leaves the registry; copy anything you keep.
 *   - Functions are not thread-safe against gs3d_engine_destroy; stop workers first.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GS3D_ABI_VERSION 1u

typedef struct gs3d_abi_header {
    uint32_t struct_size;
    uint32_t abi_version;
} gs3d_abi_header;

typedef enum gs3d_wake_bits {
    GS3D_WAKE_FRAME = 1u << 0,
    GS3D_WAKE_TASK = 1u << 1,
    GS3D_WAKE_PROGRESS = 1u << 2,
    GS3D_WAKE_ERROR = 1u << 3
} gs3d_wake_bits;

typedef enum gs3d_status {
    GS3D_OK = 0,
    GS3D_ERR_ARG = -1,
    GS3D_ERR_ABI = -2,
    GS3D_ERR_STATE = -3,
    GS3D_ERR_NOT_FOUND = -4,
    GS3D_ERR_IO = -5,
    GS3D_ERR_TASK = -6,
    GS3D_ERR_FULL = -7
} gs3d_status;

typedef enum gs3d_command_kind {
    GS3D_CMD_LOAD_DATASET = 1,   /* task */
    GS3D_CMD_EXPORT = 2,         /* task: {"path": "...", "format": "ply"|"csv"} */
    GS3D_CMD_PREPROCESS = 3,     /* task: {"source": "...", "bundle": "..."} */
    GS3D_CMD_CAMERA = 4,         /* immediate: rotate/pan/scroll deltas */
    GS3D_CMD_RENDER_OPTIONS = 5, /* immediate: point size, colormap, clip, ... */
    GS3D_CMD_PICK = 6,           /* immediate: {"x": .., "y": ..} */
    GS3D_CMD_SET_VIEWPORT = 7,   /* immediate: {"w": .., "h": ..} */
    GS3D_CMD_CANCEL = 8          /* task_id, no payload */
} gs3d_command_kind;

typedef struct gs3d_command {
    gs3d_abi_header header;
    int32_t kind;           /* gs3d_command_kind */
    int32_t reserved;       /* must be 0 */
    uint64_t task_id;       /* CANCEL target; 0 elsewhere */
    const char* payload;    /* UTF-8 JSON, may be NULL */
    uint32_t payload_bytes; /* 0 means strlen(payload) */
    uint32_t reserved2;     /* must be 0 */
} gs3d_command;

typedef enum gs3d_task_status {
    GS3D_TASK_PENDING = 0,
    GS3D_TASK_RUNNING = 1,
    GS3D_TASK_DONE = 2,
    GS3D_TASK_FAILED = 3,
    GS3D_TASK_CANCELLED = 4
} gs3d_task_status;

typedef struct gs3d_task_state {
    gs3d_abi_header header;
    uint64_t task_id;
    int32_t kind;
    int32_t status;       /* gs3d_task_status */
    float progress;       /* 0..1, never decreases */
    int32_t error_code;   /* GS3D_OK unless status is FAILED */
    uint32_t reserved;
    const char* message;  /* engine-owned; "" when empty */
} gs3d_task_state;

typedef struct gs3d_state {
    gs3d_abi_header header;
    uint32_t wake_mask;   /* events since the previous poll; read-and-clear */
    int32_t last_error;   /* gs3d_status of the most recent failing call */
    uint64_t frame_sequence;
    uint64_t point_count;
    uint32_t viewport_w;
    uint32_t viewport_h;
    uint32_t active_tasks;
    uint32_t engine_abi_version;
    double camera_pos[3];
    double camera_target[3];
    double camera_up[3];
    double last_render_ms;
} gs3d_state;

typedef struct gs3d_engine gs3d_engine;
typedef void (*gs3d_wake_fn)(void* user, uint32_t mask);

/* Creates an engine from a JSON config (may be NULL for defaults). */
int32_t geoscatter3d_engine_create(const char* config_json, gs3d_engine** out_engine);
void geoscatter3d_engine_destroy(gs3d_engine* engine);

/* Registers the wake callback. Called from engine threads with a wake mask; the
 * callback must be short and must not call back into the engine. */
int32_t geoscatter3d_engine_set_wake(gs3d_engine* engine, gs3d_wake_fn on_wake, void* user);

/* Submits a command. Task commands write the new task id to out_task_id (may be NULL). */
int32_t geoscatter3d_engine_submit(gs3d_engine* engine, const gs3d_command* command,
                                  uint64_t* out_task_id);

/* Copies the current state without waiting for the render thread. */
int32_t geoscatter3d_engine_poll_state(gs3d_engine* engine, gs3d_state* out_state);

/* Copies one task's state. */
int32_t geoscatter3d_engine_task_state(gs3d_engine* engine, uint64_t task_id,
                                      gs3d_task_state* out_state);

/* Requests cancellation; the task may finish first — check its status. */
int32_t geoscatter3d_engine_cancel(gs3d_engine* engine, uint64_t task_id);

/* Copies the latest rendered frame (RGBA8, top-down). */
int32_t geoscatter3d_engine_read_frame(gs3d_engine* engine, uint8_t* out_rgba,
                                      uint32_t max_bytes, uint32_t* out_w, uint32_t* out_h,
                                      uint32_t* out_stride, uint64_t* out_sequence);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* GEOSCATTER3D_ABI_H */
