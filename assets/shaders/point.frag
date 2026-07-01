#version 450

layout(location = 0) in float in_value;
layout(location = 1) in vec3  in_world_pos;
layout(location = 2) flat in uint in_point_id;

layout(location = 0) out vec4 out_color;
layout(location = 1) out uint out_pick_id;
layout(location = 2) out float out_pick_depth;

/*
 * PUSH CONSTANT LAYOUT — MUST match PointPipeline.hpp byte-for-byte.
 * If you change any field or reorder them, update BOTH point.vert AND point.frag
 * to match the C++ struct AND keep the offset comments aligned.
 *
 * Offset map (C++ PointPushConstants → both shaders):
 *   0:   mat4  mvp           (64 bytes)
 *   64:  vec4  clip_min      (16 bytes) — tile bbox min (xyz), w unused
 *   80:  vec4  clip_max      (16 bytes) — tile bbox max (xyz), w unused
 *   96:  float color_min     (4 bytes)  — unused in frag (pre-normalized in vert)
 *   100: float color_range   (4 bytes)  — unused in frag
 *   104: float height_offset (4 bytes)  — unused in frag
 *   108: float height_mult   (4 bytes)  — unused in frag
 *   112: float point_size    (4 bytes)  — unused in frag
 *   116: uint  height_source (4 bytes)  — unused in frag
 *   120: uint  color_source  (4 bytes)  — unused in frag
 *   124: uint  clip_mode     (4 bytes)  — 0=off, 1=discard inside [clip_min,clip_max]
 * TOTAL: 128 bytes
 */
layout(push_constant) uniform PointPushConstants {
    mat4  mvp;            // offset   0
    vec4  clip_min;       // offset  64
    vec4  clip_max;       // offset  80
    float color_min;      // offset  96
    float color_range;    // offset 100
    float height_offset;  // offset 104
    float height_mult;    // offset 108
    float point_size;     // offset 112
    uint  height_source;  // offset 116
    uint  color_source;   // offset 120
    uint  clip_mode;      // offset 124
} pc;

vec3 colormap(float t) {
    t = clamp(t, 0.0, 1.0);

    vec3 c0 = vec3(0.05, 0.10, 0.35);
    vec3 c1 = vec3(0.00, 0.45, 0.85);
    vec3 c2 = vec3(0.10, 0.85, 0.35);
    vec3 c3 = vec3(0.95, 0.85, 0.10);
    vec3 c4 = vec3(0.95, 0.20, 0.05);

    if (t < 0.25) {
        return mix(c0, c1, t / 0.25);
    } else if (t < 0.50) {
        return mix(c1, c2, (t - 0.25) / 0.25);
    } else if (t < 0.75) {
        return mix(c2, c3, (t - 0.50) / 0.25);
    } else {
        return mix(c3, c4, (t - 0.75) / 0.25);
    }
}

void main() {
    if (pc.clip_mode > 0.5) {
        bool inside =
            in_world_pos.x >= pc.clip_min.x && in_world_pos.x <= pc.clip_max.x &&
            in_world_pos.y >= pc.clip_min.y && in_world_pos.y <= pc.clip_max.y &&
            in_world_pos.z >= pc.clip_min.z && in_world_pos.z <= pc.clip_max.z;
        if (inside) discard;
    }

    out_color = vec4(colormap(in_value), 1.0);
    out_pick_id = in_point_id;
    out_pick_depth = gl_FragCoord.z;
}
