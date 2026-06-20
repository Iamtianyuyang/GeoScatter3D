#pragma once

#include "render/OffscreenFramebuffer.hpp"
#include "render/VulkanContext.hpp"
#include "camera/Camera.hpp"

#include <vulkan/vulkan.h>

#include <functional>
#include <vector>
#include <cstdint>

namespace gs3d::render {

struct ViewportResizeRequest {
    int index = 0;
    VkExtent2D extent{};
};

/*
 * Manages N (OffscreenFramebuffer, Camera) pairs for multi-viewport rendering.
 *
 * All framebuffers share the same VkFormat so they are Vulkan render-pass
 * compatible with each other and with the swapchain — one PointPipeline
 * (compiled against viewport 0's render pass) can draw into all viewports.
 *
 * Design follows Open3D's multi-window architecture (MIT) for camera state
 * management, and Polyscope's viewport registry (MIT) for indexed access.
 *
 * Camera synchronization is handled by camera::CameraHub. This module owns
 * viewport render targets and cameras, but does not decide how cameras link.
 */
class ViewportManager {
public:
    using RenderCallback = std::function<void(
        VkCommandBuffer             cmd,
        int                         viewport_index,
        const gs3d::camera::Camera& camera,
        VkExtent2D                  extent
    )>;

    ViewportManager() = default;
    ~ViewportManager();

    ViewportManager(const ViewportManager&)            = delete;
    ViewportManager& operator=(const ViewportManager&) = delete;

    // Allocates `count` viewport slots [1, 4]. All share `color_format`;
    // camera is cloned from `initial_camera`.
    void init(
        const VulkanContext&          context,
        VkFormat                      color_format,
        int                           count,
        VkExtent2D                    initial_extent,
        const gs3d::camera::Camera&   initial_camera
    );

    // Total allocated slots.
    int viewport_count() const noexcept;

    // Currently active (rendered + shown) slots [1, viewport_count()].
    int  active_count() const noexcept;
    void set_active_count(int n) noexcept;

    // Per-viewport access by index [0, viewport_count()).
    gs3d::camera::Camera&       camera(int index);
    const gs3d::camera::Camera& camera(int index) const;

    OffscreenFramebuffer&       framebuffer(int index);
    const OffscreenFramebuffer& framebuffer(int index) const;

    // Shared render pass (viewport 0's) — identical format across all viewports.
    // Use this to compile PointPipeline once and reuse for all viewports.
    [[nodiscard]] VkRenderPass render_pass() const noexcept;

    // Record all N render passes into cmd, invoking callback once per viewport.
    void render_all(VkCommandBuffer cmd, const RenderCallback& callback);

    // Resize one viewport. Call AFTER the current GPU frame is submitted.
    // No-op if the extent is unchanged (avoids vkDeviceWaitIdle every stable frame).
    void resize(int index, VkExtent2D new_extent);

    // Resize several viewports behind one vkDeviceWaitIdle call.
    void resize_many(const std::vector<ViewportResizeRequest>& requests);

    void set_clear_color(const ClearColor& color) noexcept;

    void destroy() noexcept;

private:
    struct Entry {
        gs3d::camera::Camera         camera;
        gs3d::render::OffscreenFramebuffer framebuffer;
    };

    std::vector<Entry> entries_;
    const VulkanContext* context_ = nullptr;
    int                active_count_ = 0;
};

} // namespace gs3d::render
