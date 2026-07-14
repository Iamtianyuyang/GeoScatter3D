#pragma once

#include "render/OffscreenFramebuffer.hpp"

#include <vulkan/vulkan.h>

#include <vector>

namespace gs3d::app {
struct AppState;
struct NavigationMapState;
struct RenderViewState;
}
namespace gs3d::data { class Gs3dDataset; }
namespace gs3d::render { class PointCloudGpu; }
namespace gs3d::render { class PointPipeline; }
namespace gs3d::render { struct PointPushConstants; }
namespace gs3d::render { class VulkanContext; }

namespace gs3d::app {

// Owns navigation-thumbnail framebuffer lifetime and coordinates its initial
// render, dirty refreshes, and per-view world-to-thumbnail mapping.
class NavigationMapSystem {
public:
    void initialize(
        gs3d::render::VulkanContext& context,
        VkCommandPool command_pool,
        VkFormat color_format,
        const gs3d::data::Gs3dDataset& dataset,
        AppState& app_state,
        gs3d::render::PointPipeline& point_pipeline,
        const gs3d::render::PointCloudGpu& nav_cloud,
        const std::vector<gs3d::render::PointPushConstants>& viewport_pushes,
        float dataset_bbox_max_z
    );

    void synchronize_view_rects(AppState& app_state) const;

    void record_dirty_thumbnails(
        VkCommandBuffer command_buffer,
        AppState& app_state,
        gs3d::render::PointPipeline& point_pipeline,
        const gs3d::render::PointCloudGpu& nav_cloud,
        const std::vector<gs3d::render::PointPushConstants>& viewport_pushes,
        float dataset_bbox_max_z
    );

private:
    void initialize_map(
        gs3d::render::VulkanContext& context,
        VkCommandPool command_pool,
        VkFormat color_format,
        const gs3d::data::Gs3dDataset& dataset,
        gs3d::render::OffscreenFramebuffer& framebuffer,
        NavigationMapState& state,
        gs3d::render::PointPipeline& point_pipeline,
        const gs3d::render::PointCloudGpu& nav_cloud,
        const gs3d::render::PointPushConstants& push,
        float dataset_bbox_max_z
    );

    static void render_thumbnail(
        VkCommandBuffer command_buffer,
        gs3d::render::OffscreenFramebuffer& framebuffer,
        NavigationMapState& state,
        gs3d::render::PointPipeline& point_pipeline,
        const gs3d::render::PointCloudGpu& nav_cloud,
        const gs3d::render::PointPushConstants& push,
        float dataset_bbox_max_z
    );

    static void update_view_rect(
        NavigationMapState& state,
        const std::vector<RenderViewState>& render_views,
        int viewport_index
    );

    std::vector<gs3d::render::OffscreenFramebuffer> framebuffers_;
};

} // namespace gs3d::app
