#include "gui/ImGuiLayer.hpp"

#include "ui/UiRoot.hpp"

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "imgui.h"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <system_error>

namespace gs3d::gui {

namespace {

gs3d::ui::UiRoot& ui_root_instance()
{
    static gs3d::ui::UiRoot instance;
    return instance;
}

} // namespace

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
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

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

    {
        static const char* kCandidates[] = {
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
            "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            nullptr
        };
        bool loaded = false;
        for (const char** p = kCandidates; *p && !loaded; ++p) {
            if (FILE* f = std::fopen(*p, "rb")) {
                std::fclose(f);
                loaded =
                    io.Fonts->AddFontFromFileTTF(
                        *p,
                        16.0f,
                        nullptr,
                        io.Fonts->GetGlyphRangesChineseFull()
                    ) != nullptr;
            }
        }
        if (!loaded) {
            io.FontGlobalScale = 1.4f;
        }
    }

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 2.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 2.0f;
    style.PopupRounding = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 2.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.FramePadding = ImVec2(7.0f, 4.0f);
    style.ItemSpacing = ImVec2(7.0f, 5.0f);
    style.ItemInnerSpacing = ImVec2(5.0f, 4.0f);
    style.WindowPadding = ImVec2(8.0f, 7.0f);
    style.ScrollbarSize = 12.0f;

    auto& colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.86f, 0.86f, 0.87f, 1.00f);
    colors[ImGuiCol_TextDisabled] =
        ImVec4(0.52f, 0.53f, 0.55f, 1.00f);
    colors[ImGuiCol_WindowBg] =
        ImVec4(0.118f, 0.122f, 0.130f, 1.00f);
    colors[ImGuiCol_ChildBg] =
        ImVec4(0.135f, 0.139f, 0.147f, 1.00f);
    colors[ImGuiCol_PopupBg] =
        ImVec4(0.145f, 0.149f, 0.157f, 0.98f);
    colors[ImGuiCol_Border] =
        ImVec4(0.245f, 0.250f, 0.263f, 1.00f);
    colors[ImGuiCol_FrameBg] =
        ImVec4(0.180f, 0.184f, 0.196f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] =
        ImVec4(0.235f, 0.243f, 0.263f, 1.00f);
    colors[ImGuiCol_FrameBgActive] =
        ImVec4(0.270f, 0.278f, 0.300f, 1.00f);
    colors[ImGuiCol_TitleBg] =
        ImVec4(0.105f, 0.109f, 0.116f, 1.00f);
    colors[ImGuiCol_TitleBgActive] =
        ImVec4(0.145f, 0.149f, 0.157f, 1.00f);
    colors[ImGuiCol_MenuBarBg] =
        ImVec4(0.105f, 0.109f, 0.116f, 1.00f);
    colors[ImGuiCol_Button] =
        ImVec4(0.190f, 0.196f, 0.208f, 1.00f);
    colors[ImGuiCol_ButtonHovered] =
        ImVec4(0.065f, 0.388f, 0.690f, 1.00f);
    colors[ImGuiCol_ButtonActive] =
        ImVec4(0.045f, 0.310f, 0.570f, 1.00f);
    colors[ImGuiCol_Header] =
        ImVec4(0.180f, 0.184f, 0.196f, 1.00f);
    colors[ImGuiCol_HeaderHovered] =
        ImVec4(0.065f, 0.388f, 0.690f, 0.78f);
    colors[ImGuiCol_HeaderActive] =
        ImVec4(0.045f, 0.310f, 0.570f, 1.00f);
    colors[ImGuiCol_Tab] =
        ImVec4(0.145f, 0.149f, 0.157f, 1.00f);
    colors[ImGuiCol_TabHovered] =
        ImVec4(0.065f, 0.388f, 0.690f, 0.85f);
    colors[ImGuiCol_TabSelected] =
        ImVec4(0.205f, 0.212f, 0.227f, 1.00f);
    colors[ImGuiCol_TabSelectedOverline] =
        ImVec4(0.075f, 0.470f, 0.820f, 1.00f);
    colors[ImGuiCol_DockingPreview] =
        ImVec4(0.075f, 0.470f, 0.820f, 0.55f);
    colors[ImGuiCol_DockingEmptyBg] =
        ImVec4(0.095f, 0.099f, 0.106f, 1.00f);
    colors[ImGuiCol_CheckMark] =
        ImVec4(0.075f, 0.470f, 0.820f, 1.00f);
    colors[ImGuiCol_SliderGrab] =
        ImVec4(0.075f, 0.470f, 0.820f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] =
        ImVec4(0.110f, 0.550f, 0.930f, 1.00f);
    colors[ImGuiCol_Separator] =
        ImVec4(0.245f, 0.250f, 0.263f, 1.00f);

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
