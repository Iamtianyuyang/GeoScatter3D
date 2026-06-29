#version 450

layout(location = 0) in float in_value;
layout(location = 1) in vec3  in_world_pos;
layout(location = 2) flat in uint in_point_id;

layout(location = 0) out vec4 out_color;
layout(location = 1) out uint out_pick_id;
layout(location = 2) out float out_pick_depth;

layout(push_constant) uniform PointPushConstants {
    mat4  mvp;
    float value_min;
    float value_range;
    float point_size;
    float clip_mode;
    vec4  clip_min;   // xyz = clip bounds, w = unused
    vec4  clip_max;   // xyz = clip bounds, w = unused
    uint  attr_index;
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
