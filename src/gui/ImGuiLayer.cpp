#include "gui/ImGuiLayer.hpp"
#include "gui/UiFonts.hpp"

#include "app/ResourcePath.hpp"
#include "ui/UiRoot.hpp"

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "imgui.h"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <filesystem>
#include <vector>
#include <stdexcept>
#include <system_error>

namespace gs3d::gui {

namespace {

UiFonts g_ui_fonts{};

constexpr float kRegularFontSize = 14.0f;
constexpr float kSmallFontSize = 12.0f;
constexpr float kPanelTitleFontSize = 13.0f;
constexpr float kAxisFontSize = 12.0f;
constexpr float kStatusFontSize = 12.0f;

ImFontConfig make_font_config(float size_pixels)
{
    ImFontConfig cfg;
    cfg.SizePixels = size_pixels;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    cfg.PixelSnapH = true;
    cfg.RasterizerMultiply = 1.0f;
    return cfg;
}

std::vector<std::filesystem::path> bundled_font_candidates()
{
    return {
        "assets/fonts/NotoSansSC-Regular.otf",
        "assets/fonts/NotoSansCJKsc-Regular.otf",
        "../assets/fonts/NotoSansSC-Regular.otf",
        "../assets/fonts/NotoSansCJKsc-Regular.otf",
        "../../assets/fonts/NotoSansSC-Regular.otf",
        "../../assets/fonts/NotoSansCJKsc-Regular.otf",
        "resources/fonts/NotoSansSC-Regular.otf",
        "resources/fonts/NotoSansCJKsc-Regular.otf"
    };
}

const char* const* system_font_candidates()
{
    static const char* kCandidates[] = {
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
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        nullptr
    };
    return kCandidates;
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
    for (const char* const* p = system_font_candidates(); *p != nullptr; ++p) {
        const std::filesystem::path candidate{*p};
        if (file_exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

UiFonts load_ui_fonts(ImGuiIO& io, const std::filesystem::path& ini_path)
{
    UiFonts fonts{};
    io.Fonts->Clear();
    const ImWchar* glyph_ranges = io.Fonts->GetGlyphRangesChineseFull();

    const auto bundled_font_path = resolve_bundled_font_path(ini_path);
    const auto system_font_path = bundled_font_path.empty()
        ? resolve_system_font_path()
        : std::filesystem::path{};
    const auto selected_font_path =
        !bundled_font_path.empty() ? bundled_font_path : system_font_path;

    if (!selected_font_path.empty()) {
        const std::string font_path = selected_font_path.string();
        fonts.regular = load_font(io, font_path.c_str(), kRegularFontSize, glyph_ranges);
        fonts.small = load_font(io, font_path.c_str(), kSmallFontSize, glyph_ranges);
        fonts.panel_title = load_font(
            io, font_path.c_str(), kPanelTitleFontSize, glyph_ranges);
        fonts.axis = load_font(io, font_path.c_str(), kAxisFontSize, glyph_ranges);
        fonts.status = load_font(io, font_path.c_str(), kStatusFontSize, glyph_ranges);
    }

    if (fonts.regular == nullptr) {
        fonts.regular = load_default_font(io, kRegularFontSize);
        fonts.small = load_default_font(io, kSmallFontSize);
        fonts.panel_title = load_default_font(io, kPanelTitleFontSize);
        fonts.axis = load_default_font(io, kAxisFontSize);
        fonts.status = load_default_font(io, kStatusFontSize);
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
    std::filesystem::path ini_path
) {
    device_ = context.device();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // ImGuiConfigFlags_ViewportsEnable is intentionally not set:
    // offscreen-framebuffer textures cannot be shared across independent
    // OS windows (platform viewports) without per-viewport render targets.
    // Disabling viewports keeps all 3D viewport windows docked inside the
    // main window, where the single offscreen texture works correctly.

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

    g_ui_fonts = load_ui_fonts(io, ini_path);

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 3.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 3.0f;
    style.ScrollbarRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 3.0f;
    style.WindowBorderSize = 0.8f;
    style.ChildBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.FramePadding = ImVec2(6.0f, 3.0f);
    style.ItemSpacing = ImVec2(6.0f, 5.0f);
    style.ItemInnerSpacing = ImVec2(5.0f, 4.0f);
    style.WindowPadding = ImVec2(10.0f, 8.0f);
    style.CellPadding = ImVec2(6.0f, 4.0f);
    style.ScrollbarSize = 11.0f;
    style.WindowMenuButtonPosition = ImGuiDir_None;

    auto& colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.84f, 0.85f, 0.87f, 1.00f);
    colors[ImGuiCol_TextDisabled] =
        ImVec4(0.52f, 0.55f, 0.59f, 0.90f);
    colors[ImGuiCol_WindowBg] =
        ImVec4(0.104f, 0.109f, 0.118f, 1.00f);
    colors[ImGuiCol_ChildBg] =
        ImVec4(0.125f, 0.130f, 0.140f, 1.00f);
    colors[ImGuiCol_PopupBg] =
        ImVec4(0.110f, 0.116f, 0.126f, 0.98f);
    colors[ImGuiCol_Border] =
        ImVec4(0.300f, 0.325f, 0.360f, 0.26f);
    colors[ImGuiCol_FrameBg] =
        ImVec4(0.160f, 0.168f, 0.180f, 0.96f);
    colors[ImGuiCol_FrameBgHovered] =
        ImVec4(0.205f, 0.216f, 0.235f, 1.00f);
    colors[ImGuiCol_FrameBgActive] =
        ImVec4(0.230f, 0.242f, 0.264f, 1.00f);
    colors[ImGuiCol_TitleBg] =
        ImVec4(0.098f, 0.103f, 0.111f, 1.00f);
    colors[ImGuiCol_TitleBgActive] =
        ImVec4(0.108f, 0.114f, 0.124f, 1.00f);
    colors[ImGuiCol_MenuBarBg] =
        ImVec4(0.095f, 0.100f, 0.108f, 1.00f);
    colors[ImGuiCol_Button] =
        ImVec4(0.168f, 0.175f, 0.187f, 0.92f);
    colors[ImGuiCol_ButtonHovered] =
        ImVec4(0.110f, 0.330f, 0.560f, 0.92f);
    colors[ImGuiCol_ButtonActive] =
        ImVec4(0.095f, 0.290f, 0.500f, 1.00f);
    colors[ImGuiCol_Header] =
        ImVec4(0.170f, 0.178f, 0.192f, 0.72f);
    colors[ImGuiCol_HeaderHovered] =
        ImVec4(0.105f, 0.315f, 0.540f, 0.68f);
    colors[ImGuiCol_HeaderActive] =
        ImVec4(0.090f, 0.280f, 0.490f, 0.90f);
    colors[ImGuiCol_Tab] =
        ImVec4(0.118f, 0.123f, 0.134f, 1.00f);
    colors[ImGuiCol_TabHovered] =
        ImVec4(0.118f, 0.330f, 0.560f, 0.80f);
    colors[ImGuiCol_TabSelected] =
        ImVec4(0.160f, 0.168f, 0.184f, 1.00f);
    colors[ImGuiCol_TabSelectedOverline] =
        ImVec4(0.120f, 0.420f, 0.760f, 0.88f);
    colors[ImGuiCol_DockingPreview] =
        ImVec4(0.115f, 0.400f, 0.710f, 0.45f);
    colors[ImGuiCol_DockingEmptyBg] =
        ImVec4(0.088f, 0.093f, 0.101f, 1.00f);
    colors[ImGuiCol_CheckMark] =
        ImVec4(0.120f, 0.420f, 0.760f, 0.96f);
    colors[ImGuiCol_SliderGrab] =
        ImVec4(0.120f, 0.420f, 0.760f, 0.92f);
    colors[ImGuiCol_SliderGrabActive] =
        ImVec4(0.160f, 0.500f, 0.860f, 1.00f);
    colors[ImGuiCol_Separator] =
        ImVec4(0.300f, 0.325f, 0.360f, 0.22f);

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
    init_info.DescriptorPoolSize = 8;
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
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    frame_open_ = true;

    return ui_root_instance().draw(state);
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
