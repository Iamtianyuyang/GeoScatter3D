#include "app/ViewerFrameRenderer.hpp"

#include "app/AppState.hpp"
#include "app/NavigationMapSystem.hpp"
#include "app/ScreenshotService.hpp"
#include "app/ViewerAppRunState.hpp"
#include "app/ViewerAppTileStreaming.hpp"
#include "app/ViewerBenchmarkController.hpp"
#include "app/ViewerPickSystem.hpp"
#include "app/ViewerViewportRenderSystem.hpp"
#include "ui/ImGuiLayer.hpp"
#include "platform/Window.hpp"
#include "render/PointCloudGpu.hpp"
#include "render/PointCloudLodGpu.hpp"
#include "render/PointCloudTileGpu.hpp"
#include "render/PointPipeline.hpp"
#include "render/TileSelection.hpp"
#include "render/ViewportManager.hpp"
#include "render/VulkanContext.hpp"
#include "render/VulkanRenderer.hpp"
#include "render/VulkanSwapchain.hpp"

namespace gs3d::app {

ViewerFrameRenderer::ViewerFrameRenderer(
    gs3d::platform::Window& window,
    gs3d::render::VulkanContext& context,
    gs3d::render::VulkanSwapchain& swapchain,
    gs3d::render::VulkanRenderer& renderer,
    gs3d::ui::ImGuiLayer& imgui_layer,
    NavigationMapSystem& navigation_maps,
    gs3d::render::PointPipeline& point_pipeline,
    const gs3d::render::PointCloudGpu& navigation_cloud,
    ViewerViewportRenderSystem& viewport_renderer,
    ViewerPickSystem& pick_system,
    ViewerPickFrameContext& pick_frame_context,
    ViewerBenchmarkController& benchmark_controller,
    ScreenshotService& screenshot_service,
    const float dataset_max_z
) noexcept
    : window_(window)
    , context_(context)
    , swapchain_(swapchain)
    , renderer_(renderer)
    , imgui_layer_(imgui_layer)
    , navigation_maps_(navigation_maps)
    , point_pipeline_(point_pipeline)
    , navigation_cloud_(navigation_cloud)
    , viewport_renderer_(viewport_renderer)
    , pick_system_(pick_system)
    , pick_frame_context_(pick_frame_context)
    , benchmark_controller_(benchmark_controller)
    , screenshot_service_(screenshot_service)
    , dataset_max_z_(dataset_max_z)
{
}

ViewerFrameRenderMeasurements ViewerFrameRenderer::render(
    const ViewerFrameRenderContext& frame_context
) {
    renderer_.draw_frame(
        window_,
        gs3d::render::VulkanRenderer::FrameDrawCallbacks{
            .frame_ready = [&](const std::uint32_t frame_slot) {
                pick_system_.consume_ready_frame_slot(
                    frame_slot,
                    pick_frame_context_
                );
            },
            .pre_pass = [&](VkCommandBuffer command_buffer) {
                navigation_maps_.record_dirty_thumbnails(
                    command_buffer,
                    frame_context.app_state,
                    point_pipeline_,
                    navigation_cloud_,
                    frame_context.viewport_pushes,
                    dataset_max_z_
                );
                const auto& benchmark_session = benchmark_controller_.session();
                ViewerViewportDrawContext viewport_context{
                    .visible_viewports = frame_context.visible_viewports,
                    .viewport_manager = frame_context.viewport_manager,
                    .point_pipeline = point_pipeline_,
                    .viewport_pushes = frame_context.viewport_pushes,
                    .lod_gpu_cloud = frame_context.lod_gpu_cloud,
                    .full_gpu_cloud = frame_context.full_gpu_cloud,
                    .tile_gpu_cloud = frame_context.tile_gpu_cloud,
                    .tile_stream = frame_context.tile_stream,
                    .tile_result = frame_context.tile_result,
                    .pick = pick_system_.state(),
                    .gpu_pick_readback = pick_system_.gpu_readback(),
                    .pick_debug_frame_dumper = pick_system_.debug_frame_dumper(),
                    .pending_hover_miss_dump =
                        pick_system_.pending_hover_miss_dump(),
                    .pick_debug_dump_count = pick_system_.debug_dump_count(),
                    .pick_debug_dump_completed =
                        pick_system_.debug_dump_completed(),
                    .lod_level_for_frame = frame_context.lod_level_for_frame,
                    .interacting = frame_context.interacting,
                    .benchmark_pick_enabled = benchmark_controller_.pick_enabled(),
                    .app_frame_index = benchmark_session.app_frame_index(),
                    .benchmark_pick_issue_cpu_ms =
                        benchmark_controller_.issue_cpu_ms(),
                    .benchmark_pick_issue_metadata =
                        benchmark_controller_.issue_metadata()
                };
                viewport_renderer_.record(command_buffer, viewport_context);
            },
            .in_pass = [&](VkCommandBuffer command_buffer) {
                imgui_layer_.render(command_buffer);
            },
            .post_pass = [&](VkCommandBuffer command_buffer,
                             const std::uint32_t image_index) {
                screenshot_service_.record_copy(
                    command_buffer,
                    image_index,
                    context_,
                    swapchain_
                );
            }
        }
    );

    const ViewerFrameRenderMeasurements measurements{
        .draw_record_ms = renderer_.last_draw_record_cpu_ms(),
        .acquire_wait_ms = renderer_.last_acquire_wait_ms(),
        .frame_fence_wait_ms = renderer_.last_frame_fence_wait_ms(),
        .upload_fence_wait_ms = renderer_.last_upload_fence_wait_ms(),
    };
    imgui_layer_.render_platform_windows();
    imgui_layer_.discard_frame();
    screenshot_service_.write_pending(context_, swapchain_);
    return measurements;
}

} // namespace gs3d::app
