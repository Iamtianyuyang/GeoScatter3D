#include "ui/Theme.hpp"

#include "ui/UiPalette.hpp"

namespace gs3d::ui {

namespace {

// 0xRRGGBB → sRGB ImVec4（不做线性化，线性化统一在 apply_theme 里做）。
constexpr ImVec4 rgb(unsigned hex, float alpha = 1.0f)
{
    return ImVec4(
        static_cast<float>((hex >> 16) & 0xFF) / 255.0f,
        static_cast<float>((hex >> 8) & 0xFF) / 255.0f,
        static_cast<float>(hex & 0xFF) / 255.0f,
        alpha
    );
}

constexpr ImVec4 with_alpha(ImVec4 c, float alpha)
{
    c.w = alpha;
    return c;
}

// 交换链已切换为 UNORM，不再需要 sRGB→linear 预转换。保留此函数
// 作为标识，避免 palette:: 赋值与 ImGuiStyle 覆盖的调用处改动过大。
ImVec4 to_linear(const ImVec4& c)
{
    return c;
}

// ── 方案 A：碳蓝 · 浅色测绘（冷白表面 + 克制蓝）────────────────────
constexpr ThemeTokens kCarbonBlue{
    .id = "carbon-blue",
    .name = "碳蓝 · 浅色测绘",
    .dark = false,

    .bg = rgb(0xF5F7FA),
    .text = rgb(0x121B26),
    .text_dim = rgb(0x43515F),
    .text_faint = rgb(0x6E7D8F),
    .menu_bg = rgb(0xFFFFFF),
    .surface = rgb(0xFBFCFD),
    .surface_hover = rgb(0xF0F4F8),
    .raised = rgb(0xFFFFFF),
    .frame = rgb(0xF4F7FA),
    .frame_hover = rgb(0xE8EEF5),
    .border = rgb(0xB9C6D6),

    .accent = rgb(0x2176D2),
    .accent_active = rgb(0x175EAA),
    .on_accent = rgb(0xFFFFFF),

    .green = rgb(0x24A148),
    .blue = rgb(0x2176D2),
    .purple = rgb(0xA26EC4),
    .yellow = rgb(0xF1C21B),
    .teal = rgb(0x4EC9B0),
    .var_blue = rgb(0x8AB6FF),
    .orange = rgb(0xCE9178),
    .gray = rgb(0xB0B0B0),
    .red = rgb(0xF44747),

    .viewport_bg = rgb(0x1A1D23),
    .viewport_border = rgb(0x35404D),

    .scrollbar_grab = rgb(0xC8CDD3),
    .scrollbar_grab_hovered = rgb(0xB0B5BD),
    .scrollbar_grab_active = rgb(0x989DA6),
    .tab_dimmed = rgb(0xECEDF0),

    .button_alpha = 0.50f,
    .button_hover_alpha = 0.85f,

    .window_rounding = 8.0f,
    .child_rounding = 4.0f,
    .frame_rounding = 4.0f,
    .popup_rounding = 6.0f,
    .scrollbar_rounding = 6.0f,
    .grab_rounding = 4.0f,
    .tab_rounding = 6.0f,
};

// ── 方案 A2：碳蓝 2.0 · 深色测绘（高对比度 + 紧凑间距）──────────────
constexpr ThemeTokens kCarbonBlueDark{
    .id = "carbon-blue-dark",
    .name = "碳蓝 2.0 · 深色测绘",
    .dark = true,

    .bg = rgb(0x0D1117),
    .text = rgb(0xE6EDF3),
    .text_dim = rgb(0x8B949E),
    .text_faint = rgb(0x6E7681),
    .menu_bg = rgb(0x161B22),
    .surface = rgb(0x1C2128),
    .surface_hover = rgb(0x252C35),
    .raised = rgb(0x151A22),
    .frame = rgb(0x0D1117),
    .frame_hover = rgb(0x1F2937),
    .border = rgb(0x30363D),

    .accent = rgb(0x58A6FF),
    .accent_active = rgb(0x388BFD),
    .on_accent = rgb(0x0D1117),

    .green = rgb(0x3FB950),
    .blue = rgb(0x58A6FF),
    .purple = rgb(0xBC8CFF),
    .yellow = rgb(0xD29922),
    .teal = rgb(0x39D353),
    .var_blue = rgb(0x79C0FF),
    .orange = rgb(0xD18616),
    .gray = rgb(0x8B949E),
    .red = rgb(0xF85149),

    .viewport_bg = rgb(0x010409),
    .viewport_border = rgb(0x21262D),

    .scrollbar_grab = rgb(0x30363D),
    .scrollbar_grab_hovered = rgb(0x484F58),
    .scrollbar_grab_active = rgb(0x6E7681),
    .tab_dimmed = rgb(0x161B22),

    .button_alpha = 0.30f,
    .button_hover_alpha = 0.55f,

    .window_rounding = 6.0f,
    .child_rounding = 3.0f,
    .frame_rounding = 3.0f,
    .popup_rounding = 5.0f,
    .scrollbar_rounding = 5.0f,
    .grab_rounding = 3.0f,
    .tab_rounding = 4.0f,
};

// ── 方案 B：石墨 · 深色测绘（蓝灰工作台 + 清晰蓝）──────────────────
constexpr ThemeTokens kDeepGraphite{
    .id = "deep-graphite",
    .name = "石墨 · 深色测绘",
    .dark = true,

    .bg = rgb(0x10151C),
    .text = rgb(0xF1F5FA),
    .text_dim = rgb(0xC2CDDA),
    .text_faint = rgb(0x8E9BAA),
    .menu_bg = rgb(0x151B23),
    .surface = rgb(0x1C2430),
    .surface_hover = rgb(0x273343),
    .raised = rgb(0x18202B),
    .frame = rgb(0x121923),
    .frame_hover = rgb(0x293746),
    .border = rgb(0x5D7086),

    .accent = rgb(0x5BA7F7),
    .accent_active = rgb(0x3487DD),
    .on_accent = rgb(0x0A1526),

    .green = rgb(0x4FC98D),
    .blue = rgb(0x4D9FFF),
    .purple = rgb(0xB48EDC),
    .yellow = rgb(0xF5CB40),
    .teal = rgb(0x4EC9B0),
    .var_blue = rgb(0x8AB6FF),
    .orange = rgb(0xE0A375),
    .gray = rgb(0x8190A1),
    .red = rgb(0xF26D76),

    .viewport_bg = rgb(0x0D1218),
    .viewport_border = rgb(0x374554),

    .scrollbar_grab = rgb(0x4A596A),
    .scrollbar_grab_hovered = rgb(0x5C6E82),
    .scrollbar_grab_active = rgb(0x71859B),
    .tab_dimmed = rgb(0x171E27),

    .button_alpha = 0.26f,
    .button_hover_alpha = 0.48f,

    .window_rounding = 6.0f,
    .child_rounding = 3.0f,
    .frame_rounding = 3.0f,
    .popup_rounding = 5.0f,
    .scrollbar_rounding = 5.0f,
    .grab_rounding = 3.0f,
    .tab_rounding = 4.0f,
};

// ── 方案 C：仪器 · 琥珀测绘（中性深灰 + 低饱和琥珀）──────────────────
constexpr ThemeTokens kInstrumentAmber{
    .id = "instrument-amber",
    .name = "仪器 · 琥珀测绘",
    .dark = true,

    .bg = rgb(0x151914),
    .text = rgb(0xF5F4EC),
    .text_dim = rgb(0xC6C7B9),
    .text_faint = rgb(0x969B8D),
    .menu_bg = rgb(0x191E18),
    .surface = rgb(0x222821),
    .surface_hover = rgb(0x2E382C),
    .raised = rgb(0x1D241D),
    .frame = rgb(0x121712),
    .frame_hover = rgb(0x303B2E),
    .border = rgb(0x596653),

    .accent = rgb(0xE8AF3A),
    .accent_active = rgb(0xCC8D14),
    .on_accent = rgb(0x1C1608),

    .green = rgb(0x7AD05B),
    .blue = rgb(0x6FB1FF),
    .purple = rgb(0xB48EDC),
    .yellow = rgb(0xE8AF3A),
    .teal = rgb(0x4EC9B0),
    .var_blue = rgb(0x9CC3FF),
    .orange = rgb(0xD89A72),
    .gray = rgb(0x87917F),
    .red = rgb(0xF0655A),

    .viewport_bg = rgb(0x0E120E),
    .viewport_border = rgb(0x3A4537),

    .scrollbar_grab = rgb(0x4C5848),
    .scrollbar_grab_hovered = rgb(0x60705A),
    .scrollbar_grab_active = rgb(0x76866E),
    .tab_dimmed = rgb(0x1B211A),

    .button_alpha = 0.28f,
    .button_hover_alpha = 0.48f,

    .window_rounding = 2.0f,
    .child_rounding = 1.0f,
    .frame_rounding = 1.0f,
    .popup_rounding = 2.0f,
    .scrollbar_rounding = 2.0f,
    .grab_rounding = 1.0f,
    .tab_rounding = 2.0f,
};

// ── 高对比 · 浅色测绘（TIA-92）────────────────────
constexpr ThemeTokens kHighContrastLight{
    .id = "high-contrast",
    .name = "高对比 · 浅色测绘",
    .dark = false,
    .bg = rgb(0xFFFFFF),
    .text = rgb(0x0A0F14),
    .text_dim = rgb(0x2E3945),
    .text_faint = rgb(0x5A6A7A),
    .menu_bg = rgb(0xFFFFFF),
    .surface = rgb(0xFFFFFF),
    .surface_hover = rgb(0xE9EEF4),
    .raised = rgb(0xFFFFFF),
    .frame = rgb(0xF2F5F8),
    .frame_hover = rgb(0xDFE6EE),
    .border = rgb(0x7C8CA0),
    .accent = rgb(0x0D5FCC),
    .accent_active = rgb(0x08469A),
    .on_accent = rgb(0xFFFFFF),
    .green = rgb(0x1B7F36),
    .blue = rgb(0x0D5FCC),
    .purple = rgb(0x7D3BB0),
    .yellow = rgb(0xB8860B),
    .teal = rgb(0x14856F),
    .var_blue = rgb(0x2D7FF0),
    .orange = rgb(0xB0561E),
    .gray = rgb(0x6E6E6E),
    .red = rgb(0xC62828),
    .viewport_bg = rgb(0x1A1D23),
    .viewport_border = rgb(0x3A4655),
    .scrollbar_grab = rgb(0x9AA7B8),
    .scrollbar_grab_hovered = rgb(0x7C8CA0),
    .scrollbar_grab_active = rgb(0x5F7086),
    .tab_dimmed = rgb(0xE4E9F0),
    .button_alpha = 0.55f,
    .button_hover_alpha = 0.90f,
    .window_rounding = 4.0f,
    .child_rounding = 2.0f,
    .frame_rounding = 2.0f,
    .popup_rounding = 3.0f,
    .scrollbar_rounding = 3.0f,
    .grab_rounding = 2.0f,
    .tab_rounding = 2.0f,
};

constexpr const ThemeTokens* kThemes[kThemeCount] = {
    &kCarbonBlue,
    &kCarbonBlueDark,
    &kDeepGraphite,
    &kInstrumentAmber,
    &kHighContrastLight,
};

ThemeId g_active_theme = ThemeId::kCarbonBlueDark;

} // namespace

const ThemeTokens& theme_tokens(ThemeId id)
{
    const int index = static_cast<int>(id);
    if (index < 0 || index >= kThemeCount) {
        return kCarbonBlue;
    }
    return *kThemes[index];
}

ThemeId active_theme()
{
    return g_active_theme;
}

ThemeId theme_from_string(std::string_view name, ThemeId fallback)
{
    for (int i = 0; i < kThemeCount; ++i) {
        if (name == kThemes[i]->id) {
            return static_cast<ThemeId>(i);
        }
    }
    return fallback;
}

void set_startup_theme(ThemeId id)
{
    g_active_theme = id;
}

void apply_theme(ThemeId id, float ui_scale)
{
    g_active_theme = id;
    const ThemeTokens& t = theme_tokens(id);

    // ── palette:: 全局语义色（线性空间，供各面板 draw list 使用）────
    palette::kBg = to_linear(t.bg);
    palette::kText = to_linear(t.text);
    palette::kTextDim = to_linear(t.text_dim);
    palette::kTextFaint = to_linear(t.text_faint);
    palette::kMenuBg = to_linear(t.menu_bg);
    palette::kSurface = to_linear(t.surface);
    palette::kSurfaceHover = to_linear(t.surface_hover);
    palette::kRaised = to_linear(t.raised);
    palette::kFrame = to_linear(t.frame);
    palette::kBorder = to_linear(t.border);
    palette::kGreen = to_linear(t.green);
    palette::kBlue = to_linear(t.blue);
    palette::kPurple = to_linear(t.purple);
    palette::kYellow = to_linear(t.yellow);
    palette::kTeal = to_linear(t.teal);
    palette::kVarBlue = to_linear(t.var_blue);
    palette::kOrange = to_linear(t.orange);
    palette::kNumGreen = to_linear(t.green);
    palette::kGray = to_linear(t.gray);
    palette::kRed = to_linear(t.red);
    palette::kAccent = to_linear(t.accent);
    palette::kAccentActive = to_linear(t.accent_active);
    palette::kOnAccent = to_linear(t.on_accent);
    palette::kFrameHover = to_linear(t.frame_hover);
    palette::kViewportBg = to_linear(t.viewport_bg);
    palette::kViewportBorder = to_linear(t.viewport_border);

    // ── ImGuiStyle：圆角按主题基准 × ui_scale ───────────────────────
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = t.window_rounding * ui_scale;
    style.ChildRounding = t.child_rounding * ui_scale;
    style.FrameRounding = t.frame_rounding * ui_scale;
    style.PopupRounding = t.popup_rounding * ui_scale;
    style.ScrollbarRounding = t.scrollbar_rounding * ui_scale;
    style.GrabRounding = t.grab_rounding * ui_scale;
    style.TabRounding = t.tab_rounding * ui_scale;

    // 以内置 Light/Dark 为底：apply 没显式覆盖的冷门条目（TextLink、
    // TreeLines 等）也能拿到一套协调的默认值。
    if (t.dark) {
        ImGui::StyleColorsDark(&style);
    } else {
        ImGui::StyleColorsLight(&style);
    }

    // A compact desktop-tool rhythm with enough target area for repeated
    // property editing. Keep these metrics theme-independent so switching
    // palettes never makes the workspace jump or controls change size.
    style.Alpha = 1.0f;
    // 标题、标签和只读值在项目树与属性面板中大量使用 TextDisabled。
    // 过低的 alpha 会在深色底上看似“消失”，因此保留足够的阅读对比。
    style.DisabledAlpha = 0.70f;
    style.WindowPadding = ImVec2(10.0f * ui_scale, 9.0f * ui_scale);
    style.FramePadding = ImVec2(8.0f * ui_scale, 4.5f * ui_scale);
    style.ItemSpacing = ImVec2(8.0f * ui_scale, 7.0f * ui_scale);
    style.ItemInnerSpacing = ImVec2(6.0f * ui_scale, 4.0f * ui_scale);
    style.CellPadding = ImVec2(6.0f * ui_scale, 5.0f * ui_scale);
    style.TouchExtraPadding = ImVec2(0.0f, 0.0f);
    style.IndentSpacing = 18.0f * ui_scale;
    style.ColumnsMinSpacing = 6.0f * ui_scale;
    style.ScrollbarSize = 11.0f * ui_scale;
    style.GrabMinSize = 18.0f * ui_scale;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;
    style.TabBarBorderSize = 1.0f;
    style.TabBarOverlineSize = 2.0f * ui_scale;
    style.SeparatorTextBorderSize = 1.0f;
    style.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
    style.SeparatorTextPadding = ImVec2(
        7.0f * ui_scale,
        4.0f * ui_scale
    );
    style.DockingSeparatorSize = 2.0f * ui_scale;
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.5f);
    auto& colors = style.Colors;

    colors[ImGuiCol_Text] = t.text;
    colors[ImGuiCol_TextDisabled] = t.text_dim;
    colors[ImGuiCol_WindowBg] = t.bg;
    colors[ImGuiCol_ChildBg] = t.surface;
    colors[ImGuiCol_PopupBg] = t.surface;
    colors[ImGuiCol_Border] = with_alpha(t.border, t.dark ? 0.72f : 0.62f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    // 输入框/下拉底色保持中性，交互态才引入 accent
    colors[ImGuiCol_FrameBg] = with_alpha(t.frame, 0.96f);
    colors[ImGuiCol_FrameBgHovered] = t.frame_hover;
    colors[ImGuiCol_FrameBgActive] = with_alpha(t.accent, 0.20f);

    colors[ImGuiCol_TitleBg] = t.bg;
    colors[ImGuiCol_TitleBgActive] = t.surface;
    colors[ImGuiCol_TitleBgCollapsed] = with_alpha(t.bg, 0.51f);

    colors[ImGuiCol_MenuBarBg] = t.raised;

    // Low-contrast track + clearly stepped thumb states. The narrower track
    // saves panel width while GrabMinSize keeps short documents usable.
    colors[ImGuiCol_ScrollbarBg] = with_alpha(t.border, 0.18f);
    colors[ImGuiCol_ScrollbarGrab] = t.scrollbar_grab;
    colors[ImGuiCol_ScrollbarGrabHovered] = t.scrollbar_grab_hovered;
    colors[ImGuiCol_ScrollbarGrabActive] = t.scrollbar_grab_active;

    colors[ImGuiCol_CheckMark] = t.accent;
    colors[ImGuiCol_SliderGrab] = with_alpha(t.accent, 0.92f);
    colors[ImGuiCol_SliderGrabActive] = t.accent_active;

    colors[ImGuiCol_Button] = with_alpha(t.accent, t.button_alpha);
    colors[ImGuiCol_ButtonHovered] = with_alpha(t.accent, t.button_hover_alpha);
    // 半透明而非实心 accent_active：原生按钮的文字用 ImGuiCol_Text
    // （正文色），实心深蓝/亮琥珀底会和正文色撞色（碳蓝下黑字压深蓝、
    // 琥珀下白字压亮橙）。半透明与底色混合后正文色在三套主题都可读。
    colors[ImGuiCol_ButtonActive] = with_alpha(t.accent, 0.55f);

    colors[ImGuiCol_Header] = with_alpha(t.accent, 0.12f);
    colors[ImGuiCol_HeaderHovered] = with_alpha(t.accent, 0.25f);
    colors[ImGuiCol_HeaderActive] = with_alpha(t.accent, 0.40f);

    colors[ImGuiCol_Separator] = with_alpha(t.border, 0.50f);
    colors[ImGuiCol_SeparatorHovered] = with_alpha(t.accent, 0.78f);
    colors[ImGuiCol_SeparatorActive] = t.accent;

    colors[ImGuiCol_ResizeGrip] = with_alpha(t.accent, 0.20f);
    colors[ImGuiCol_ResizeGripHovered] = with_alpha(t.accent, 0.67f);
    colors[ImGuiCol_ResizeGripActive] = with_alpha(t.accent, 0.95f);

    colors[ImGuiCol_Tab] = t.bg;
    colors[ImGuiCol_TabHovered] = with_alpha(t.accent, 0.15f);
    colors[ImGuiCol_TabSelected] = t.frame;
    colors[ImGuiCol_TabSelectedOverline] = t.accent;
    colors[ImGuiCol_TabDimmed] = t.tab_dimmed;
    colors[ImGuiCol_TabDimmedSelected] = t.surface;

    colors[ImGuiCol_DockingPreview] = with_alpha(t.accent, 0.30f);
    colors[ImGuiCol_DockingEmptyBg] = t.bg;

    colors[ImGuiCol_PlotLines] = t.accent;
    colors[ImGuiCol_PlotLinesHovered] = t.red;
    colors[ImGuiCol_PlotHistogram] = with_alpha(t.accent, 0.70f);
    colors[ImGuiCol_PlotHistogramHovered] = with_alpha(t.red, 0.70f);

    colors[ImGuiCol_TableHeaderBg] = t.surface;
    colors[ImGuiCol_TableBorderStrong] = with_alpha(t.border, 0.60f);
    colors[ImGuiCol_TableBorderLight] = with_alpha(t.border, 0.30f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_TableRowBgAlt] = t.dark
        ? ImVec4(1.0f, 1.0f, 1.0f, 0.03f)
        : ImVec4(0.0f, 0.0f, 0.0f, 0.03f);

    colors[ImGuiCol_TextSelectedBg] = with_alpha(t.accent, 0.20f);
    colors[ImGuiCol_NavHighlight] = with_alpha(t.accent, 0.40f);
    colors[ImGuiCol_DragDropTarget] = with_alpha(t.accent, 0.30f);
    colors[ImGuiCol_NavWindowingHighlight] = with_alpha(t.text, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.15f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.25f);

    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // 交换链使用 UNORM，ThemeTokens 的十六进制色值即为最终显示色；
    // 保留统一入口，供将来切换颜色管线时集中处理。
    for (int i = 0; i < ImGuiCol_COUNT; ++i) {
        colors[i] = to_linear(colors[i]);
    }
}

} // namespace gs3d::ui
