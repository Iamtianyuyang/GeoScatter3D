#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in float in_value;

layout(location = 0) out float out_value;
layout(location = 1) out vec3 out_world_pos;

layout(push_constant) uniform PointPushConstants {
    mat4  mvp;
    float value_min;
    float value_range;
    float point_size;
    float clip_mode;
    vec3  clip_min;   // std430: alignment 16, implicit 4-byte pad before clip_max
    vec3  clip_max;
} pc;

void main() {
    gl_Position  = pc.mvp * vec4(in_position, 1.0);
    gl_PointSize = pc.point_size;
    out_world_pos = in_position;

    if (pc.value_range > 0.0) {
        out_value = clamp((in_value - pc.value_min) / pc.value_range, 0.0, 1.0);
    } else {
        out_value = 0.0;
    }
}
