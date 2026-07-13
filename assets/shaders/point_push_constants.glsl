#ifndef GS3D_POINT_PUSH_CONSTANTS_GLSL
#define GS3D_POINT_PUSH_CONSTANTS_GLSL

// Canonical GLSL layout shared by every point shader. Keep this in lockstep
// with gs3d::render::PointPushConstants; the C++ header asserts every offset.
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
} pc;                     // total: 128 bytes

#endif
