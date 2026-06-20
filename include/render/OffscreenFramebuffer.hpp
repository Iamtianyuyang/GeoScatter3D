#pragma once

#include "render/VulkanContext.hpp"
#include "render/VulkanDepthBuffer.hpp"
#include "render/VulkanRenderer.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>

namespace gs3d::render {

/*
 * Offscreen render target that writes to a VkImage and exposes a
 * VkDescriptorSet usable directly with ImGui::Image().
 *
 * Design follows SaschaWillems/Vulkan offscreen sample (MIT) for the
 * render pass setup, and the ImGui docking Vulkan backend for texture
 * registration via ImGui_ImplVulkan_AddTexture.
 *
 * color_format passed to create() MUST match the swapchain format so
 * that PointPipeline (created against the swapchain render pass) stays
 * Vulkan-compatible with this framebuffer's render pass.
 */
class OffscreenFramebuffer {
public:
    using DrawCallback = std::function<void(VkCommandBuffer)>;

    OffscreenFramebuffer() = default;
    ~OffscreenFramebuffer();

    OffscreenFramebuffer(const OffscreenFramebuffer&) = delete;
    OffscreenFramebuffer& operator=(const OffscreenFramebuffer&) = delete;

    OffscreenFramebuffer(OffscreenFramebuffer&&) noexcept;
    OffscreenFramebuffer& operator=(OffscreenFramebuffer&&) noexcept;

    void create(
        const VulkanContext& context,
        VkExtent2D           extent,
        VkFormat             color_format
    );

    // Resize: recreates images and framebuffer; render pass is reused.
    void resize(VkExtent2D new_extent);

    // Same resize operation, but the caller guarantees the device is idle.
    // Used to batch several viewport resizes behind one synchronization point.
    void resize_after_device_idle(VkExtent2D new_extent);

    void destroy() noexcept;

    // Record one render pass into cmd. Caller owns the command buffer lifecycle.
    void render(VkCommandBuffer cmd, const DrawCallback& callback);

    void set_clear_color(const ClearColor& color) noexcept;

    [[nodiscard]] VkRenderPass    render_pass()      const noexcept;
    [[nodiscard]] VkExtent2D      extent()           const noexcept;
    [[nodiscard]] VkDescriptorSet imgui_descriptor() const noexcept;
    [[nodiscard]] bool            valid()            const noexcept;

private:
    const VulkanContext* context_      = nullptr;
    VkExtent2D           extent_       = {};
    VkFormat             color_format_ = VK_FORMAT_UNDEFINED;
    ClearColor           clear_color_  = {};

    VkImage        color_image_  = VK_NULL_HANDLE;
    VkDeviceMemory color_memory_ = VK_NULL_HANDLE;
    VkImageView    color_view_   = VK_NULL_HANDLE;

    VulkanDepthBuffer depth_buffer_{};

    VkRenderPass  render_pass_  = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_  = VK_NULL_HANDLE;

    VkDescriptorSet imgui_descriptor_ = VK_NULL_HANDLE;

private:
    void create_color_image();
    void create_render_pass();
    void create_framebuffer();
    void register_imgui_texture();
    void unregister_imgui_texture() noexcept;
    void cleanup_image_resources() noexcept;
    void recreate_image_resources(VkExtent2D new_extent);

    [[nodiscard]]
    static std::uint32_t find_memory_type(
        VkPhysicalDevice      physical_device,
        std::uint32_t         type_filter,
        VkMemoryPropertyFlags properties
    );
};

} // namespace gs3d::render
