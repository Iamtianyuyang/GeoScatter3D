#pragma once

#include "app/AppState.hpp"
#include "app/UiActions.hpp"
#include "render/VulkanContext.hpp"
#include "render/VulkanRenderer.hpp"

#include <vulkan/vulkan.h>

struct GLFWwindow;

namespace gs3d::gui {

class ImGuiLayer {
public:
    ImGuiLayer() = default;
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer&)            = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    void init(
        GLFWwindow* window,
        const gs3d::render::VulkanContext&  context,
        const gs3d::render::VulkanRenderer& renderer,
        std::uint32_t min_image_count
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
};

} // namespace gs3d::gui
