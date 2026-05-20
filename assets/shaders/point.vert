#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in float in_value;

layout(location = 0) out float out_value;
layout(location = 1) out vec3  out_world_pos;

layout(push_constant) uniform PointPushConstants {
    mat4  mvp;
    float value_min;
    float value_range;
    float point_size;
    float clip_mode;
    vec4  clip_min;       // xyz = clip bounds, w = unused
    vec4  clip_max;       // xyz = clip bounds, w = unused
    uint  attr_index;     // 0 = value, 1 = z (elevation)
} pc;

void main() {
    gl_Position  = pc.mvp * vec4(in_position, 1.0);
    gl_PointSize = pc.point_size;
    out_world_pos = in_position;

    // Select attribute for color mapping (Potree / CloudCompare pattern:
    // switch active attribute via uniform, no GPU data re-upload needed).
    float raw_attr;
    if (pc.attr_index == 1u) {
        raw_attr = in_position.z;   // elevation / depth
    } else {
        raw_attr = in_value;        // stored attribute (amplitude, etc.)
    }

    if (pc.value_range > 0.0) {
        out_value = clamp((raw_attr - pc.value_min) / pc.value_range, 0.0, 1.0);
    } else {
        out_value = 0.0;
    }
}
