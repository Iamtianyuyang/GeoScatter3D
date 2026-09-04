#pragma once

#include "imgui.h"

#include <string_view>

namespace gs3d::ui {

/*
 * 可切换 UI 颜色主题（Theme / Color）。
 * 语义层级：bg < surface < raised < border；text > text_dim > text_faint。
 * 与布局（Layout）正交分离：主题仅定义颜色，不干预布局几何与圆角。
 */

enum class ThemeId : int {
    kCarbonBlue = 0,
    kCarbonBlueDark = 1,
    kDeepGraphite = 2,
    kInstrumentAmber = 3,
    kHighContrastLight = 4,
    kCustom = 100,

    // 现代命名别名
    kArcticLight = 0,
    kMidnightDark = 1,
    kSlateGraphite = 2,
    kRadarAmber = 3,
    kHighContrast = 4,
};

inline constexpr int kThemeCount = 5;

/*
 * 纯颜色标记集合（Pure Color Tokens）。
 * 仅包含颜色、对比度与透明度阶梯，圆角等几何参数收敛至 LayoutMetrics。
 */
struct ThemeTokens {
    const char* id = "";         // 基础标识，如 "carbon-blue"
    const char* alias_id = "";   // 现代别名，如 "arctic-light"
    const char* name = "";       // 菜单显示名，如 "极地冷白 (Arctic Light)"
    bool dark = false;           // 以 StyleColorsDark 还是 StyleColorsLight 为底

    // ── 基础表面 / 文字（sRGB）────────────────────────────────
    ImVec4 bg{};            // 窗口背景
    ImVec4 text{};          // 正文
    ImVec4 text_dim{};      // 弱文字
    ImVec4 text_faint{};    // 更弱文字
    ImVec4 menu_bg{};       // 菜单栏/顶栏
    ImVec4 surface{};       // 面板/弹窗
    ImVec4 surface_hover{}; // 卡片悬停
    ImVec4 raised{};        // 抬升表面：顶栏/状态栏
    ImVec4 frame{};         // 输入框底
    ImVec4 frame_hover{};   // 输入框悬停
    ImVec4 border{};        // 边框

    // ── 强调色 ───────────────────────────────────────────────
    ImVec4 accent{};        // 交互强调
    ImVec4 accent_active{}; // 按下/拖动态（更深一档）
    ImVec4 on_accent{};     // accent 底上的文字/对勾

    // ── 语义点缀色（palette:: 语义见 UiPalette.hpp）──────────
    ImVec4 green{};
    ImVec4 blue{};
    ImVec4 purple{};
    ImVec4 yellow{};
    ImVec4 teal{};
    ImVec4 var_blue{};
    ImVec4 orange{};
    ImVec4 gray{};
    ImVec4 red{};

    // ── 3D 视口 ─────────────────────────────────────────────
    ImVec4 viewport_bg{};
    ImVec4 viewport_border{};

    // ── 滚动条 / 标签杂项 ───────────────────────────────────
    ImVec4 scrollbar_grab{};
    ImVec4 scrollbar_grab_hovered{};
    ImVec4 scrollbar_grab_active{};
    ImVec4 tab_dimmed{};

    // ── 按钮透明度阶梯（accent 上叠 alpha）───────────────────
    float button_alpha = 0.30f;
    float button_hover_alpha = 0.55f;
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

// 应用主题：重写 palette:: 全局色 + ImGuiStyle 纯颜色。需要 ImGui 上下文。
void apply_theme(ThemeId id, float ui_scale);
void apply_theme(std::string_view id, float ui_scale);

// 注册新颜色主题
bool register_theme(const ThemeTokens& tokens);

} // namespace gs3d::ui
