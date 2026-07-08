#include "gui/ImGuiLayer.hpp"
#include "gui/UiFonts.hpp"

#include "app/ResourcePath.hpp"
#include "ui/UiRoot.hpp"

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "imgui.h"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>
#include <stdexcept>
#include <system_error>
#include <cmath>

namespace gs3d::gui {

namespace {

UiFonts g_ui_fonts{};

constexpr float kRegularFontSize = 14.0f;
constexpr float kSmallFontSize = 12.0f;
constexpr float kPanelTitleFontSize = 13.0f;
constexpr float kAxisFontSize = 12.0f;
constexpr float kStatusFontSize = 12.0f;

// 1920x1080 on a 23.4" panel is the visual baseline: the UI already looks
// right there, so ui_scale == 1.0 at that PPI. Higher-PPI panels get a
// proportionally larger (but clamped) UI. This is independent of the Vulkan
// render-size chain, which keeps using the GLFW backend's
// io.DisplayFramebufferScale.
constexpr float kBaselineMonitorWidth = 1920.0f;
constexpr float kBaselineMonitorHeight = 1080.0f;
constexpr float kBaselineMonitorDiagonalInches = 23.4f;
constexpr float kMinUiScale = 1.0f;
// Upper clamp on final_ui_scale (= ppi_scale * user_multiplier). 2.5 is safe
// with oversample==1 + ChineseSimplifiedCommon (~2500 glyphs): the largest
// atlas would be ~2500 * (14*2.5)² ≈ 3.1M px ≈ 12 MB RGBA, well within GPU
// texture limits. The previous TexID crash was at oversample==2 with
// ChineseFull (~20k glyphs), a fundamentally different regime.
constexpr float kMaxUiScale = 2.5f;

// ── VSCode Dark+ Syntax Colors (备用点缀色) ──────────────────────────
// Not applied to ImGuiStyle; available for syntax highlighting / custom
// drawing when needed.
static constexpr ImVec4 kSyntaxComment  {0.341f, 0.651f, 0.290f, 1.00f}; // #57A64A  green
static constexpr ImVec4 kSyntaxKeyword  {0.337f, 0.612f, 0.839f, 1.00f}; // #569CD6  blue
static constexpr ImVec4 kSyntaxControl  {0.847f, 0.627f, 0.875f, 1.00f}; // #D8A0DF  purple
static constexpr ImVec4 kSyntaxFunction {0.863f, 0.863f, 0.678f, 1.00f}; // #DCDCAA  yellow
static constexpr ImVec4 kSyntaxType     {0.306f, 0.753f, 0.690f, 1.00f}; // #4EC9B0  cyan
static constexpr ImVec4 kSyntaxVariable {0.612f, 0.804f, 0.937f, 1.00f}; // #9CDCFE  light blue
static constexpr ImVec4 kSyntaxString   {0.808f, 0.573f, 0.486f, 1.00f}; // #CE9178  orange
static constexpr ImVec4 kSyntaxNumber   {0.710f, 0.808f, 0.659f, 1.00f}; // #B5CEA8  light green
static constexpr ImVec4 kSyntaxOperator {0.706f, 0.706f, 0.706f, 1.00f}; // #B4B4B4  gray

// VSCode Dark+ UI tokens (used for ImGuiStyle below).
static constexpr ImVec4 kVscBg         {0.118f, 0.118f, 0.118f, 1.00f}; // #1E1E1E
static constexpr ImVec4 kVscText       {0.855f, 0.855f, 0.855f, 1.00f}; // #DADADA
static constexpr ImVec4 kVscTextDim    {0.604f, 0.604f, 0.604f, 1.00f}; // #9A9A9A
static constexpr ImVec4 kVscAccent     {0.000f, 0.478f, 0.800f, 1.00f}; // #007ACC
static constexpr ImVec4 kVscError      {0.957f, 0.278f, 0.278f, 1.00f}; // #F44747
static constexpr ImVec4 kVscFrameBg    {0.176f, 0.176f, 0.176f, 1.00f}; // #2D2D2D
static constexpr ImVec4 kVscSurface    {0.145f, 0.145f, 0.145f, 1.00f}; // #252525
static constexpr ImVec4 kVscBorder     {0.243f, 0.243f, 0.243f, 1.00f}; // #3E3E3E

// Plausibility guards for glfwGetMonitorPhysicalSize(), which on Linux/X11
// and some virtual/remote displays returns 0x0 or nonsensical values.
constexpr int kMinPlausiblePhysicalSizeMm = 50;   // < 5 cm => bogus
constexpr int kMaxPlausiblePhysicalSizeMm = 2000;  // > 200 cm => bogus
constexpr float kMinPlausiblePpi = 30.0f;   // ~ a huge projection wall
constexpr float kMaxPlausiblePpi = 600.0f;  // ~ beyond phone-class panels

float compute_baseline_ppi()
{
    const float diag_px =
        std::sqrt(kBaselineMonitorWidth * kBaselineMonitorWidth +
                  kBaselineMonitorHeight * kBaselineMonitorHeight);
    return diag_px / kBaselineMonitorDiagonalInches;
}

struct MonitorResolution {
    int width = 0;
    int height = 0;
};

struct MonitorPhysicalSizeMm {
    int width_mm = 0;
    int height_mm = 0;
};

struct MonitorInfo {
    MonitorResolution resolution;
    MonitorPhysicalSizeMm physical_mm;
    GLFWmonitor* monitor = nullptr;
};

MonitorInfo get_window_monitor_info(GLFWwindow* window)
{
    MonitorInfo info{};

    GLFWmonitor* monitor = nullptr;

    // Fullscreen windows report their monitor directly.
    if (window != nullptr) {
        monitor = glfwGetWindowMonitor(window);
    }

    // Windowed mode: locate the monitor that contains the window's center.
    if (monitor == nullptr && window != nullptr) {
        int win_x = 0;
        int win_y = 0;
        int win_w = 0;
        int win_h = 0;
        glfwGetWindowPos(window, &win_x, &win_y);
        glfwGetWindowSize(window, &win_w, &win_h);
        const int center_x = win_x + (win_w > 0 ? win_w / 2 : 0);
        const int center_y = win_y + (win_h > 0 ? win_h / 2 : 0);

        int count = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&count);
        for (int i = 0; i < count && monitor == nullptr; ++i) {
            int mx = 0;
            int my = 0;
            glfwGetMonitorPos(monitors[i], &mx, &my);
            const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
            if (mode == nullptr) {
                continue;
            }
            if (center_x >= mx && center_x < mx + mode->width &&
                center_y >= my && center_y < my + mode->height) {
                monitor = monitors[i];
            }
        }
    }

    // Last resort: primary monitor.
    if (monitor == nullptr) {
        monitor = glfwGetPrimaryMonitor();
    }

    info.monitor = monitor;

    if (monitor != nullptr) {
        if (const GLFWvidmode* mode = glfwGetVideoMode(monitor)) {
            info.resolution.width = mode->width;
            info.resolution.height = mode->height;
        }
        glfwGetMonitorPhysicalSize(
            monitor, &info.physical_mm.width_mm, &info.physical_mm.height_mm);
    }

    return info;
}

float clamp_ui_scale(float scale)
{
    if (scale < kMinUiScale) {
        scale = kMinUiScale;
    }
    if (scale > kMaxUiScale) {
        scale = kMaxUiScale;
    }
    return scale;
}

bool physical_size_is_plausible(const MonitorPhysicalSizeMm& sz)
{
    return sz.width_mm >= kMinPlausiblePhysicalSizeMm &&
           sz.height_mm >= kMinPlausiblePhysicalSizeMm &&
           sz.width_mm <= kMaxPlausiblePhysicalSizeMm &&
           sz.height_mm <= kMaxPlausiblePhysicalSizeMm;
}

struct UiScaleResult {
    float ui_scale = 1.0f;
    bool fallback_used = false;
    float diagonal_inches = 0.0f;
    float ppi = 0.0f;
};

UiScaleResult compute_ui_scale(GLFWwindow* window)
{
    UiScaleResult result{};

    const MonitorInfo info = get_window_monitor_info(window);
    const float baseline_ppi = compute_baseline_ppi();

    const int res_w = info.resolution.width;
    const int res_h = info.resolution.height;

    // Need a valid resolution to compute anything meaningful.
    if (res_w <= 0 || res_h <= 0) {
        result.ui_scale = kMinUiScale;
        result.fallback_used = true;
        return result;
    }

    const float diag_px =
        std::sqrt(static_cast<float>(res_w) * static_cast<float>(res_w) +
                  static_cast<float>(res_h) * static_cast<float>(res_h));

    // Try the PPI-based path: physical size -> diagonal inches -> PPI.
    if (physical_size_is_plausible(info.physical_mm)) {
        const float w_in = static_cast<float>(info.physical_mm.width_mm) / 25.4f;
        const float h_in = static_cast<float>(info.physical_mm.height_mm) / 25.4f;
        const float diag_in = std::sqrt(w_in * w_in + h_in * h_in);
        if (diag_in > 0.0f) {
            const float ppi = diag_px / diag_in;
            result.diagonal_inches = diag_in;
            result.ppi = ppi;
            if (ppi >= kMinPlausiblePpi && ppi <= kMaxPlausiblePpi) {
                result.ui_scale = clamp_ui_scale(ppi / baseline_ppi);
                result.fallback_used = false;
                return result;
            }
            // PPI outside plausibility band — fall through to fallback.
        }
    }

    // Fallback: resolution-only ratio vs the 1080p baseline. Used when the
    // physical size is missing/zero/bogus (common on Linux/X11) or when the
    // derived PPI is implausible. This still scales up on higher-resolution
    // panels, just without accounting for physical size.
    result.fallback_used = true;
    result.ppi = 0.0f;
    result.diagonal_inches = 0.0f;
    const float scale = static_cast<float>(res_h) / kBaselineMonitorHeight;
    result.ui_scale = clamp_ui_scale(scale);
    return result;
}

ImFontConfig make_font_config(float size_pixels)
{
    ImFontConfig cfg;
    cfg.SizePixels = size_pixels;
    // Oversample lowered to 1 to keep the CJK font atlas small: the Vulkan
    // ImGui backend lazily uploads ImTextureData on first render, and an
    // oversized atlas (large fonts * oversample 2 * ~2500 common glyphs) can
    // fail to upload, leaving TexID invalid and asserting in GetTexID().
    cfg.OversampleH = 1;
    cfg.OversampleV = 1;
    cfg.PixelSnapH = true;
    cfg.RasterizerMultiply = 1.0f;
    return cfg;
}

std::vector<std::filesystem::path> bundled_font_candidates()
{
    return {
        "assets/fonts/SourceHanSansSC-Regular.otf",
        "assets/fonts/NotoSansSC-Regular.otf",
        "assets/fonts/NotoSansCJKsc-Regular.otf",
        "../assets/fonts/SourceHanSansSC-Regular.otf",
        "../assets/fonts/NotoSansSC-Regular.otf",
        "../assets/fonts/NotoSansCJKsc-Regular.otf",
        "../../assets/fonts/SourceHanSansSC-Regular.otf",
        "../../assets/fonts/NotoSansSC-Regular.otf",
        "../../assets/fonts/NotoSansCJKsc-Regular.otf",
        "resources/fonts/SourceHanSansSC-Regular.otf",
        "resources/fonts/NotoSansSC-Regular.otf",
        "resources/fonts/NotoSansCJKsc-Regular.otf"
    };
}

std::vector<std::string> system_font_candidates()
{
#if defined(_WIN32)
    const char* windir = std::getenv("WINDIR");
    if (!windir || windir[0] == '\0') {
        windir = std::getenv("SystemRoot");
    }
    std::string base = (windir && windir[0] != '\0')
        ? (std::string(windir) + "\\Fonts\\")
        : "C:\\Windows\\Fonts\\";
    return {
        base + "msyh.ttc",
        base + "msyh.ttf",
        base + "segoeui.ttf",
        base + "simhei.ttf"
    };
#else
    return {
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJKsc-Regular.otf",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/msyh.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/simhei.ttf",
        "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/STHeiti Light.ttc",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf"
    };
#endif
}

bool file_exists(const std::filesystem::path& path)
{
    if (path.empty()) {
        return false;
    }

    std::error_code ec;
    return std::filesystem::exists(path, ec) &&
           std::filesystem::is_regular_file(path, ec);
}

ImFont* load_font(ImGuiIO& io,
                  const char* path,
                  float size_pixels,
                  const ImWchar* glyph_ranges)
{
    ImFontConfig cfg = make_font_config(size_pixels);
    return io.Fonts->AddFontFromFileTTF(path, size_pixels, &cfg, glyph_ranges);
}

ImFont* load_default_font(ImGuiIO& io, float size_pixels)
{
    ImFontConfig cfg = make_font_config(size_pixels);
    return io.Fonts->AddFontDefault(&cfg);
}

std::filesystem::path resolve_bundled_font_path(
    const std::filesystem::path& ini_path)
{
    gs3d::app::ResourcePathContext context;
    context.config_path = ini_path;

    for (const auto& candidate : bundled_font_candidates()) {
        const auto resolved =
            gs3d::app::ResourcePath::resolve_optional_file(candidate, context);
        if (!resolved.empty()) {
            return resolved;
        }
    }

    return {};
}

std::filesystem::path resolve_system_font_path()
{
    for (const auto& candidate : system_font_candidates()) {
        const std::filesystem::path path{candidate};
        if (file_exists(path)) {
            return path;
        }
    }
    return {};
}

std::filesystem::path resolve_font_weight(
    const std::filesystem::path& regular_path,
    const std::vector<std::string>& names
) {
    if (regular_path.empty()) {
        return {};
    }
    for (const auto& name : names) {
        const auto candidate = regular_path.parent_path() / name;
        if (file_exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

UiFonts load_ui_fonts(ImGuiIO& io,
                        const std::filesystem::path& ini_path,
                        float ui_scale)
{
    UiFonts fonts{};
    fonts.ui_scale = ui_scale;
    io.Fonts->Clear();
    const ImWchar* glyph_ranges = io.Fonts->GetGlyphRangesChineseSimplifiedCommon();

    const auto bundled_font_path = resolve_bundled_font_path(ini_path);
    const auto system_font_path = bundled_font_path.empty()
        ? resolve_system_font_path()
        : std::filesystem::path{};
    const auto selected_font_path =
        !bundled_font_path.empty() ? bundled_font_path : system_font_path;

    if (!selected_font_path.empty()) {
        const std::string font_path = selected_font_path.string();
        fonts.regular = load_font(io, font_path.c_str(), kRegularFontSize * ui_scale, glyph_ranges);
        const auto medium_path = resolve_font_weight(
            selected_font_path,
            {
                "SourceHanSansSC-Medium.otf",
                "NotoSansCJKsc-Medium.otf",
                "NotoSansSC-Medium.otf"
            }
        );
        const auto bold_path = resolve_font_weight(
            selected_font_path,
            {
                "SourceHanSansSC-Bold.otf",
                "NotoSansCJKsc-Bold.otf",
                "NotoSansSC-Bold.otf"
            }
        );
        if (!medium_path.empty()) {
            const auto path = medium_path.string();
            fonts.medium = load_font(
                io,
                path.c_str(),
                kRegularFontSize * ui_scale,
                glyph_ranges
            );
        }
        if (!bold_path.empty()) {
            const auto path = bold_path.string();
            fonts.bold = load_font(
                io,
                path.c_str(),
                kRegularFontSize * ui_scale,
                glyph_ranges
            );
        }
        fonts.small = load_font(io, font_path.c_str(), kSmallFontSize * ui_scale, glyph_ranges);
        // Panel titles use medium weight for visual hierarchy.
        if (!medium_path.empty()) {
            fonts.panel_title = load_font(
                io, medium_path.string().c_str(),
                kPanelTitleFontSize * ui_scale, glyph_ranges);
        } else {
            fonts.panel_title = load_font(
                io, font_path.c_str(),
                kPanelTitleFontSize * ui_scale, glyph_ranges);
        }
        fonts.axis = load_font(io, font_path.c_str(), kAxisFontSize * ui_scale, glyph_ranges);
        fonts.status = load_font(io, font_path.c_str(), kStatusFontSize * ui_scale, glyph_ranges);
    }

    if (fonts.regular == nullptr) {
        fonts.regular = load_default_font(io, kRegularFontSize * ui_scale);
        fonts.medium = fonts.regular;
        fonts.bold = fonts.regular;
        fonts.small = load_default_font(io, kSmallFontSize * ui_scale);
        fonts.panel_title = load_default_font(io, kPanelTitleFontSize * ui_scale);
        fonts.axis = load_default_font(io, kAxisFontSize * ui_scale);
        fonts.status = load_default_font(io, kStatusFontSize * ui_scale);
    }

    if (fonts.medium == nullptr) {
        fonts.medium = fonts.regular;
    }
    if (fonts.bold == nullptr) {
        fonts.bold = fonts.medium;
    }
    if (fonts.small == nullptr) {
        fonts.small = fonts.regular;
    }
    if (fonts.panel_title == nullptr) {
        fonts.panel_title = fonts.regular;
    }
    if (fonts.axis == nullptr) {
        fonts.axis = fonts.small != nullptr ? fonts.small : fonts.regular;
    }
    if (fonts.status == nullptr) {
        fonts.status = fonts.axis != nullptr ? fonts.axis : fonts.regular;
    }

    io.FontDefault = fonts.regular;
    io.FontGlobalScale = 1.0f;
    return fonts;
}

gs3d::ui::UiRoot& ui_root_instance()
{
    static gs3d::ui::UiRoot instance;
    return instance;
}

} // namespace

const UiFonts& ui_fonts() noexcept
{
    return g_ui_fonts;
}

ImGuiLayer::~ImGuiLayer()
{
    if (initialized_) {
        shutdown();
    }
}

void ImGuiLayer::init(
    GLFWwindow* window,
    const gs3d::render::VulkanContext& context,
    const gs3d::render::VulkanRenderer& renderer,
    std::uint32_t min_image_count,
    std::filesystem::path ini_path,
    float ui_scale_multiplier,
    bool enable_multi_viewports
) {
    device_ = context.device();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    if (enable_multi_viewports) {
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    }

    if (ini_path.empty()) {
        io.IniFilename = nullptr;
    } else {
        std::error_code ec;
        const auto parent = ini_path.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, ec);
        }
        ini_path_storage_ = ini_path.string();
        io.IniFilename = ini_path_storage_.c_str();
    }

    // ui_scale is derived from the monitor's PPI (resolution / physical
    // size), NOT from glfwGetWindowContentScale. The latter can be misleading
    // on some Linux setups (e.g. reporting a large scale on a 1080p panel)
    // and led to oversized font atlases. When the physical size is missing or
    // bogus (common on Linux/X11), compute_ui_scale falls back to a
    // resolution-only ratio. The user-configurable multiplier then gives a
    // comfort bump on top of the objective PPI baseline. glfw content_scale
    // is only read below for the diagnostic log.
    const UiScaleResult scale_result = compute_ui_scale(window);
    const float ppi_ui_scale = scale_result.ui_scale;
    const float final_ui_scale = clamp_ui_scale(
        ppi_ui_scale * ui_scale_multiplier);
    const float ui_scale = final_ui_scale;

    // Set a minimum window size proportional to ui_scale so the UI can't be
    // shrunk below a usable layout. Computed from the actual space budget:
    //   min_w = left_min(180) + center_min(scales with font for viewport
    //             toolbar ≈ 300*S) + right_min(220, usable with compact mode)
    //             + splitters(~10)
    //   min_h = menu(20*S) + toolbar(28*S) + status(22*S) + viewport(350)
    // Clamped so tiny 1080p panels and huge 4K panels both stay reasonable.
    const int min_win_w = static_cast<int>(std::clamp(
        410.0f + std::max(350.0f, 300.0f * final_ui_scale),
        820.0f, 2200.0f));
    const int min_win_h = static_cast<int>(std::clamp(
        70.0f * final_ui_scale + 350.0f, 500.0f, 1600.0f));
    glfwSetWindowSizeLimits(
        window, min_win_w, min_win_h, GLFW_DONT_CARE, GLFW_DONT_CARE);

    g_ui_fonts = load_ui_fonts(io, ini_path, ui_scale);

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(ui_scale);
    // VSCode Dark+ theme: #1E1E1E background, #DADADA text,
    // #007ACC accent reserved for interactive / selected states.
    style.WindowRounding = 2.0f * ui_scale;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 2.0f * ui_scale;
    style.PopupRounding = 2.0f * ui_scale;
    style.ScrollbarRounding = 2.0f * ui_scale;
    style.GrabRounding = 2.0f * ui_scale;
    style.TabRounding = 1.0f * ui_scale;
    style.WindowBorderSize = 0.8f * ui_scale;
    style.ChildBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.FramePadding = ImVec2(6.0f * ui_scale, 3.0f * ui_scale);
    style.ItemSpacing = ImVec2(6.0f * ui_scale, 5.0f * ui_scale);
    style.ItemInnerSpacing = ImVec2(5.0f * ui_scale, 4.0f * ui_scale);
    style.WindowPadding = ImVec2(10.0f * ui_scale, 8.0f * ui_scale);
    style.CellPadding = ImVec2(6.0f * ui_scale, 4.0f * ui_scale);
    style.ScrollbarSize = 11.0f * ui_scale;
    style.WindowMenuButtonPosition = ImGuiDir_None;

    // ── VSCode Dark+ Theme ───────────────────────────────────────────
    // Rule: FrameBg defaults stay dark gray (#2D2D2D / #353535).
    //       Only interactive states (hover/active/selected) get #007ACC.
    auto& colors = style.Colors;

    colors[ImGuiCol_Text]                  = kVscText;                  // #DADADA
    colors[ImGuiCol_TextDisabled]          = kVscTextDim;               // #9A9A9A
    colors[ImGuiCol_WindowBg]              = kVscBg;                    // #1E1E1E
    colors[ImGuiCol_ChildBg]               = kVscSurface;               // #252525
    colors[ImGuiCol_PopupBg]               = kVscSurface;               // #252525
    colors[ImGuiCol_Border]                = ImVec4(kVscBorder.x, kVscBorder.y, kVscBorder.z, 0.50f);
    colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // Frame: inputs, combos — DARK GRAY, NOT BLUE
    colors[ImGuiCol_FrameBg]               = ImVec4(kVscFrameBg.x, kVscFrameBg.y, kVscFrameBg.z, 0.96f);
    colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.208f, 0.208f, 0.208f, 1.00f); // #353535
    colors[ImGuiCol_FrameBgActive]         = ImVec4(kVscAccent.x, kVscAccent.y, kVscAccent.z, 0.35f);

    // Title bars
    colors[ImGuiCol_TitleBg]               = kVscBg;                    // #1E1E1E
    colors[ImGuiCol_TitleBgActive]         = kVscSurface;               // #252525
    colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(kVscBg.x, kVscBg.y, kVscBg.z, 0.51f);

    // Menu bar
    colors[ImGuiCol_MenuBarBg]             = ImVec4(0.106f, 0.106f, 0.106f, 1.00f); // #1B1B1B

    // Scrollbar
    colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.098f, 0.098f, 0.098f, 0.53f);
    colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.259f, 0.259f, 0.259f, 1.00f); // #424242
    colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.333f, 0.333f, 0.333f, 1.00f); // #555555
    colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.400f, 0.400f, 0.400f, 1.00f); // #666666

    // CheckMark / Slider — ACCENT BLUE
    colors[ImGuiCol_CheckMark]             = kVscAccent;                // #007ACC
    colors[ImGuiCol_SliderGrab]            = ImVec4(kVscAccent.x, kVscAccent.y, kVscAccent.z, 0.92f);
    colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.059f, 0.537f, 0.859f, 1.00f); // #0F89DB

    // Button — ACCENT BLUE (default ~50%, hover/active ramp up)
    colors[ImGuiCol_Button]                = ImVec4(kVscAccent.x, kVscAccent.y, kVscAccent.z, 0.50f);
    colors[ImGuiCol_ButtonHovered]         = ImVec4(kVscAccent.x, kVscAccent.y, kVscAccent.z, 0.85f);
    colors[ImGuiCol_ButtonActive]          = ImVec4(0.000f, 0.400f, 0.700f, 1.00f);

    // Header — ACCENT BLUE
    colors[ImGuiCol_Header]                = ImVec4(kVscAccent.x, kVscAccent.y, kVscAccent.z, 0.31f);
    colors[ImGuiCol_HeaderHovered]         = ImVec4(kVscAccent.x, kVscAccent.y, kVscAccent.z, 0.70f);
    colors[ImGuiCol_HeaderActive]          = ImVec4(kVscAccent.x, kVscAccent.y, kVscAccent.z, 0.90f);

    // Separator
    colors[ImGuiCol_Separator]             = ImVec4(kVscBorder.x, kVscBorder.y, kVscBorder.z, 0.50f);
    colors[ImGuiCol_SeparatorHovered]      = ImVec4(kVscAccent.x, kVscAccent.y, kVscAccent.z, 0.78f);
    colors[ImGuiCol_SeparatorActive]       = kVscAccent;

    // Resize grip — ACCENT BLUE
    colors[ImGuiCol_ResizeGrip]            = ImVec4(kVscAccent.x, kVscAccent.y, kVscAccent.z, 0.20f);
    colors[ImGuiCol_ResizeGripHovered]     = ImVec4(kVscAccent.x, kVscAccent.y, kVscAccent.z, 0.67f);
    colors[ImGuiCol_ResizeGripActive]      = ImVec4(kVscAccent.x, kVscAccent.y, kVscAccent.z, 0.95f);

    // Tabs
    colors[ImGuiCol_Tab]                   = kVscBg;                    // #1E1E1E
    colors[ImGuiCol_TabHovered]            = ImVec4(kVscAccent.x, kVscAccent.y, kVscAccent.z, 0.58f);
    colors[ImGuiCol_TabSelected]           = kVscFrameBg;               // #2D2D2D
    colors[ImGuiCol_TabSelectedOverline]   = kVscAccent;                // #007ACC
    colors[ImGuiCol_TabDimmed]             = ImVec4(0.098f, 0.098f, 0.098f, 1.00f);
    colors[ImGuiCol_TabDimmedSelected]     = kVscSurface;               // #252525

    // Docking
    colors[ImGuiCol_DockingPreview]        = ImVec4(kVscAccent.x, kVscAccent.y, kVscAccent.z, 0.45f);
    colors[ImGuiCol_DockingEmptyBg]        = kVscBg;                    // #1E1E1E

    // Plot — accent + error red as hover hint
    colors[ImGuiCol_PlotLines]             = kVscAccent;
    colors[ImGuiCol_PlotLinesHovered]      = kVscError;                 // #F44747
    colors[ImGuiCol_PlotHistogram]         = ImVec4(kVscAccent.x, kVscAccent.y, kVscAccent.z, 0.70f);
    colors[ImGuiCol_PlotHistogramHovered]  = ImVec4(kVscError.x, kVscError.y, kVscError.z, 0.70f);

    // Table
    colors[ImGuiCol_TableHeaderBg]         = kVscSurface;               // #252525
    colors[ImGuiCol_TableBorderStrong]     = ImVec4(kVscBorder.x, kVscBorder.y, kVscBorder.z, 0.60f);
    colors[ImGuiCol_TableBorderLight]      = ImVec4(kVscBorder.x, kVscBorder.y, kVscBorder.z, 0.30f);
    colors[ImGuiCol_TableRowBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]         = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);

    // Misc
    colors[ImGuiCol_TextSelectedBg]        = ImVec4(kVscAccent.x, kVscAccent.y, kVscAccent.z, 0.35f);
    colors[ImGuiCol_NavHighlight]          = ImVec4(kVscAccent.x, kVscAccent.y, kVscAccent.z, 0.50f);
    colors[ImGuiCol_DragDropTarget]        = ImVec4(kVscAccent.x, kVscAccent.y, kVscAccent.z, 0.40f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(kVscText.x, kVscText.y, kVscText.z, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.00f, 0.00f, 0.00f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.00f, 0.00f, 0.00f, 0.35f);

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    if (!ImGui_ImplGlfw_InitForVulkan(window, true)) {
        ImGui::DestroyContext();
        device_ = VK_NULL_HANDLE;
        throw std::runtime_error("ImGuiLayer: failed to init GLFW backend");
    }

    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.ApiVersion = VK_API_VERSION_1_0;
    init_info.Instance = context.instance();
    init_info.PhysicalDevice = context.physical_device();
    init_info.Device = context.device();
    init_info.QueueFamily = context.queue_family_indices().graphics_family.value();
    init_info.Queue = context.graphics_queue();
    init_info.DescriptorPoolSize = 64;
    init_info.MinImageCount = min_image_count;
    init_info.ImageCount = min_image_count;
    init_info.PipelineInfoMain.RenderPass = renderer.render_pass();
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.MinAllocationSize = 1024 * 1024;

    if (!ImGui_ImplVulkan_Init(&init_info)) {
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        device_ = VK_NULL_HANDLE;
        throw std::runtime_error("ImGuiLayer: failed to init ImGui Vulkan backend");
    }

    // Gather diagnostics. Note: io.DisplayFramebufferScale is the ImGui
    // default (1,1) here because the GLFW backend only updates it during
    // ImGui_ImplGlfw_NewFrame(); we also compute the live framebuffer/window
    // ratio so the real (upcoming) value is visible at startup.
    const MonitorInfo monitor_info = get_window_monitor_info(window);
    int win_w = 0;
    int win_h = 0;
    int fb_w = 0;
    int fb_h = 0;
    glfwGetWindowSize(window, &win_w, &win_h);
    glfwGetFramebufferSize(window, &fb_w, &fb_h);
    float glfw_content_scale = 1.0f;
    glfwGetWindowContentScale(window, &glfw_content_scale, nullptr);
    if (glfw_content_scale <= 0.0f) {
        glfw_content_scale = 1.0f;
    }
    const float fb_ratio_x = (win_w > 0) ? static_cast<float>(fb_w) / static_cast<float>(win_w) : 1.0f;
    const float fb_ratio_y = (win_h > 0) ? static_cast<float>(fb_h) / static_cast<float>(win_h) : 1.0f;
    const float baseline_ppi = compute_baseline_ppi();

    std::fprintf(stderr,
        "[UI] monitor_res=%dx%d  monitor_physical=%dx%dmm  "
        "diagonal=%.2fin  ppi=%.1f  base_ppi=%.1f  fallback=%s  "
        "window=%dx%d  framebuffer=%dx%d  glfw_content_scale=%.2f  "
        "ppi_ui_scale=%.3f  user_multiplier=%.2f  "
        "final_ui_scale=%.3f (clamp %.2f..%.2f)  "
        "regular=%.1f small=%.1f panel=%.1f axis=%.1f status=%.1f  "
        "overlay_scale=font_derived  "
        "oversample=1/1  glyph_range=ChineseSimplifiedCommon  "
        "DisplayFramebufferScale=io(%.2f,%.2f) live_ratio(%.2f,%.2f)  "
        "build_called=false  descriptor_pool_size=%u  "
        "min_window=%dx%d\n",
        monitor_info.resolution.width, monitor_info.resolution.height,
        monitor_info.physical_mm.width_mm, monitor_info.physical_mm.height_mm,
        static_cast<double>(scale_result.diagonal_inches),
        static_cast<double>(scale_result.ppi),
        static_cast<double>(baseline_ppi),
        scale_result.fallback_used ? "yes(res-height)" : "no",
        win_w, win_h,
        fb_w, fb_h,
        static_cast<double>(glfw_content_scale),
        static_cast<double>(ppi_ui_scale),
        static_cast<double>(ui_scale_multiplier),
        static_cast<double>(final_ui_scale),
        static_cast<double>(kMinUiScale),
        static_cast<double>(kMaxUiScale),
        static_cast<double>(kRegularFontSize * final_ui_scale),
        static_cast<double>(kSmallFontSize * final_ui_scale),
        static_cast<double>(kPanelTitleFontSize * final_ui_scale),
        static_cast<double>(kAxisFontSize * final_ui_scale),
        static_cast<double>(kStatusFontSize * final_ui_scale),
        static_cast<double>(io.DisplayFramebufferScale.x),
        static_cast<double>(io.DisplayFramebufferScale.y),
        static_cast<double>(fb_ratio_x),
        static_cast<double>(fb_ratio_y),
        init_info.DescriptorPoolSize,
        min_win_w, min_win_h);

    initialized_ = true;
}

void ImGuiLayer::shutdown()
{
    if (!initialized_) {
        return;
    }

    vkDeviceWaitIdle(device_);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    initialized_ = false;
    frame_open_ = false;
    frame_rendered_ = false;
    device_ = VK_NULL_HANDLE;
}

gs3d::app::UiActions ImGuiLayer::new_frame(gs3d::app::AppState& state)
{
    begin_frame();
    return ui_root_instance().draw(state);
}

void ImGuiLayer::begin_frame()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    frame_open_ = true;
}

void ImGuiLayer::discard_frame()
{
    if (frame_open_) {
        ImGui::EndFrame();
        frame_open_ = false;
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
        }
    }
    frame_rendered_ = false;
}

void ImGuiLayer::render(VkCommandBuffer command_buffer)
{
    ImGui::Render();
    frame_open_ = false;
    frame_rendered_ = true;
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer);
}

void ImGuiLayer::render_platform_windows()
{
    if (!frame_rendered_) {
        return;
    }

    frame_rendered_ = false;
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

} // namespace gs3d::gui
