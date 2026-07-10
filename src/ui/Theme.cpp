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

// sRGB → linear（alpha 不变），palette:: 与 ImGuiStyle 落地前统一转换。
ImVec4 to_linear(const ImVec4& c)
{
    return ImVec4(
        srgb_to_linear(c.x),
        srgb_to_linear(c.y),
        srgb_to_linear(c.z),
        c.w
    );
}

// ── 方案 A：碳蓝 · Modern SaaS（现有主题，白表面 + IBM Carbon 蓝）────
constexpr ThemeTokens kCarbonBlue{
    .id = "carbon-blue",
    .name = "碳蓝 · Modern SaaS",
    .dark = false,

    .bg = rgb(0xF7F8FA),
    .text = rgb(0x161616),
    .text_dim = rgb(0x6F6F6F),
    .text_faint = rgb(0xA0A0A0),
    .menu_bg = rgb(0xFFFFFF),
    .surface = rgb(0xFFFFFF),
    .surface_hover = rgb(0xF5F5F5),
    .frame = rgb(0xF7F8FA),
    .frame_hover = rgb(0xE8EAED),
    .border = rgb(0xE5E7EB),

    .accent = rgb(0x0F62FE),
    .accent_active = rgb(0x0B4ECB),
    .on_accent = rgb(0xFFFFFF),

    .green = rgb(0x24A148),
    .blue = rgb(0x0F62FE),
    .purple = rgb(0xA26EC4),
    .yellow = rgb(0xF1C21B),
    .teal = rgb(0x4EC9B0),
    .var_blue = rgb(0x8AB6FF),
    .orange = rgb(0xCE9178),
    .gray = rgb(0xB0B0B0),
    .red = rgb(0xF44747),

    .viewport_bg = rgb(0x1A1D23),
    .viewport_border = rgb(0x2A3038),

    .scrollbar_grab = rgb(0xC1C7CD),
    .scrollbar_grab_hovered = rgb(0xA8AEB8),
    .scrollbar_grab_active = rgb(0x8D949E),
    .tab_dimmed = rgb(0xE7E8EA),

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

// ── 方案 B：石墨 · Deep Graphite（暗色 DCC 工作台 + 亮蓝）────────────
constexpr ThemeTokens kDeepGraphite{
    .id = "deep-graphite",
    .name = "石墨 · Deep Graphite",
    .dark = true,

    .bg = rgb(0x14171C),
    .text = rgb(0xE8ECF2),
    .text_dim = rgb(0x8B96A6),
    .text_faint = rgb(0x5E6979),
    .menu_bg = rgb(0x191E25),
    .surface = rgb(0x1B2027),
    .surface_hover = rgb(0x232933),
    .frame = rgb(0x14181E),
    .frame_hover = rgb(0x262D37),
    .border = rgb(0x333C49),

    .accent = rgb(0x4D9FFF),
    .accent_active = rgb(0x3B8BEC),
    .on_accent = rgb(0x0A1526),

    .green = rgb(0x4FC98D),
    .blue = rgb(0x4D9FFF),
    .purple = rgb(0xB48EDC),
    .yellow = rgb(0xF5CB40),
    .teal = rgb(0x4EC9B0),
    .var_blue = rgb(0x8AB6FF),
    .orange = rgb(0xE0A375),
    .gray = rgb(0x707C8C),
    .red = rgb(0xF26D76),

    .viewport_bg = rgb(0x101318),
    .viewport_border = rgb(0x262D37),

    .scrollbar_grab = rgb(0x3A4450),
    .scrollbar_grab_hovered = rgb(0x4A5666),
    .scrollbar_grab_active = rgb(0x5B6A7E),
    .tab_dimmed = rgb(0x191D24),

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

// ── 方案 C：仪器 · Instrument Amber（墨绿灰 + 琥珀，直角仪表感）──────
constexpr ThemeTokens kInstrumentAmber{
    .id = "instrument-amber",
    .name = "仪器 · Instrument Amber",
    .dark = true,

    .bg = rgb(0x151A17),
    .text = rgb(0xE7EDE4),
    .text_dim = rgb(0x8C998A),
    .text_faint = rgb(0x5C6858),
    .menu_bg = rgb(0x10140F),
    .surface = rgb(0x1B211D),
    .surface_hover = rgb(0x232B26),
    .frame = rgb(0x10150F),
    .frame_hover = rgb(0x27302A),
    .border = rgb(0x3A463C),

    .accent = rgb(0xFFB000),
    .accent_active = rgb(0xE09B00),
    .on_accent = rgb(0x201500),

    .green = rgb(0x7AD05B),
    .blue = rgb(0x6FB1FF),
    .purple = rgb(0xB48EDC),
    .yellow = rgb(0xF1C21B),
    .teal = rgb(0x4EC9B0),
    .var_blue = rgb(0x9CC3FF),
    .orange = rgb(0xD89A72),
    .gray = rgb(0x77836F),
    .red = rgb(0xF0655A),

    .viewport_bg = rgb(0x0D110E),
    .viewport_border = rgb(0x29322B),

    .scrollbar_grab = rgb(0x3C4840),
    .scrollbar_grab_hovered = rgb(0x4C5A50),
    .scrollbar_grab_active = rgb(0x5D6E62),
    .tab_dimmed = rgb(0x171C19),

    .button_alpha = 0.25f,
    .button_hover_alpha = 0.50f,

    .window_rounding = 2.0f,
    .child_rounding = 1.0f,
    .frame_rounding = 1.0f,
    .popup_rounding = 2.0f,
    .scrollbar_rounding = 2.0f,
    .grab_rounding = 1.0f,
    .tab_rounding = 2.0f,
};

constexpr const ThemeTokens* kThemes[kThemeCount] = {
    &kCarbonBlue,
    &kDeepGraphite,
    &kInstrumentAmber,
};

ThemeId g_active_theme = ThemeId::kCarbonBlue;

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
    style.DisabledAlpha = 0.42f;
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
    colors[ImGuiCol_Border] = with_alpha(t.border, 0.50f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    // 输入框/下拉底色保持中性，交互态才引入 accent
    colors[ImGuiCol_FrameBg] = with_alpha(t.frame, 0.96f);
    colors[ImGuiCol_FrameBgHovered] = t.frame_hover;
    colors[ImGuiCol_FrameBgActive] = with_alpha(t.accent, 0.20f);

    colors[ImGuiCol_TitleBg] = t.bg;
    colors[ImGuiCol_TitleBgActive] = t.surface;
    colors[ImGuiCol_TitleBgCollapsed] = with_alpha(t.bg, 0.51f);

    colors[ImGuiCol_MenuBarBg] = t.menu_bg;

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
    colors[ImGuiCol_ButtonActive] = t.accent_active;

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

    // 交换链是 B8G8R8A8_SRGB：硬件把 shader 输出当线性值再编码。上面的
    // 颜色按 sRGB 十六进制值书写，必须整体预转换到线性空间，屏幕上才
    // 显示为书写的原值（否则整个 UI 被提亮冲淡）。alpha 不转换。
    for (int i = 0; i < ImGuiCol_COUNT; ++i) {
        colors[i] = to_linear(colors[i]);
    }
}

} // namespace gs3d::ui
