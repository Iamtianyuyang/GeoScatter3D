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


// 256-level Jet rainbow LUT — 256 discrete entries, no interpolation.
// Generated by tools/generate_rainbow256_lut.py
const vec3 kRainbow256[256] = vec3[256](
    vec3(0.0, 0.0, 0.5),  //   0
    vec3(0.0, 0.0, 0.51568627),  //   1
    vec3(0.0, 0.0, 0.53137255),  //   2
    vec3(0.0, 0.0, 0.54705882),  //   3
    vec3(0.0, 0.0, 0.5627451),  //   4
    vec3(0.0, 0.0, 0.57843137),  //   5
    vec3(0.0, 0.0, 0.59411765),  //   6
    vec3(0.0, 0.0, 0.60980392),  //   7
    vec3(0.0, 0.0, 0.6254902),  //   8
    vec3(0.0, 0.0, 0.64117647),  //   9
    vec3(0.0, 0.0, 0.65686275),  //  10
    vec3(0.0, 0.0, 0.67254902),  //  11
    vec3(0.0, 0.0, 0.68823529),  //  12
    vec3(0.0, 0.0, 0.70392157),  //  13
    vec3(0.0, 0.0, 0.71960784),  //  14
    vec3(0.0, 0.0, 0.73529412),  //  15
    vec3(0.0, 0.0, 0.75098039),  //  16
    vec3(0.0, 0.0, 0.76666667),  //  17
    vec3(0.0, 0.0, 0.78235294),  //  18
    vec3(0.0, 0.0, 0.79803922),  //  19
    vec3(0.0, 0.0, 0.81372549),  //  20
    vec3(0.0, 0.0, 0.82941176),  //  21
    vec3(0.0, 0.0, 0.84509804),  //  22
    vec3(0.0, 0.0, 0.86078431),  //  23
    vec3(0.0, 0.0, 0.87647059),  //  24
    vec3(0.0, 0.0, 0.89215686),  //  25
    vec3(0.0, 0.0, 0.90784314),  //  26
    vec3(0.0, 0.0, 0.92352941),  //  27
    vec3(0.0, 0.0, 0.93921569),  //  28
    vec3(0.0, 0.0, 0.95490196),  //  29
    vec3(0.0, 0.0, 0.97058824),  //  30
    vec3(0.0, 0.0, 0.98627451),  //  31
    vec3(0.0, 0.00196078, 1.0),  //  32
    vec3(0.0, 0.01764706, 1.0),  //  33
    vec3(0.0, 0.03333333, 1.0),  //  34
    vec3(0.0, 0.04901961, 1.0),  //  35
    vec3(0.0, 0.06470588, 1.0),  //  36
    vec3(0.0, 0.08039216, 1.0),  //  37
    vec3(0.0, 0.09607843, 1.0),  //  38
    vec3(0.0, 0.11176471, 1.0),  //  39
    vec3(0.0, 0.12745098, 1.0),  //  40
    vec3(0.0, 0.14313725, 1.0),  //  41
    vec3(0.0, 0.15882353, 1.0),  //  42
    vec3(0.0, 0.1745098, 1.0),  //  43
    vec3(0.0, 0.19019608, 1.0),  //  44
    vec3(0.0, 0.20588235, 1.0),  //  45
    vec3(0.0, 0.22156863, 1.0),  //  46
    vec3(0.0, 0.2372549, 1.0),  //  47
    vec3(0.0, 0.25294118, 1.0),  //  48
    vec3(0.0, 0.26862745, 1.0),  //  49
    vec3(0.0, 0.28431373, 1.0),  //  50
    vec3(0.0, 0.3, 1.0),  //  51
    vec3(0.0, 0.31568627, 1.0),  //  52
    vec3(0.0, 0.33137255, 1.0),  //  53
    vec3(0.0, 0.34705882, 1.0),  //  54
    vec3(0.0, 0.3627451, 1.0),  //  55
    vec3(0.0, 0.37843137, 1.0),  //  56
    vec3(0.0, 0.39411765, 1.0),  //  57
    vec3(0.0, 0.40980392, 1.0),  //  58
    vec3(0.0, 0.4254902, 1.0),  //  59
    vec3(0.0, 0.44117647, 1.0),  //  60
    vec3(0.0, 0.45686275, 1.0),  //  61
    vec3(0.0, 0.47254902, 1.0),  //  62
    vec3(0.0, 0.48823529, 1.0),  //  63
    vec3(0.0, 0.50392157, 1.0),  //  64
    vec3(0.0, 0.51960784, 1.0),  //  65
    vec3(0.0, 0.53529412, 1.0),  //  66
    vec3(0.0, 0.55098039, 1.0),  //  67
    vec3(0.0, 0.56666667, 1.0),  //  68
    vec3(0.0, 0.58235294, 1.0),  //  69
    vec3(0.0, 0.59803922, 1.0),  //  70
    vec3(0.0, 0.61372549, 1.0),  //  71
    vec3(0.0, 0.62941176, 1.0),  //  72
    vec3(0.0, 0.64509804, 1.0),  //  73
    vec3(0.0, 0.66078431, 1.0),  //  74
    vec3(0.0, 0.67647059, 1.0),  //  75
    vec3(0.0, 0.69215686, 1.0),  //  76
    vec3(0.0, 0.70784314, 1.0),  //  77
    vec3(0.0, 0.72352941, 1.0),  //  78
    vec3(0.0, 0.73921569, 1.0),  //  79
    vec3(0.0, 0.75490196, 1.0),  //  80
    vec3(0.0, 0.77058824, 1.0),  //  81
    vec3(0.0, 0.78627451, 1.0),  //  82
    vec3(0.0, 0.80196078, 1.0),  //  83
    vec3(0.0, 0.81764706, 1.0),  //  84
    vec3(0.0, 0.83333333, 1.0),  //  85
    vec3(0.0, 0.84901961, 1.0),  //  86
    vec3(0.0, 0.86470588, 1.0),  //  87
    vec3(0.0, 0.88039216, 1.0),  //  88
    vec3(0.0, 0.89607843, 1.0),  //  89
    vec3(0.0, 0.91176471, 1.0),  //  90
    vec3(0.0, 0.92745098, 1.0),  //  91
    vec3(0.0, 0.94313725, 1.0),  //  92
    vec3(0.0, 0.95882353, 1.0),  //  93
    vec3(0.0, 0.9745098, 1.0),  //  94
    vec3(0.0, 0.99019608, 1.0),  //  95
    vec3(0.00588235, 1.0, 0.99411765),  //  96
    vec3(0.02156863, 1.0, 0.97843137),  //  97
    vec3(0.0372549, 1.0, 0.9627451),  //  98
    vec3(0.05294118, 1.0, 0.94705882),  //  99
    vec3(0.06862745, 1.0, 0.93137255),  // 100
    vec3(0.08431373, 1.0, 0.91568627),  // 101
    vec3(0.1, 1.0, 0.9),  // 102
    vec3(0.11568627, 1.0, 0.88431373),  // 103
    vec3(0.13137255, 1.0, 0.86862745),  // 104
    vec3(0.14705882, 1.0, 0.85294118),  // 105
    vec3(0.1627451, 1.0, 0.8372549),  // 106
    vec3(0.17843137, 1.0, 0.82156863),  // 107
    vec3(0.19411765, 1.0, 0.80588235),  // 108
    vec3(0.20980392, 1.0, 0.79019608),  // 109
    vec3(0.2254902, 1.0, 0.7745098),  // 110
    vec3(0.24117647, 1.0, 0.75882353),  // 111
    vec3(0.25686275, 1.0, 0.74313725),  // 112
    vec3(0.27254902, 1.0, 0.72745098),  // 113
    vec3(0.28823529, 1.0, 0.71176471),  // 114
    vec3(0.30392157, 1.0, 0.69607843),  // 115
    vec3(0.31960784, 1.0, 0.68039216),  // 116
    vec3(0.33529412, 1.0, 0.66470588),  // 117
    vec3(0.35098039, 1.0, 0.64901961),  // 118
    vec3(0.36666667, 1.0, 0.63333333),  // 119
    vec3(0.38235294, 1.0, 0.61764706),  // 120
    vec3(0.39803922, 1.0, 0.60196078),  // 121
    vec3(0.41372549, 1.0, 0.58627451),  // 122
    vec3(0.42941176, 1.0, 0.57058824),  // 123
    vec3(0.44509804, 1.0, 0.55490196),  // 124
    vec3(0.46078431, 1.0, 0.53921569),  // 125
    vec3(0.47647059, 1.0, 0.52352941),  // 126
    vec3(0.49215686, 1.0, 0.50784314),  // 127
    vec3(0.50784314, 1.0, 0.49215686),  // 128
    vec3(0.52352941, 1.0, 0.47647059),  // 129
    vec3(0.53921569, 1.0, 0.46078431),  // 130
    vec3(0.55490196, 1.0, 0.44509804),  // 131
    vec3(0.57058824, 1.0, 0.42941176),  // 132
    vec3(0.58627451, 1.0, 0.41372549),  // 133
    vec3(0.60196078, 1.0, 0.39803922),  // 134
    vec3(0.61764706, 1.0, 0.38235294),  // 135
    vec3(0.63333333, 1.0, 0.36666667),  // 136
    vec3(0.64901961, 1.0, 0.35098039),  // 137
    vec3(0.66470588, 1.0, 0.33529412),  // 138
    vec3(0.68039216, 1.0, 0.31960784),  // 139
    vec3(0.69607843, 1.0, 0.30392157),  // 140
    vec3(0.71176471, 1.0, 0.28823529),  // 141
    vec3(0.72745098, 1.0, 0.27254902),  // 142
    vec3(0.74313725, 1.0, 0.25686275),  // 143
    vec3(0.75882353, 1.0, 0.24117647),  // 144
    vec3(0.7745098, 1.0, 0.2254902),  // 145
    vec3(0.79019608, 1.0, 0.20980392),  // 146
    vec3(0.80588235, 1.0, 0.19411765),  // 147
    vec3(0.82156863, 1.0, 0.17843137),  // 148
    vec3(0.8372549, 1.0, 0.1627451),  // 149
    vec3(0.85294118, 1.0, 0.14705882),  // 150
    vec3(0.86862745, 1.0, 0.13137255),  // 151
    vec3(0.88431373, 1.0, 0.11568627),  // 152
    vec3(0.9, 1.0, 0.1),  // 153
    vec3(0.91568627, 1.0, 0.08431373),  // 154
    vec3(0.93137255, 1.0, 0.06862745),  // 155
    vec3(0.94705882, 1.0, 0.05294118),  // 156
    vec3(0.9627451, 1.0, 0.0372549),  // 157
    vec3(0.97843137, 1.0, 0.02156863),  // 158
    vec3(0.99411765, 1.0, 0.00588235),  // 159
    vec3(1.0, 0.99019608, 0.0),  // 160
    vec3(1.0, 0.9745098, 0.0),  // 161
    vec3(1.0, 0.95882353, 0.0),  // 162
    vec3(1.0, 0.94313725, 0.0),  // 163
    vec3(1.0, 0.92745098, 0.0),  // 164
    vec3(1.0, 0.91176471, 0.0),  // 165
    vec3(1.0, 0.89607843, 0.0),  // 166
    vec3(1.0, 0.88039216, 0.0),  // 167
    vec3(1.0, 0.86470588, 0.0),  // 168
    vec3(1.0, 0.84901961, 0.0),  // 169
    vec3(1.0, 0.83333333, 0.0),  // 170
    vec3(1.0, 0.81764706, 0.0),  // 171
    vec3(1.0, 0.80196078, 0.0),  // 172
    vec3(1.0, 0.78627451, 0.0),  // 173
    vec3(1.0, 0.77058824, 0.0),  // 174
    vec3(1.0, 0.75490196, 0.0),  // 175
    vec3(1.0, 0.73921569, 0.0),  // 176
    vec3(1.0, 0.72352941, 0.0),  // 177
    vec3(1.0, 0.70784314, 0.0),  // 178
    vec3(1.0, 0.69215686, 0.0),  // 179
    vec3(1.0, 0.67647059, 0.0),  // 180
    vec3(1.0, 0.66078431, 0.0),  // 181
    vec3(1.0, 0.64509804, 0.0),  // 182
    vec3(1.0, 0.62941176, 0.0),  // 183
    vec3(1.0, 0.61372549, 0.0),  // 184
    vec3(1.0, 0.59803922, 0.0),  // 185
    vec3(1.0, 0.58235294, 0.0),  // 186
    vec3(1.0, 0.56666667, 0.0),  // 187
    vec3(1.0, 0.55098039, 0.0),  // 188
    vec3(1.0, 0.53529412, 0.0),  // 189
    vec3(1.0, 0.51960784, 0.0),  // 190
    vec3(1.0, 0.50392157, 0.0),  // 191
    vec3(1.0, 0.48823529, 0.0),  // 192
    vec3(1.0, 0.47254902, 0.0),  // 193
    vec3(1.0, 0.45686275, 0.0),  // 194
    vec3(1.0, 0.44117647, 0.0),  // 195
    vec3(1.0, 0.4254902, 0.0),  // 196
    vec3(1.0, 0.40980392, 0.0),  // 197
    vec3(1.0, 0.39411765, 0.0),  // 198
    vec3(1.0, 0.37843137, 0.0),  // 199
    vec3(1.0, 0.3627451, 0.0),  // 200
    vec3(1.0, 0.34705882, 0.0),  // 201
    vec3(1.0, 0.33137255, 0.0),  // 202
    vec3(1.0, 0.31568627, 0.0),  // 203
    vec3(1.0, 0.3, 0.0),  // 204
    vec3(1.0, 0.28431373, 0.0),  // 205
    vec3(1.0, 0.26862745, 0.0),  // 206
    vec3(1.0, 0.25294118, 0.0),  // 207
    vec3(1.0, 0.2372549, 0.0),  // 208
    vec3(1.0, 0.22156863, 0.0),  // 209
    vec3(1.0, 0.20588235, 0.0),  // 210
    vec3(1.0, 0.19019608, 0.0),  // 211
    vec3(1.0, 0.1745098, 0.0),  // 212
    vec3(1.0, 0.15882353, 0.0),  // 213
    vec3(1.0, 0.14313725, 0.0),  // 214
    vec3(1.0, 0.12745098, 0.0),  // 215
    vec3(1.0, 0.11176471, 0.0),  // 216
    vec3(1.0, 0.09607843, 0.0),  // 217
    vec3(1.0, 0.08039216, 0.0),  // 218
    vec3(1.0, 0.06470588, 0.0),  // 219
    vec3(1.0, 0.04901961, 0.0),  // 220
    vec3(1.0, 0.03333333, 0.0),  // 221
    vec3(1.0, 0.01764706, 0.0),  // 222
    vec3(1.0, 0.00196078, 0.0),  // 223
    vec3(0.98627451, 0.0, 0.0),  // 224
    vec3(0.97058824, 0.0, 0.0),  // 225
    vec3(0.95490196, 0.0, 0.0),  // 226
    vec3(0.93921569, 0.0, 0.0),  // 227
    vec3(0.92352941, 0.0, 0.0),  // 228
    vec3(0.90784314, 0.0, 0.0),  // 229
    vec3(0.89215686, 0.0, 0.0),  // 230
    vec3(0.87647059, 0.0, 0.0),  // 231
    vec3(0.86078431, 0.0, 0.0),  // 232
    vec3(0.84509804, 0.0, 0.0),  // 233
    vec3(0.82941176, 0.0, 0.0),  // 234
    vec3(0.81372549, 0.0, 0.0),  // 235
    vec3(0.79803922, 0.0, 0.0),  // 236
    vec3(0.78235294, 0.0, 0.0),  // 237
    vec3(0.76666667, 0.0, 0.0),  // 238
    vec3(0.75098039, 0.0, 0.0),  // 239
    vec3(0.73529412, 0.0, 0.0),  // 240
    vec3(0.71960784, 0.0, 0.0),  // 241
    vec3(0.70392157, 0.0, 0.0),  // 242
    vec3(0.68823529, 0.0, 0.0),  // 243
    vec3(0.67254902, 0.0, 0.0),  // 244
    vec3(0.65686275, 0.0, 0.0),  // 245
    vec3(0.64117647, 0.0, 0.0),  // 246
    vec3(0.6254902, 0.0, 0.0),  // 247
    vec3(0.60980392, 0.0, 0.0),  // 248
    vec3(0.59411765, 0.0, 0.0),  // 249
    vec3(0.57843137, 0.0, 0.0),  // 250
    vec3(0.5627451, 0.0, 0.0),  // 251
    vec3(0.54705882, 0.0, 0.0),  // 252
    vec3(0.53137255, 0.0, 0.0),  // 253
    vec3(0.51568627, 0.0, 0.0),  // 254
    vec3(0.5, 0.0, 0.0)  // 255
);

vec3 colormap_rainbow256(float t) {
    t = clamp(t, 0.0, 1.0);
    // Discrete 256-level lookup — NO interpolation between levels.
    int idx = int(t * 255.0);
    return kRainbow256[idx];
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
    case 8u:  return colormap_rainbow256(t);
    default:  return colormap_geo(t);
    }
}

void main() {
    // Extract bit fields from flags.
    uint spatial_clip  = pc.flags & 1u;
    uint colormap_idx  = (pc.flags >> 1u) & 0x7Fu;
    uint value_clip    = (pc.flags >> 8u) & 1u;
    uint point_shape   = (pc.flags >> 9u) & 0x7u;

    // ── Point shape via gl_PointCoord discard ──
    // gl_PointCoord ∈ [0,1]², (0,0)=top-left of the rasterized square.
    // Discard fragments outside the selected shape's implicit region.
    {
        vec2  pc = gl_PointCoord - vec2(0.5);       // center at (0,0)
        float r  = 0.5;                              // inscribed radius

        if (point_shape == 1u) {
            // Circle: discard outside radius
            if (dot(pc, pc) > r * r) discard;
        } else if (point_shape == 2u) {
            // Diamond: discard outside Manhattan distance
            float md = abs(pc.x) + abs(pc.y);
            if (md > r) discard;
        } else if (point_shape == 3u) {
            // Triangle (upward-pointing): top vertex at (0.5, 0.15),
            // base from (0.1, 0.88) to (0.9, 0.88).
            float top_y   = -0.35;
            float base_y  =  0.38;
            float half_b  =  0.40;
            float ty      = (pc.y - top_y) / (base_y - top_y);
            float hw      = half_b * ty;
            if (ty < 0.0 || ty > 1.0 || abs(pc.x) > hw) discard;
        }
        // point_shape == 0u: Square — no discard, hardware default.
    }

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
