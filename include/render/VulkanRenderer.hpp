#pragma once

#include "platform/Window.hpp"
#include "render/GpuFrameTimer.hpp"
#include "render/VulkanContext.hpp"
#include "render/VulkanSwapchain.hpp"

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
    using FrameReadyCallback = std::function<void(std::uint32_t frame_slot)>;

    // pre_pass:  recorded before the swapchain rendering scope begins
    //            (use for offscreen render passes)
    // in_pass:   recorded inside the swapchain dynamic rendering scope
    //            (ImGui, overlays)
    // post_pass: recorded after the rendering scope ends, still inside the
    //            command buffer; the swapchain image is in PRESENT_SRC_KHR
    //            (use for swapchain readback, layout transitions, etc.)
    //            Receives (VkCommandBuffer, swapchain_image_index).
    struct FrameDrawCallbacks {
        FrameReadyCallback frame_ready{};
        DrawCallback pre_pass{};
        DrawCallback in_pass{};
        std::function<void(VkCommandBuffer, std::uint32_t)> post_pass{};
    };

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

    // Legacy: draw_callback runs inside the main render pass.
    void draw_frame(
        gs3d::platform::Window& window,
        const DrawCallback& draw_callback
    );

    // Full control: pre_pass before render pass, in_pass inside render pass.
    void draw_frame(
        gs3d::platform::Window& window,
        const FrameDrawCallbacks& callbacks
    );

    void set_clear_color(const ClearColor& color) noexcept;

    /*
     * 等待所有 in-flight 帧的 fence（替代 vkDeviceWaitIdle）。
     * 只等当前正在飞行的渲染帧，不等无关的 GPU 队列。
     * 在销毁/替换被 GPU 使用的 buffer 前调用。
     */
    void wait_for_in_flight_fences();

    /*
     * vkAcquireNextImageKHR 在上一帧调用中的阻塞时长（毫秒）。
     * 仅 VK_PRESENT_MODE_FIFO_KHR 下有意义：vsync 等待会让帧时间
     * 虚高，LOD 自适应需要减去这部分才能正确判断 GPU 是否真的超预算。
     */
    [[nodiscard]]
    double last_acquire_wait_ms() const noexcept;

    [[nodiscard]]
    double last_frame_fence_wait_ms() const noexcept;

    [[nodiscard]]
    double last_upload_fence_wait_ms() const noexcept;

    [[nodiscard]]
    double last_draw_record_cpu_ms() const noexcept;

    [[nodiscard]]
    bool has_last_gpu_frame_ms() const noexcept;

    [[nodiscard]]
    double last_gpu_frame_ms() const noexcept;

    [[nodiscard]]
    VkFormat swapchain_image_format() const noexcept;

    [[nodiscard]]
    VkCommandPool command_pool() const noexcept;

    [[nodiscard]]
    VkExtent2D extent() const noexcept;

    [[nodiscard]]
    std::uint32_t frames_in_flight() const noexcept;

    [[nodiscard]]
    bool is_frame_slot_ready(std::uint32_t frame_slot) const;

private:
    static constexpr std::uint32_t MAX_FRAMES_IN_FLIGHT = 2;

private:
    const VulkanContext& context_;
    VulkanSwapchain& swapchain_;

    // 交换链 pass 使用 VK_KHR_dynamic_rendering（instance 为 Vulkan 1.2，
    // 入口函数须经 vkGetDeviceProcAddr 以 KHR 后缀加载）。
    PFN_vkCmdBeginRenderingKHR cmd_begin_rendering_ = nullptr;
    PFN_vkCmdEndRenderingKHR cmd_end_rendering_ = nullptr;

    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> command_buffers_;

    std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> image_available_semaphores_{};
    std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> render_finished_semaphores_{};
    std::array<VkFence, MAX_FRAMES_IN_FLIGHT> in_flight_fences_{};

    GpuFrameTimer gpu_frame_timer_;

    std::uint32_t current_frame_ = 0;

    // vkAcquireNextImageKHR 阻塞时长（毫秒），用于 FIFO vsync 补偿
    double last_acquire_wait_ms_ = 0.0;
    double last_frame_fence_wait_ms_ = 0.0;
    double last_upload_fence_wait_ms_ = 0.0;
    double last_draw_record_cpu_ms_ = 0.0;

    ClearColor clear_color_{};

private:
    void load_dynamic_rendering_functions();
    void create_command_pool();
    void create_command_buffers();
    void create_sync_objects();

    void recreate_swapchain_resources(gs3d::platform::Window& window);

    void record_command_buffer(
        VkCommandBuffer command_buffer,
        std::uint32_t image_index,
        const FrameDrawCallbacks& callbacks
    );

    void cleanup_sync_objects();

    [[nodiscard]]
    bool framebuffer_available(
        const gs3d::platform::Window& window
    ) const noexcept;
};

} // namespace gs3d::render
