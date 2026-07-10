#pragma once

#include "imgui.h"

#include <cmath>

namespace gs3d::ui {

/*
 * 统一语义色板（默认初始化为「碳蓝 · Modern SaaS」主题）。
 *
 * 所有颜色以设计稿的 sRGB 十六进制值书写，直接作为 sRGB 值使用。
 * 交换链使用 UNORM 格式——硬件不做 sRGB EOTF，shader/ImGui 输出的
 * 值即是屏幕显示值。不再需要 sRGB→linear 预转换。
 *
 * palette:: 里的值不是常量：ui::apply_theme()（见 Theme.hpp）会在主题
 * 切换时整体重写，各面板每帧取值即可自动跟随主题。不要把 palette 值
 * 缓存到 static/constexpr 里。三套主题的 token 定义在 Theme.cpp。
 */

// sRGB 分量 (0-255) → ImVec4（直接归一化，不做颜色空间转换）。
[[nodiscard]]
inline ImVec4 srgb_vec4(int r, int g, int b, float alpha = 1.0f) {
    return ImVec4(
        static_cast<float>(r) / 255.0f,
        static_cast<float>(g) / 255.0f,
        static_cast<float>(b) / 255.0f,
        alpha
    );
}

// sRGB 分量 (0-255) → ImU32（draw list 用，直接归一化）。
[[nodiscard]]
inline ImU32 srgb_color(int r, int g, int b, int alpha = 255) {
    return ImGui::ColorConvertFloat4ToU32(
        srgb_vec4(r, g, b, static_cast<float>(alpha) / 255.0f)
    );
}

namespace palette {

// ── 基础 ────────────────────────────────────────────────────────────
inline ImVec4 kBg       = srgb_vec4(0xF7, 0xF8, 0xFA); // 背景
inline ImVec4 kText     = srgb_vec4(0x16, 0x16, 0x16); // 正文
inline ImVec4 kTextDim  = srgb_vec4(0x6F, 0x6F, 0x6F); // 弱文字
inline ImVec4 kTextFaint = srgb_vec4(0xA0, 0xA0, 0xA0); // 更弱文字

// ── 派生表面（中性灰阶）─────────────────────────────────
inline ImVec4 kMenuBg   = srgb_vec4(0xFF, 0xFF, 0xFF); // 菜单栏
inline ImVec4 kSurface  = srgb_vec4(0xFF, 0xFF, 0xFF); // 面板/弹窗
inline ImVec4 kSurfaceHover = srgb_vec4(0xF5, 0xF5, 0xF5); // 卡片悬停
inline ImVec4 kFrame    = srgb_vec4(0xF7, 0xF8, 0xFA); // 输入框
inline ImVec4 kBorder   = srgb_vec4(0xE5, 0xE7, 0xEB); // 边框

// ── 语义色（语义用途见注释）─────────────────────────────────────────
inline ImVec4 kGreen    = srgb_vec4(0x24, 0xA1, 0x48); // 成功绿：确认/通过
inline ImVec4 kBlue     = srgb_vec4(0x0F, 0x62, 0xFE); // 专业蓝：Z轴/信息
inline ImVec4 kPurple   = srgb_vec4(0xA2, 0x6E, 0xC4); // 控制流紫：特殊状态
inline ImVec4 kYellow   = srgb_vec4(0xF1, 0xC2, 0x1B); // 明亮黄：测量/十字线
inline ImVec4 kTeal     = srgb_vec4(0x4E, 0xC9, 0xB0); // 类型青绿：区域选择
inline ImVec4 kVarBlue  = srgb_vec4(0x8A, 0xB6, 0xFF); // 变量浅蓝：选中点/坐标
inline ImVec4 kOrange   = srgb_vec4(0xCE, 0x91, 0x78); // 字符串橙：警告/降级
inline ImVec4 kNumGreen = srgb_vec4(0x24, 0xA1, 0x48); // 数字绿：数值读数
inline ImVec4 kGray     = srgb_vec4(0xB0, 0xB0, 0xB0); // 运算符灰：标尺/刻度
inline ImVec4 kRed      = srgb_vec4(0xF4, 0x47, 0x47); // 错误红：错误/X轴
inline ImVec4 kAccent   = srgb_vec4(0x0F, 0x62, 0xFE); // 专业蓝：交互强调
inline ImVec4 kAccentActive = srgb_vec4(0x0B, 0x4E, 0xCB); // 强调色按下/拖动态
inline ImVec4 kOnAccent = srgb_vec4(0xFF, 0xFF, 0xFF); // accent 底上的文字/对勾
inline ImVec4 kFrameHover = srgb_vec4(0xE8, 0xEA, 0xED); // 输入框悬停底

// ── 3D 视口暗色画布（明暗对比核心）─────────────────────────────────
inline ImVec4 kViewportBg = srgb_vec4(0x1A, 0x1D, 0x23); // 暗色画布
inline ImVec4 kViewportBorder = srgb_vec4(0x2A, 0x30, 0x38); // 视口边框

} // namespace palette

// 语义色的 ImU32 便捷取值（draw list 场景），可覆盖 alpha。
[[nodiscard]]
inline ImU32 to_u32(const ImVec4& color, int alpha = 255) {
    ImVec4 c = color;
    c.w = static_cast<float>(alpha) / 255.0f;
    return ImGui::ColorConvertFloat4ToU32(c);
}

// 用户自选颜色（如测量线/十字准线）按 sRGB ImU32 存储，交换链是 UNORM
// 格式——直接使用无需转换。保留此函数作为标识（identity），避免调用处
// 改动过大。
[[nodiscard]]
inline ImU32 srgb_u32_to_linear(ImU32 srgb) {
    return srgb;
}

} // namespace gs3d::ui
