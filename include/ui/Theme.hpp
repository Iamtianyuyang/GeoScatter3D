#pragma once

#include "imgui.h"

#include <string_view>

namespace gs3d::ui {

/*
 * 四套可切换 UI 主题（TIA-92 新增高对比）。
 * 语义层级：bg < surface < raised < border；text > text_dim > text_faint。
 * raised 用于顶栏/状态栏等抬升表面。
 */

enum class ThemeId : int {
    kCarbonBlue = 0,
    kCarbonBlueDark = 1,
    kDeepGraphite = 2,
    kInstrumentAmber = 3,
    kHighContrastLight = 4,
};

inline constexpr int kThemeCount = 5;

struct ThemeTokens {
    const char* id;   // 配置文件里的标识，如 "carbon-blue"
    const char* name; // 菜单显示名，如 "碳蓝 · Modern SaaS"
    bool dark;        // 以 StyleColorsDark 还是 StyleColorsLight 为底

    // ── 基础表面 / 文字（sRGB）────────────────────────────────
    ImVec4 bg;            // 窗口背景
    ImVec4 text;          // 正文
    ImVec4 text_dim;      // 弱文字
    ImVec4 text_faint;    // 更弱文字
    ImVec4 menu_bg;       // 菜单栏/顶栏
    ImVec4 surface;       // 面板/弹窗
    ImVec4 surface_hover; // 卡片悬停
    ImVec4 raised;        // 抬升表面：顶栏/状态栏
    ImVec4 frame;         // 输入框底
    ImVec4 frame_hover;   // 输入框悬停
    ImVec4 border;        // 边框

    // ── 强调色 ───────────────────────────────────────────────
    ImVec4 accent;        // 交互强调
    ImVec4 accent_active; // 按下/拖动态（更深一档）
    ImVec4 on_accent;     // accent 底上的文字/对勾

    // ── 语义点缀色（palette:: 语义见 UiPalette.hpp）──────────
    ImVec4 green;
    ImVec4 blue;
    ImVec4 purple;
    ImVec4 yellow;
    ImVec4 teal;
    ImVec4 var_blue;
    ImVec4 orange;
    ImVec4 gray;
    ImVec4 red;

    // ── 3D 视口 ─────────────────────────────────────────────
    ImVec4 viewport_bg;
    ImVec4 viewport_border;

    // ── 滚动条 / 标签杂项 ───────────────────────────────────
    ImVec4 scrollbar_grab;
    ImVec4 scrollbar_grab_hovered;
    ImVec4 scrollbar_grab_active;
    ImVec4 tab_dimmed;

    // ── 按钮透明度阶梯（accent 上叠 alpha）───────────────────
    float button_alpha;
    float button_hover_alpha;

    // ── 圆角（未乘 ui_scale 的基准像素）──────────────────────
    float window_rounding;
    float child_rounding;
    float frame_rounding;
    float popup_rounding;
    float scrollbar_rounding;
    float grab_rounding;
    float tab_rounding;
};

[[nodiscard]] const ThemeTokens& theme_tokens(ThemeId id);

[[nodiscard]] ThemeId active_theme();

// 按配置字符串（ThemeTokens::id）解析主题；未知值返回 fallback。
[[nodiscard]] ThemeId theme_from_string(
    std::string_view name,
    ThemeId fallback = ThemeId::kCarbonBlue
);

// 仅记录启动主题，不触碰 ImGui（可在 CreateContext 之前调用）。
void set_startup_theme(ThemeId id);

// 应用主题：重写 palette:: 全局色 + ImGuiStyle 颜色/圆角。需要 ImGui 上下文。
void apply_theme(ThemeId id, float ui_scale);

} // namespace gs3d::ui
