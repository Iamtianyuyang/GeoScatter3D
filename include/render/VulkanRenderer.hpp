#pragma once

#include "platform/Window.hpp"
#include "render/VulkanContext.hpp"
#include "render/VulkanSwapchain.hpp"
#include "render/VulkanDepthBuffer.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace gs3d::render {

struct ClearColor {
    float r = 0.02f;
    float g = 0.02f;
    float b = 0.025f;
    float a = 1.0f;
};

class VulkanRenderer {
public:
    using DrawCallback = std::function<void(VkCommandBuffer command_buffer)>;

public:
    VulkanRenderer(
        const VulkanContext& context,
        VulkanSwapchain& swapchain
    );

    ~VulkanRenderer();

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    VulkanRenderer(VulkanRenderer&&) = delete;
    VulkanRenderer& operator=(VulkanRenderer&&) = delete;

    void draw_frame(gs3d::platform::Window& window);

    void draw_frame(
        gs3d::platform::Window& window,
        const DrawCallback& draw_callback
    );

    void set_clear_color(const ClearColor& color) noexcept;

    /*
     * 等待所有 in-flight 帧的 fence（替代 vkDeviceWaitIdle）。
     * 只等当前正在飞行的渲染帧，不等无关的 GPU 队列。
     * 在销毁/替换被 GPU 使用的 buffer 前调用。
     */
    void wait_for_in_flight_fences();

    [[nodiscard]]
    VkRenderPass render_pass() const noexcept;

    [[nodiscard]]
    VkCommandPool command_pool() const noexcept;

    [[nodiscard]]
    VkExtent2D extent() const noexcept;

private:
    static constexpr std::uint32_t MAX_FRAMES_IN_FLIGHT = 2;

private:
    const VulkanContext& context_;
    VulkanSwapchain& swapchain_;

    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VulkanDepthBuffer depth_buffer_{};
    std::vector<VkFramebuffer> framebuffers_;

    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> command_buffers_;

    std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> image_available_semaphores_{};
    std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> render_finished_semaphores_{};
    std::array<VkFence, MAX_FRAMES_IN_FLIGHT> in_flight_fences_{};

    std::uint32_t current_frame_ = 0;

    ClearColor clear_color_{};

private:
    void create_render_pass();
    void create_framebuffers();
    void create_command_pool();
    void create_command_buffers();
    void create_sync_objects();

    void recreate_swapchain_resources(gs3d::platform::Window& window);

    void record_command_buffer(
        VkCommandBuffer command_buffer,
        std::uint32_t image_index,
        const DrawCallback& draw_callback
    );

    void cleanup_swapchain_resources();
    void cleanup_sync_objects();

    [[nodiscard]]
    bool framebuffer_available(
        const gs3d::platform::Window& window
    ) const noexcept;
};

} // namespace gs3d::render