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
 *   64:  vec4  clip_min      (16 bytes) — xyz = spatial clip, w = value_clip_min
 *   80:  vec4  clip_max      (16 bytes) — xyz = spatial clip, w = value_clip_max
 *   96:  float color_min     (4 bytes)  — unused in frag
 *   100: float color_range   (4 bytes)  — unused in frag
 *   104: float height_offset (4 bytes)  — unused in frag
 *   108: float height_mult   (4 bytes)  — unused in frag
 *   112: float point_size    (4 bytes)  — unused in frag
 *   116: uint  height_source (4 bytes)  — unused in frag
 *   120: uint  color_source  (4 bytes)  — unused in frag
 *   124: uint  flags         (4 bytes)  — bit0=spatial, bits1-7=colormap, bit8=value_clip
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
    uint  flags;          // offset 124
} pc;

// ── Colormap 0: Geo (blue-cyan-green-yellow-red) ──
vec3 colormap_geo(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 c0 = vec3(0.05, 0.10, 0.35);
    vec3 c1 = vec3(0.00, 0.45, 0.85);
    vec3 c2 = vec3(0.10, 0.85, 0.35);
    vec3 c3 = vec3(0.95, 0.85, 0.10);
    vec3 c4 = vec3(0.95, 0.20, 0.05);
    if (t < 0.25)      return mix(c0, c1, t / 0.25);
    else if (t < 0.50) return mix(c1, c2, (t - 0.25) / 0.25);
    else if (t < 0.75) return mix(c2, c3, (t - 0.50) / 0.25);
    else               return mix(c3, c4, (t - 0.75) / 0.25);
}

// ── Colormap 1: Viridis (perceptually uniform) ──
vec3 colormap_viridis(float t) {
    t = clamp(t, 0.0, 1.0);
    // Sampled from matplotlib viridis at 6 control points
    vec3 cols[6] = vec3[6](
        vec3(0.267, 0.004, 0.329),
        vec3(0.282, 0.141, 0.458),
        vec3(0.127, 0.409, 0.495),
        vec3(0.210, 0.618, 0.383),
        vec3(0.678, 0.792, 0.138),
        vec3(0.993, 0.906, 0.144)
    );
    float s = t * 5.0;
    int i = clamp(int(s), 0, 4);
    return mix(cols[i], cols[i+1], fract(s));
}

// ── Colormap 2: Jet (classic rainbow) ──
vec3 colormap_jet(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 c0 = vec3(0.0, 0.0, 0.5);
    vec3 c1 = vec3(0.0, 0.5, 1.0);
    vec3 c2 = vec3(0.0, 1.0, 1.0);
    vec3 c3 = vec3(1.0, 1.0, 0.0);
    vec3 c4 = vec3(1.0, 0.0, 0.0);
    if (t < 0.25)      return mix(c0, c1, t / 0.25);
    else if (t < 0.50) return mix(c1, c2, (t - 0.25) / 0.25);
    else if (t < 0.75) return mix(c2, c3, (t - 0.50) / 0.25);
    else               return mix(c3, c4, (t - 0.75) / 0.25);
}

// ── Colormap 3: Grayscale ──
vec3 colormap_gray(float t) {
    t = clamp(t, 0.0, 1.0);
    return vec3(t);
}

// ── Colormap 4: Thermal (black-red-yellow-white) ──
vec3 colormap_thermal(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 cols[5] = vec3[5](
        vec3(0.0, 0.0, 0.0),
        vec3(0.6, 0.0, 0.0),
        vec3(1.0, 0.3, 0.0),
        vec3(1.0, 0.9, 0.0),
        vec3(1.0, 1.0, 1.0)
    );
    float s = t * 4.0;
    int i = clamp(int(s), 0, 3);
    return mix(cols[i], cols[i+1], fract(s));
}

// ── Colormap 5: Coolwarm (blue-white-red, diverging) ──
vec3 colormap_coolwarm(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 cols[5] = vec3[5](
        vec3(0.23, 0.30, 0.75),
        vec3(0.55, 0.60, 1.00),
        vec3(0.95, 0.95, 0.95),
        vec3(1.00, 0.55, 0.40),
        vec3(0.70, 0.02, 0.15)
    );
    float s = t * 4.0;
    int i = clamp(int(s), 0, 3);
    return mix(cols[i], cols[i+1], fract(s));
}

// ── Colormap 6: Turbo ──
vec3 colormap_turbo(float t) {
    t = clamp(t, 0.0, 1.0);
    // Sampled from Google Turbo at 8 control points
    vec3 cols[8] = vec3[8](
        vec3(0.190, 0.072, 0.232),
        vec3(0.283, 0.354, 0.962),
        vec3(0.138, 0.572, 0.998),
        vec3(0.071, 0.754, 0.756),
        vec3(0.302, 0.874, 0.418),
        vec3(0.865, 0.886, 0.106),
        vec3(0.982, 0.659, 0.037),
        vec3(0.480, 0.016, 0.010)
    );
    float s = t * 7.0;
    int i = clamp(int(s), 0, 6);
    return mix(cols[i], cols[i+1], fract(s));
}

// ── Colormap 7: Plasma ──
vec3 colormap_plasma(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 cols[6] = vec3[6](
        vec3(0.050, 0.030, 0.528),
        vec3(0.369, 0.015, 0.633),
        vec3(0.674, 0.069, 0.474),
        vec3(0.896, 0.315, 0.155),
        vec3(0.986, 0.618, 0.070),
        vec3(0.940, 0.975, 0.131)
    );
    float s = t * 5.0;
    int i = clamp(int(s), 0, 4);
    return mix(cols[i], cols[i+1], fract(s));
}

vec3 colormap_dispatch(float t, uint index) {
    // ponytail: switch in shader, if performance matters switch to 1D texture lookup
    switch (index) {
    case 1u:  return colormap_viridis(t);
    case 2u:  return colormap_jet(t);
    case 3u:  return colormap_gray(t);
    case 4u:  return colormap_thermal(t);
    case 5u:  return colormap_coolwarm(t);
    case 6u:  return colormap_turbo(t);
    case 7u:  return colormap_plasma(t);
    default:  return colormap_geo(t);
    }
}

void main() {
    // Extract bit fields from flags.
    uint spatial_clip  = pc.flags & 1u;
    uint colormap_idx  = (pc.flags >> 1u) & 0x7Fu;
    uint value_clip    = (pc.flags >> 8u) & 1u;

    // Spatial clip (tile overlay clipping).
    if (spatial_clip > 0u) {
        bool inside =
            in_world_pos.x >= pc.clip_min.x && in_world_pos.x <= pc.clip_max.x &&
            in_world_pos.y >= pc.clip_min.y && in_world_pos.y <= pc.clip_max.y &&
            in_world_pos.z >= pc.clip_min.z && in_world_pos.z <= pc.clip_max.z;
        if (inside) discard;
    }

    // Value range clip — discard points outside [value_clip_min, value_clip_max].
    if (value_clip > 0u) {
        float vmin = pc.clip_min.w;
        float vmax = pc.clip_max.w;
        if (in_value < vmin || in_value > vmax) discard;
    }

    out_color = vec4(colormap_dispatch(in_value, colormap_idx), 1.0);
    out_pick_id = in_point_id;
    out_pick_depth = gl_FragCoord.z;
}
