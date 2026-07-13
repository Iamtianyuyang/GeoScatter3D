#include "app/NavigationMapSystem.hpp"

#include "app/AppState.hpp"
#include "camera/Camera.hpp"
#include "data/Gs3dDataset.hpp"
#include "render/PointCloudGpu.hpp"
#include "render/PointPipeline.hpp"
#include "render/VulkanContext.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace gs3d::app {

void NavigationMapSystem::initialize(
    gs3d::render::VulkanContext& context,
    VkCommandPool command_pool,
    VkFormat color_format,
    const gs3d::data::Gs3dDataset& dataset,
    AppState& app_state,
    gs3d::render::PointPipeline& point_pipeline,
    const gs3d::render::PointCloudGpu& nav_cloud,
    const std::vector<gs3d::render::PointPushConstants>& viewport_pushes,
    float dataset_bbox_max_z
) {
    framebuffers_.clear();
    framebuffers_.resize(app_state.navigation_maps.size());
    for (std::size_t index = 0;
         index < app_state.navigation_maps.size();
         ++index) {
        const auto& push =
            index < viewport_pushes.size()
                ? viewport_pushes[index]
                : viewport_pushes.front();
        initialize_map(
            context,
            command_pool,
            color_format,
            dataset,
            framebuffers_[index],
            app_state.navigation_maps[index],
            point_pipeline,
            nav_cloud,
            push,
            dataset_bbox_max_z
        );
    }
    if (!app_state.navigation_maps.empty()) {
        app_state.navigation_map = app_state.navigation_maps.front();
    }
}

void NavigationMapSystem::synchronize_view_rects(AppState& app_state) const
{
    const std::size_t count = std::min(
        app_state.navigation_maps.size(),
        app_state.render_views.size()
    );
    for (std::size_t index = 0; index < count; ++index) {
        update_view_rect(
            app_state.navigation_maps[index],
            app_state.render_views,
            static_cast<int>(index)
        );
    }
    app_state.navigation_map = navigation_map_for_view(
        app_state,
        app_state.active_viewport_index
    );
}

void NavigationMapSystem::record_dirty_thumbnails(
    VkCommandBuffer command_buffer,
    AppState& app_state,
    gs3d::render::PointPipeline& point_pipeline,
    const gs3d::render::PointCloudGpu& nav_cloud,
    const std::vector<gs3d::render::PointPushConstants>& viewport_pushes,
    float dataset_bbox_max_z
) {
    const std::size_t count = std::min(
        app_state.navigation_maps.size(),
        framebuffers_.size()
    );
    for (std::size_t index = 0; index < count; ++index) {
        auto& state = app_state.navigation_maps[index];
        auto& framebuffer = framebuffers_[index];
        if (!state.dirty || !framebuffer.valid()) {
            continue;
        }
        const auto& push =
            index < viewport_pushes.size()
                ? viewport_pushes[index]
                : viewport_pushes.front();
        render_thumbnail(
            command_buffer,
            framebuffer,
            state,
            point_pipeline,
            nav_cloud,
            push,
            dataset_bbox_max_z
        );
    }
}

void NavigationMapSystem::render_thumbnail(
    VkCommandBuffer command_buffer,
    gs3d::render::OffscreenFramebuffer& framebuffer,
    NavigationMapState& state,
    gs3d::render::PointPipeline& point_pipeline,
    const gs3d::render::PointCloudGpu& nav_cloud,
    const gs3d::render::PointPushConstants& push,
    float dataset_bbox_max_z
) {
    const auto texture_width = static_cast<std::uint32_t>(state.tex_w);
    const auto texture_height = static_cast<std::uint32_t>(state.tex_h);

    gs3d::camera::Camera camera;
    camera.set_viewport(texture_width, texture_height);
    const float bbox_height = state.bbox_max_y - state.bbox_min_y;
    camera.set_orthographic(
        bbox_height,
        0.001f,
        std::max(1.0f, bbox_height * 10.0f)
    );
    const float center_x = (state.bbox_min_x + state.bbox_max_x) * 0.5f;
    const float center_y = (state.bbox_min_y + state.bbox_max_y) * 0.5f;
    camera.look_at(
        {center_x, center_y, dataset_bbox_max_z + bbox_height},
        {center_x, center_y, 0.0f},
        {0.0f, 1.0f, 0.0f}
    );

    const auto matrix = camera.view_projection_matrix();
    gs3d::render::PointPushConstants thumbnail_push = push;
    std::memcpy(
        thumbnail_push.mvp,
        matrix.data(),
        sizeof(thumbnail_push.mvp)
    );

    framebuffer.render(
        command_buffer,
        [&](VkCommandBuffer command_buffer) {
            point_pipeline.draw(
                command_buffer,
                nav_cloud,
                VkExtent2D{texture_width, texture_height},
                thumbnail_push
            );
        }
    );

    state.texture_descriptor = framebuffer.imgui_descriptor();
    state.dirty = false;
}

void NavigationMapSystem::initialize_map(
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
) {
    const float bbox_width = dataset.bbox_max_x() - dataset.bbox_min_x();
    const float bbox_height = dataset.bbox_max_y() - dataset.bbox_min_y();
    const float aspect = bbox_height > 0.0f ? bbox_width / bbox_height : 1.0f;
    constexpr float kBaseTextureSize = 256.0f;
    std::uint32_t texture_width = kBaseTextureSize;
    std::uint32_t texture_height = kBaseTextureSize;
    if (aspect >= 1.0f) {
        texture_height = std::max(
            64u,
            static_cast<std::uint32_t>(kBaseTextureSize / aspect)
        );
    } else {
        texture_width = std::max(
            64u,
            static_cast<std::uint32_t>(kBaseTextureSize * aspect)
        );
    }

    framebuffer.create(
        context,
        VkExtent2D{texture_width, texture_height},
        color_format
    );
    state.tex_w = static_cast<float>(texture_width);
    state.tex_h = static_cast<float>(texture_height);
    state.bbox_min_x = dataset.bbox_min_x();
    state.bbox_min_y = dataset.bbox_min_y();
    state.bbox_max_x = dataset.bbox_max_x();
    state.bbox_max_y = dataset.bbox_max_y();

    VkCommandBufferAllocateInfo allocate_info{};
    allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate_info.commandPool = command_pool;
    allocate_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(context.device(), &allocate_info, &command_buffer);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(command_buffer, &begin_info);

    render_thumbnail(
        command_buffer,
        framebuffer,
        state,
        point_pipeline,
        nav_cloud,
        push,
        dataset_bbox_max_z
    );

    vkEndCommandBuffer(command_buffer);

    VkFence fence = VK_NULL_HANDLE;
    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkCreateFence(context.device(), &fence_info, nullptr, &fence);

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;
    vkQueueSubmit(context.graphics_queue(), 1, &submit_info, fence);

    vkWaitForFences(context.device(), 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(context.device(), fence, nullptr);
    vkFreeCommandBuffers(context.device(), command_pool, 1, &command_buffer);

    state.valid = true;
}

void NavigationMapSystem::update_view_rect(
    NavigationMapState& state,
    const std::vector<RenderViewState>& render_views,
    int viewport_index
) {
    if (!state.valid || viewport_index < 0 ||
        viewport_index >= static_cast<int>(render_views.size())) {
        return;
    }
    const auto& view = render_views[static_cast<std::size_t>(viewport_index)];
    const float bbox_width = state.bbox_max_x - state.bbox_min_x;
    const float bbox_height = state.bbox_max_y - state.bbox_min_y;
    if (bbox_width <= 0.0f || bbox_height <= 0.0f) {
        return;
    }
    state.view_rect_min_x =
        (view.map_axis_x_min - state.bbox_min_x) / bbox_width * state.tex_w;
    state.view_rect_max_x =
        (view.map_axis_x_max - state.bbox_min_x) / bbox_width * state.tex_w;
    state.view_rect_min_y =
        (1.0f - (view.map_axis_y_max - state.bbox_min_y) / bbox_height) *
        state.tex_h;
    state.view_rect_max_y =
        (1.0f - (view.map_axis_y_min - state.bbox_min_y) / bbox_height) *
        state.tex_h;
    state.view_rect_valid = true;
}

} // namespace gs3d::app
