#pragma once

#include "imgui.h"

#include <cmath>

namespace gs3d::ui {

/*
 * 统一语义色板（Modern SaaS Light Theme — IBM Carbon Blue accent）。
 *
 * 所有颜色以设计稿的 sRGB 十六进制值书写。主/副窗口交换链都是
 * B8G8R8A8_SRGB：硬件把 shader 输出当线性值再做 sRGB 编码，因此凡是
 * 交给 ImGui（样式或 draw list）的颜色必须先做 sRGB→linear 预转换，
 * 屏幕上才会显示为书写的原值。palette 命名空间里的常量已经转换完毕，
 * 可直接使用；新增颜色请一律通过 srgb_color()/srgb_vec4() 生成。
 *
 * 例外：色标（colormap）渐变预览等“所见即场景”的颜色不要转换——
 * 场景离屏渲染走的是另一条路径，预览需要与其保持一致。
 */

[[nodiscard]]
inline float srgb_to_linear(float srgb) {
    return srgb <= 0.04045f
        ? srgb / 12.92f
        : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
}

// sRGB 分量 (0-255) → 线性空间 ImVec4。alpha 不做转换。
[[nodiscard]]
inline ImVec4 srgb_vec4(int r, int g, int b, float alpha = 1.0f) {
    return ImVec4(
        srgb_to_linear(static_cast<float>(r) / 255.0f),
        srgb_to_linear(static_cast<float>(g) / 255.0f),
        srgb_to_linear(static_cast<float>(b) / 255.0f),
        alpha
    );
}

// sRGB 分量 (0-255) → 线性空间 ImU32（draw list 用）。
[[nodiscard]]
inline ImU32 srgb_color(int r, int g, int b, int alpha = 255) {
    return ImGui::ColorConvertFloat4ToU32(
        srgb_vec4(r, g, b, static_cast<float>(alpha) / 255.0f)
    );
}

namespace palette {

// ── 基础 ────────────────────────────────────────────────────────────
inline const ImVec4 kBg       = srgb_vec4(0xF7, 0xF8, 0xFA); // 背景
inline const ImVec4 kText     = srgb_vec4(0x16, 0x16, 0x16); // 正文
inline const ImVec4 kTextDim  = srgb_vec4(0x6F, 0x6F, 0x6F); // 弱文字
inline const ImVec4 kTextFaint = srgb_vec4(0xA0, 0xA0, 0xA0); // 更弱文字

// ── 派生表面（中性灰阶）─────────────────────────────────
inline const ImVec4 kMenuBg   = srgb_vec4(0xFF, 0xFF, 0xFF); // 菜单栏
inline const ImVec4 kSurface  = srgb_vec4(0xFF, 0xFF, 0xFF); // 面板/弹窗
inline const ImVec4 kSurfaceHover = srgb_vec4(0xF5, 0xF5, 0xF5); // 卡片悬停
inline const ImVec4 kFrame    = srgb_vec4(0xF7, 0xF8, 0xFA); // 输入框
inline const ImVec4 kBorder   = srgb_vec4(0xE5, 0xE7, 0xEB); // 边框

// ── 语义色（语义用途见注释）─────────────────────────────────────────
inline const ImVec4 kGreen    = srgb_vec4(0x24, 0xA1, 0x48); // 成功绿：确认/通过
inline const ImVec4 kBlue     = srgb_vec4(0x0F, 0x62, 0xFE); // 专业蓝：Z轴/信息
inline const ImVec4 kPurple   = srgb_vec4(0xA2, 0x6E, 0xC4); // 控制流紫：特殊状态
inline const ImVec4 kYellow   = srgb_vec4(0xF1, 0xC2, 0x1B); // 明亮黄：测量/十字线
inline const ImVec4 kTeal     = srgb_vec4(0x4E, 0xC9, 0xB0); // 类型青绿：区域选择
inline const ImVec4 kVarBlue  = srgb_vec4(0x8A, 0xB6, 0xFF); // 变量浅蓝：选中点/坐标
inline const ImVec4 kOrange   = srgb_vec4(0xCE, 0x91, 0x78); // 字符串橙：警告/降级
inline const ImVec4 kNumGreen = srgb_vec4(0x24, 0xA1, 0x48); // 数字绿：数值读数
inline const ImVec4 kGray     = srgb_vec4(0xB0, 0xB0, 0xB0); // 运算符灰：标尺/刻度
inline const ImVec4 kRed      = srgb_vec4(0xF4, 0x47, 0x47); // 错误红：错误/X轴
inline const ImVec4 kAccent   = srgb_vec4(0x0F, 0x62, 0xFE); // 专业蓝：交互强调

// ── 3D 视口暗色画布（明暗对比核心）─────────────────────────────────
inline const ImVec4 kViewportBg = srgb_vec4(0x1A, 0x1D, 0x23); // 暗色画布
inline const ImVec4 kViewportBorder = srgb_vec4(0x2A, 0x30, 0x38); // 视口边框

} // namespace palette

// 语义色的 ImU32 便捷取值（draw list 场景），可覆盖 alpha。
[[nodiscard]]
inline ImU32 to_u32(const ImVec4& color, int alpha = 255) {
    ImVec4 c = color;
    c.w = static_cast<float>(alpha) / 255.0f;
    return ImGui::ColorConvertFloat4ToU32(c);
}

// 存储在应用状态里的 sRGB ImU32 颜色（如用户自选的测量线颜色）
// 在绘制前转换到线性空间。alpha 不变。
[[nodiscard]]
inline ImU32 srgb_u32_to_linear(ImU32 srgb) {
    ImVec4 c = ImGui::ColorConvertU32ToFloat4(srgb);
    c.x = srgb_to_linear(c.x);
    c.y = srgb_to_linear(c.y);
    c.z = srgb_to_linear(c.z);
    return ImGui::ColorConvertFloat4ToU32(c);
}

} // namespace gs3d::ui
