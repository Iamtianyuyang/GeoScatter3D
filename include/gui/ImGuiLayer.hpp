#pragma once

#include "app/AppState.hpp"
#include "app/UiActions.hpp"
#include "render/VulkanContext.hpp"
#include "render/VulkanRenderer.hpp"

#include <vulkan/vulkan.h>

#include <filesystem>
#include <string>

struct GLFWwindow;

namespace gs3d::gui {

class ImGuiLayer {
public:
    ImGuiLayer() = default;
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer&)            = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    // `ini_path` empty = no persistence (default ImGui in-memory layout,
    // not saved/loaded across restarts).
    void init(
        GLFWwindow* window,
        const gs3d::render::VulkanContext&  context,
        const gs3d::render::VulkanRenderer& renderer,
        std::uint32_t min_image_count,
        std::filesystem::path ini_path = {}
    );

    void shutdown();

    [[nodiscard]]
    gs3d::app::UiActions new_frame(gs3d::app::AppState& state);

    void discard_frame();

    void render(VkCommandBuffer command_buffer);

    void render_platform_windows();

private:
    VkDevice device_         = VK_NULL_HANDLE;
    bool     initialized_    = false;
    bool     frame_open_     = false;
    bool     frame_rendered_ = false;

    // ImGui's io.IniFilename stores a raw `const char*` it expects to stay
    // valid for the IO object's lifetime, so the path string must outlive
    // the ImGui context rather than being a temporary.
    std::string ini_path_storage_;
};

} // namespace gs3d::gui
