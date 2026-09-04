#include "ui/color/Theme.hpp"

#include "ui/layouts/LayoutMetrics.hpp"
#include "ui/color/ThemeRegistry.hpp"
#include "ui/color/UiPalette.hpp"

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
// 作为标识，避免 palette:: 赋值的调用处改动过大。
ImVec4 to_linear(const ImVec4& c)
{
    return c;
}

// ── 方案 A：极地冷白 (Arctic Light) · 浅色（冷白表面 + 克制蓝）────────────────────
constexpr ThemeTokens kCarbonBlue{
    .id = "carbon-blue",
    .alias_id = "arctic-light",
    .name = "极地冷白 (Arctic Light)",
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
};

// ── 方案 A2：深空暗夜 (Midnight Dark) · 深色（纯粹深邃暗夜基底）──────────────
constexpr ThemeTokens kCarbonBlueDark{
    .id = "carbon-blue-dark",
    .alias_id = "midnight-dark",
    .name = "深空暗夜 (Midnight Dark)",
    .dark = true,

    .bg = rgb(0x0B0D13),
    .text = rgb(0xF8FAFC),
    .text_dim = rgb(0x94A3B8),
    .text_faint = rgb(0x64748B),
    .menu_bg = rgb(0x10131B),
    .surface = rgb(0x151924),
    .surface_hover = rgb(0x1F2536),
    .raised = rgb(0x121621),
    .frame = rgb(0x0E1119),
    .frame_hover = rgb(0x1C2232),
    .border = rgb(0x242C3D),

    .accent = rgb(0x3B82F6),
    .accent_active = rgb(0x2563EB),
    .on_accent = rgb(0xFFFFFF),

    .green = rgb(0x10B981),
    .blue = rgb(0x3B82F6),
    .purple = rgb(0x8B5CF6),
    .yellow = rgb(0xF59E0B),
    .teal = rgb(0x14B8A6),
    .var_blue = rgb(0x60A5FA),
    .orange = rgb(0xF97316),
    .gray = rgb(0x64748B),
    .red = rgb(0xEF4444),

    .viewport_bg = rgb(0x06070A),
    .viewport_border = rgb(0x1D2330),

    .scrollbar_grab = rgb(0x293144),
    .scrollbar_grab_hovered = rgb(0x3B4660),
    .scrollbar_grab_active = rgb(0x4E5C7D),
    .tab_dimmed = rgb(0x10131C),

    .button_alpha = 0.22f,
    .button_hover_alpha = 0.40f,
};

// ── 方案 B：蓝灰石墨 (Slate Graphite) · 深色（蓝灰现代工作台 + 电光天蓝）──────────────
constexpr ThemeTokens kDeepGraphite{
    .id = "deep-graphite",
    .alias_id = "slate-graphite",
    .name = "蓝灰石墨 (Slate Graphite)",
    .dark = true,

    .bg = rgb(0x0F1319),
    .text = rgb(0xF1F5F9),
    .text_dim = rgb(0x94A3B8),
    .text_faint = rgb(0x64748B),
    .menu_bg = rgb(0x151B24),
    .surface = rgb(0x19202C),
    .surface_hover = rgb(0x232D3E),
    .raised = rgb(0x171E29),
    .frame = rgb(0x121720),
    .frame_hover = rgb(0x212B3B),
    .border = rgb(0x2D3A4D),

    .accent = rgb(0x38BDF8),
    .accent_active = rgb(0x0284C7),
    .on_accent = rgb(0x0F172A),

    .green = rgb(0x10B981),
    .blue = rgb(0x38BDF8),
    .purple = rgb(0xA78BFA),
    .yellow = rgb(0xFBBF24),
    .teal = rgb(0x2DD4BF),
    .var_blue = rgb(0x7DD3FC),
    .orange = rgb(0xFB923C),
    .gray = rgb(0x64748B),
    .red = rgb(0xF87171),

    .viewport_bg = rgb(0x0A0D12),
    .viewport_border = rgb(0x232C3A),

    .scrollbar_grab = rgb(0x334155),
    .scrollbar_grab_hovered = rgb(0x475569),
    .scrollbar_grab_active = rgb(0x64748B),
    .tab_dimmed = rgb(0x131822),

    .button_alpha = 0.20f,
    .button_hover_alpha = 0.38f,
};

// ── 方案 C：雷达琥珀 (Radar Amber / 曜石金) · 深色（纯粹曜石黑 + 电镀钛金琥珀）──────────────────
constexpr ThemeTokens kInstrumentAmber{
    .id = "instrument-amber",
    .alias_id = "radar-amber",
    .name = "雷达琥珀 (Radar Amber)",
    .dark = true,

    .bg = rgb(0x111317),
    .text = rgb(0xF4F4F6),
    .text_dim = rgb(0x9CA3AF),
    .text_faint = rgb(0x6B7280),
    .menu_bg = rgb(0x16181E),
    .surface = rgb(0x1A1C23),
    .surface_hover = rgb(0x242833),
    .raised = rgb(0x181A21),
    .frame = rgb(0x13141A),
    .frame_hover = rgb(0x222631),
    .border = rgb(0x2F3443),

    .accent = rgb(0xF59E0B),
    .accent_active = rgb(0xD97706),
    .on_accent = rgb(0x000000),

    .green = rgb(0x10B981),
    .blue = rgb(0x3B82F6),
    .purple = rgb(0x8B5CF6),
    .yellow = rgb(0xF59E0B),
    .teal = rgb(0x14B8A6),
    .var_blue = rgb(0x60A5FA),
    .orange = rgb(0xF97316),
    .gray = rgb(0x6B7280),
    .red = rgb(0xEF4444),

    .viewport_bg = rgb(0x0B0C0F),
    .viewport_border = rgb(0x262A36),

    .scrollbar_grab = rgb(0x374151),
    .scrollbar_grab_hovered = rgb(0x4B5563),
    .scrollbar_grab_active = rgb(0x6B7280),
    .tab_dimmed = rgb(0x14161D),

    .button_alpha = 0.20f,
    .button_hover_alpha = 0.35f,
};

// ── 强光对比 (High Contrast) · 浅色（高对比无障碍与强光）────────────────────
constexpr ThemeTokens kHighContrastLight{
    .id = "high-contrast",
    .alias_id = "contrast-light",
    .name = "强光对比 (High Contrast)",
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
    if (index >= 0 && index < kThemeCount) {
        return *kThemes[index];
    }
    const auto* t = ThemeRegistry::instance().find_theme(id);
    if (t != nullptr) {
        return *t;
    }
    return kCarbonBlue;
}

ThemeId active_theme()
{
    return g_active_theme;
}

ThemeId theme_from_string(std::string_view name, ThemeId fallback)
{
    for (int i = 0; i < kThemeCount; ++i) {
        if (name == kThemes[i]->id ||
            (kThemes[i]->alias_id != nullptr && name == kThemes[i]->alias_id)) {
            return static_cast<ThemeId>(i);
        }
    }
    if (name == "light") return ThemeId::kArcticLight;
    if (name == "dark") return ThemeId::kMidnightDark;
    if (name == "graphite") return ThemeId::kSlateGraphite;
    if (name == "amber") return ThemeId::kRadarAmber;
    if (name == "contrast") return ThemeId::kHighContrast;

    const auto* t = ThemeRegistry::instance().find_theme(name);
    if (t != nullptr) {
        return ThemeId::kCustom;
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

    apply_layout_geometry(default_layout_geometry(), ui_scale);
}

void apply_theme(std::string_view id, float ui_scale)
{
    const auto* t = ThemeRegistry::instance().find_theme(id);
    if (t != nullptr) {
        ThemeId tid = theme_from_string(id, ThemeId::kCustom);
        apply_theme(tid, ui_scale);
    }
}

} // namespace gs3d::ui
