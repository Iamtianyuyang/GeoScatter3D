#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in float in_value;
layout(location = 2) in uint in_point_id;

layout(location = 0) out float out_value;
layout(location = 1) out vec3  out_world_pos;
layout(location = 2) flat out uint out_point_id;

/*
 * PUSH CONSTANT LAYOUT — MUST match PointPipeline.hpp byte-for-byte.
 * If you change any field or reorder them, update BOTH point.vert AND point.frag
 * to match the C++ struct AND keep the offset comments aligned.
 *
 * Offset map identical to point.frag; see point.frag for the authoritative table.
 */
layout(push_constant) uniform PointPushConstants {
    mat4  mvp;            // offset   0
    vec4  clip_min;       // offset  64 — xyz = spatial clip, w = value_clip_min
    vec4  clip_max;       // offset  80 — xyz = spatial clip, w = value_clip_max
    float color_min;      // offset  96 — min of the selected color attribute
    float color_range;    // offset 100 — range of the selected color attribute
    float height_offset;  // offset 104 — height = offset + raw * mult (pre-computed on CPU)
    float height_mult;    // offset 108
    float point_size;     // offset 112
    uint  height_source;  // offset 116 — AttrPhysicalSource: 0=z, 1=value, ...
    uint  color_source;   // offset 120 — AttrPhysicalSource: 0=z, 1=value, ...
    uint  flags;          // offset 124 — bit0=spatial_clip, bits1-7=colormap, bit8=value_clip
} pc;

void main() {
    // ===== 高度通道：从选中的物理来源读取原始值，做 CPU 预计算的线性映射 =====
    float raw_height;
    if (pc.height_source == 0u) {
        raw_height = in_position.z;          // Gs3dPoint::z
    } else if (pc.height_source == 1u) {
        raw_height = in_value;               // Gs3dPoint::value
    } else {
        raw_height = in_position.z;          // fallback
    }
    // else if (pc.height_source == 2u) { raw_height = in_attr2; }  // 扩展预留

    float height = pc.height_offset + raw_height * pc.height_mult;
    gl_Position = pc.mvp * vec4(in_position.xy, height, 1.0);
    gl_PointSize = pc.point_size;
    out_world_pos = vec3(in_position.xy, height);
    out_point_id = in_point_id;

    // ===== 颜色通道：从选中的物理来源读取原始值，归一化到 [0,1] =====
    float raw_color;
    if (pc.color_source == 0u) {
        raw_color = in_position.z;           // Gs3dPoint::z
    } else if (pc.color_source == 1u) {
        raw_color = in_value;                // Gs3dPoint::value
    } else {
        raw_color = in_value;                // fallback
    }
    // else if (pc.color_source == 2u) { raw_color = in_attr2; }  // 扩展预留

    if (pc.color_range > 0.0) {
        out_value = clamp((raw_color - pc.color_min) / pc.color_range, 0.0, 1.0);
    } else {
        out_value = 0.0;
    }
}
