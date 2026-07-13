#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in float in_value;
layout(location = 2) in uint in_point_id;

layout(location = 0) out float out_value;
layout(location = 1) out vec3  out_world_pos;
layout(location = 2) flat out uint out_point_id;

#extension GL_GOOGLE_include_directive : require
#include "point_push_constants.glsl"

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
