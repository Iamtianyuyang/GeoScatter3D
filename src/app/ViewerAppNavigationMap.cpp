#include "app/ViewerApp.hpp"
#include "app/ViewerAppRunState.hpp"

#include "app/AppState.hpp"
#include "camera/Camera.hpp"
#include "data/Gs3dDataset.hpp"
#include "render/OffscreenFramebuffer.hpp"
#include "render/PointCloudGpu.hpp"
#include "render/PointPipeline.hpp"
#include "render/VulkanContext.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace gs3d::app {

/*
 * 导航图缩略图：正交俯视相机从上往下渲染最低细节点云，覆盖整个数据集
 * XY 包围盒。启动时预渲染一次(init_navigation_map)，着色属性变更后在
 * pre_pass 中重渲(nav.dirty)。相机参数只依赖 nav 里的坐标映射字段，
 * 两条路径共用同一份实现。
 */
void ViewerApp::record_navigation_thumbnail(
    VkCommandBuffer cmd,
    gs3d::render::OffscreenFramebuffer& nav_fb,
    NavigationMapState& nav,
    const ViewerAppNavThumbnailContext& ctx
) {
    const auto tex_w = static_cast<std::uint32_t>(nav.tex_w);
    const auto tex_h = static_cast<std::uint32_t>(nav.tex_h);

    // 正交俯视相机：从上往下看，覆盖整个 bbox 的 XY 范围
    gs3d::camera::Camera nav_cam;
    nav_cam.set_viewport(tex_w, tex_h);
    const float nav_bbox_h = nav.bbox_max_y - nav.bbox_min_y;
    nav_cam.set_orthographic(
        nav_bbox_h,
        0.001f,
        std::max(1.0f, nav_bbox_h * 10.0f)
    );
    const float nav_cx =
        (nav.bbox_min_x + nav.bbox_max_x) * 0.5f;
    const float nav_cy =
        (nav.bbox_min_y + nav.bbox_max_y) * 0.5f;
    nav_cam.look_at(
        {nav_cx, nav_cy, ctx.dataset_bbox_max_z + nav_bbox_h},
        {nav_cx, nav_cy, 0.0f},
        {0.0f, 1.0f, 0.0f}
    );

    const auto nav_mvp = nav_cam.view_projection_matrix();
    gs3d::render::PointPushConstants nav_push = ctx.push;
    std::memcpy(nav_push.mvp, nav_mvp.data(), sizeof(nav_push.mvp));

    nav_fb.render(
        cmd,
        [&](VkCommandBuffer cb) {
            ctx.point_pipeline.draw(
                cb,
                ctx.nav_cloud,
                VkExtent2D{tex_w, tex_h},
                nav_push
            );
        }
    );

    nav.texture_descriptor = nav_fb.imgui_descriptor();
    nav.dirty = false;
}

void ViewerApp::init_navigation_map(
    gs3d::render::VulkanContext& context,
    VkCommandPool command_pool,
    VkFormat color_format,
    const gs3d::data::Gs3dDataset& dataset,
    gs3d::render::OffscreenFramebuffer& nav_fb,
    NavigationMapState& nav,
    const ViewerAppNavThumbnailContext& ctx
) {
    // bbox 宽高比决定纹理尺寸，保证纹理像素全部有效，无 letterbox。
    const float nav_bbox_w =
        dataset.bbox_max_x() - dataset.bbox_min_x();
    const float nav_bbox_h =
        dataset.bbox_max_y() - dataset.bbox_min_y();
    const float nav_aspect =
        nav_bbox_h > 0.0f ? nav_bbox_w / nav_bbox_h : 1.0f;
    constexpr float kNavBaseSize = 256.0f;
    std::uint32_t nav_tex_w = kNavBaseSize;
    std::uint32_t nav_tex_h = kNavBaseSize;
    if (nav_aspect >= 1.0f) {
        nav_tex_h = std::max(
            64u,
            static_cast<std::uint32_t>(kNavBaseSize / nav_aspect)
        );
    } else {
        nav_tex_w = std::max(
            64u,
            static_cast<std::uint32_t>(kNavBaseSize * nav_aspect)
        );
    }

    nav_fb.create(
        context,
        VkExtent2D{nav_tex_w, nav_tex_h},
        color_format
    );

    // 坐标系映射：纹理像素 ↔ 数据集 XY 包围盒，只在这里写一次。
    // 缩略图渲染相机和视野框绘制共用这组参数。
    nav.tex_w = static_cast<float>(nav_tex_w);
    nav.tex_h = static_cast<float>(nav_tex_h);
    nav.bbox_min_x = dataset.bbox_min_x();
    nav.bbox_min_y = dataset.bbox_min_y();
    nav.bbox_max_x = dataset.bbox_max_x();
    nav.bbox_max_y = dataset.bbox_max_y();

    // 单次命令缓冲区：渲染缩略图
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType =
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandPool = command_pool;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(
        context.device(),
        &alloc_info,
        &cmd
    );

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType =
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags =
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin_info);

    record_navigation_thumbnail(cmd, nav_fb, nav, ctx);

    vkEndCommandBuffer(cmd);

    VkFence fence = VK_NULL_HANDLE;
    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkCreateFence(context.device(), &fence_info, nullptr, &fence);

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;
    vkQueueSubmit(
        context.graphics_queue(),
        1,
        &submit_info,
        fence
    );

    vkWaitForFences(context.device(), 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(context.device(), fence, nullptr);
    vkFreeCommandBuffers(
        context.device(),
        command_pool,
        1,
        &cmd
    );

    nav.valid = true;
}

} // namespace gs3d::app
