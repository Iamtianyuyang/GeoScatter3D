#include "render/ViewportManager.hpp"

#include <algorithm>
#include <stdexcept>

namespace gs3d::render {

ViewportManager::~ViewportManager()
{
    destroy();
}

void ViewportManager::init(
    const VulkanContext&        context,
    VkFormat                    color_format,
    int                         count,
    VkExtent2D                  initial_extent,
    const gs3d::camera::Camera& initial_camera
) {
    context_ = &context;
    active_count_ = std::clamp(count, 1, 4);
    entries_.resize(static_cast<std::size_t>(active_count_));
    for (auto& e : entries_) {
        e.camera = initial_camera;
        e.framebuffer.create(context, initial_extent, color_format);
    }
}

int ViewportManager::viewport_count() const noexcept
{
    return static_cast<int>(entries_.size());
}

int ViewportManager::active_count() const noexcept
{
    return active_count_;
}

void ViewportManager::set_active_count(int n) noexcept
{
    active_count_ = std::clamp(n, 1, static_cast<int>(entries_.size()));
}

gs3d::camera::Camera& ViewportManager::camera(int index)
{
    return entries_.at(static_cast<std::size_t>(index)).camera;
}

const gs3d::camera::Camera& ViewportManager::camera(int index) const
{
    return entries_.at(static_cast<std::size_t>(index)).camera;
}

OffscreenFramebuffer& ViewportManager::framebuffer(int index)
{
    return entries_.at(static_cast<std::size_t>(index)).framebuffer;
}

const OffscreenFramebuffer& ViewportManager::framebuffer(int index) const
{
    return entries_.at(static_cast<std::size_t>(index)).framebuffer;
}

VkRenderPass ViewportManager::render_pass() const noexcept
{
    if (entries_.empty()) return VK_NULL_HANDLE;
    return entries_[0].framebuffer.render_pass();
}

void ViewportManager::render_all(VkCommandBuffer cmd, const RenderCallback& callback)
{
    for (int i = 0; i < active_count_; ++i) {
        auto& e = entries_[static_cast<std::size_t>(i)];
        e.framebuffer.render(cmd, [&, i](VkCommandBuffer c) {
            callback(c, i, e.camera, e.framebuffer.extent());
        });
    }
}

void ViewportManager::resize(int index, VkExtent2D new_extent)
{
    resize_many({ViewportResizeRequest{index, new_extent}});
}

void ViewportManager::resize_many(
    const std::vector<ViewportResizeRequest>& requests
) {
    std::vector<ViewportResizeRequest> changed;
    changed.reserve(requests.size());

    for (const auto& request : requests) {
        if (request.index < 0 ||
            request.index >= static_cast<int>(entries_.size()) ||
            request.extent.width == 0 ||
            request.extent.height == 0) {
            continue;
        }

        const auto& extent =
            entries_[static_cast<std::size_t>(request.index)]
                .framebuffer.extent();
        if (extent.width == request.extent.width &&
            extent.height == request.extent.height) {
            continue;
        }
        changed.push_back(request);
    }

    if (changed.empty() || context_ == nullptr) {
        return;
    }

    vkDeviceWaitIdle(context_->device());
    for (const auto& request : changed) {
        auto& entry = entries_[static_cast<std::size_t>(request.index)];
        entry.framebuffer.resize_after_device_idle(request.extent);
        entry.camera.set_viewport(
            request.extent.width,
            request.extent.height
        );
    }
}

void ViewportManager::set_clear_color(const ClearColor& color) noexcept
{
    for (auto& e : entries_) {
        e.framebuffer.set_clear_color(color);
    }
}

void ViewportManager::destroy() noexcept
{
    for (auto& e : entries_) {
        e.framebuffer.destroy();
    }
    entries_.clear();
    context_ = nullptr;
    active_count_ = 0;
}

} // namespace gs3d::render
