#include "gui/ImGuiLayer.hpp"
#include "gui/UiFonts.hpp"

#include "app/ResourcePath.hpp"
#include "ui/Theme.hpp"
#include "ui/UiPalette.hpp"
#include "ui/UiRoot.hpp"

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "imgui.h"
#include "imgui_internal.h"

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
    const gs3d::render::VulkanContext&  context,
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
        // Use native OS decorations for detached viewports so moving a
        // torn-out window does not depend on ImGui's self-drawn title-bar
        // hit testing.
        io.ConfigViewportsNoDecoration = false;
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

    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(ui_scale);
    style.WindowBorderSize = 1.0f * ui_scale;
    style.ChildBorderSize = 1.0f * ui_scale;
    style.FrameBorderSize = 1.0f * ui_scale;
    style.FramePadding = ImVec2(8.0f * ui_scale, 5.0f * ui_scale);
    style.ItemSpacing = ImVec2(8.0f * ui_scale, 6.0f * ui_scale);
    style.ItemInnerSpacing = ImVec2(6.0f * ui_scale, 5.0f * ui_scale);
    style.WindowPadding = ImVec2(14.0f * ui_scale, 12.0f * ui_scale);
    style.CellPadding = ImVec2(8.0f * ui_scale, 6.0f * ui_scale);
    style.ScrollbarSize = 11.0f * ui_scale;
    style.WindowMenuButtonPosition = ImGuiDir_None;

    // 主题（颜色 + 圆角）统一由 Theme 模块落地：重写 ImGuiStyle 颜色表、
    // palette:: 语义色，并做 sRGB→linear 预转换（交换链是 B8G8R8A8_SRGB）。
    // 启动主题来自 viewer.toml，运行期可经 视图→主题 菜单随时切换。
    gs3d::ui::apply_theme(gs3d::ui::active_theme(), ui_scale);

    if (!ImGui_ImplGlfw_InitForVulkan(window, true)) {
        ImGui::DestroyContext();
        device_ = VK_NULL_HANDLE;
        throw std::runtime_error("ImGuiLayer: failed to init GLFW backend");
    }

    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.ApiVersion = VK_API_VERSION_1_2;
    init_info.Instance = context.instance();
    init_info.PhysicalDevice = context.physical_device();
    init_info.Device = context.device();
    init_info.QueueFamily = context.queue_family_indices().graphics_family.value();
    init_info.Queue = context.graphics_queue();
    init_info.DescriptorPoolSize = 64;
    init_info.MinImageCount = min_image_count;
    init_info.ImageCount = min_image_count;
    init_info.MinAllocationSize = 1024 * 1024;

    // Dynamic rendering：主窗口与副窗口共用同一格式声明。副窗口的交换链
    // 会按 PipelineInfoForViewports 请求的格式优先选择，从而继承主交换链
    // 的 sRGB 格式——否则后端默认只请求 UNORM，副窗口整体偏暗。
    //
    // 副视口管线创建时会将 colorAttachmentCount 重写为 1（见 ImGui
    // Vulkan 后端 CreateWindow），因此这里可以安全地声明多个格式请求，
    // 后端仅用它们做 Surface 格式优选，不影响实际管线。
    color_attachment_format_ = renderer.swapchain_image_format();

    static const VkFormat kViewportFormatRequests[] = {
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8A8_UNORM,  // dup: 推力大于单个条目，确保选中 UNORM
    };

    VkPipelineRenderingCreateInfoKHR rendering_create_info{};
    rendering_create_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    rendering_create_info.colorAttachmentCount = 1;
    rendering_create_info.pColorAttachmentFormats = &color_attachment_format_;

    VkPipelineRenderingCreateInfoKHR viewport_rendering_create_info{};
    viewport_rendering_create_info.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    viewport_rendering_create_info.colorAttachmentCount =
        static_cast<uint32_t>(sizeof(kViewportFormatRequests) / sizeof(kViewportFormatRequests[0]));
    viewport_rendering_create_info.pColorAttachmentFormats =
        kViewportFormatRequests;

    init_info.UseDynamicRendering = true;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo =
        rendering_create_info;
    init_info.PipelineInfoForViewports.PipelineRenderingCreateInfo =
        viewport_rendering_create_info;

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
    // TODO(debug): 临时诊断多视口点击路由，定位副窗口输入失效
    {
        ImGuiContext& g = *ImGui::GetCurrentContext();
        if (g.IO.MouseClicked[0] || g.IO.MouseReleased[0]) {
            std::fprintf(stderr,
                "[VPDBG] %s pos=(%.0f,%.0f) hovered_vp=%08X mouse_vp=%08X "
                "hovered_win=%s moving_win=%s nav_win=%s\n",
                g.IO.MouseClicked[0] ? "CLICK" : "RELEASE",
                g.IO.MousePos.x, g.IO.MousePos.y,
                g.IO.MouseHoveredViewport,
                g.MouseViewport ? g.MouseViewport->ID : 0,
                g.HoveredWindow ? g.HoveredWindow->Name : "<none>",
                g.MovingWindow ? g.MovingWindow->Name : "<none>",
                g.NavWindow ? g.NavWindow->Name : "<none>");
        }
    }
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
