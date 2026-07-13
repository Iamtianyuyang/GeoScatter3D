#pragma once

#include <cstddef>
#include <vector>

namespace gs3d::app {
class NavigationMapSystem;
class ScreenshotService;
class ViewerBenchmarkController;
class ViewerPickSystem;
class ViewerViewportRenderSystem;
struct AppState;
struct ViewerAppTileStreamState;
struct ViewerPickFrameContext;
}
namespace gs3d::render {
class PointCloudGpu;
class PointCloudLodGpu;
class PointCloudTileGpu;
class PointPipeline;
class ViewportManager;
class VulkanContext;
class VulkanRenderer;
class VulkanSwapchain;
struct PointPushConstants;
struct TileSelectionResult;
}
namespace gs3d::gui { class ImGuiLayer; }
namespace gs3d::platform { class Window; }

namespace gs3d::app {

struct ViewerFrameRenderContext {
    AppState& app_state;
    const std::vector<int>& visible_viewports;
    gs3d::render::ViewportManager& viewport_manager;
    const std::vector<gs3d::render::PointPushConstants>& viewport_pushes;
    const gs3d::render::PointCloudLodGpu* lod_gpu_cloud = nullptr;
    const gs3d::render::PointCloudGpu* full_gpu_cloud = nullptr;
    const gs3d::render::PointCloudTileGpu* tile_gpu_cloud = nullptr;
    const ViewerAppTileStreamState& tile_stream;
    const gs3d::render::TileSelectionResult& tile_result;
    std::size_t lod_level_for_frame = 0;
    bool interacting = false;
};

struct ViewerFrameRenderMeasurements {
    double draw_record_ms = 0.0;
    double acquire_wait_ms = 0.0;
    double frame_fence_wait_ms = 0.0;
    double upload_fence_wait_ms = 0.0;
};

// Owns the one-frame presentation sequence: offscreen viewport passes, ImGui,
// optional screenshot readback and renderer timing measurements. Its
// dependencies are stable for the viewer session; each call accepts only the
// state that varies from frame to frame.
class ViewerFrameRenderer {
public:
    ViewerFrameRenderer(
        gs3d::platform::Window& window,
        gs3d::render::VulkanContext& context,
        gs3d::render::VulkanSwapchain& swapchain,
        gs3d::render::VulkanRenderer& renderer,
        gs3d::gui::ImGuiLayer& imgui_layer,
        NavigationMapSystem& navigation_maps,
        gs3d::render::PointPipeline& point_pipeline,
        const gs3d::render::PointCloudGpu& navigation_cloud,
        ViewerViewportRenderSystem& viewport_renderer,
        ViewerPickSystem& pick_system,
        ViewerPickFrameContext& pick_frame_context,
        ViewerBenchmarkController& benchmark_controller,
        ScreenshotService& screenshot_service,
        float dataset_max_z
    ) noexcept;

    [[nodiscard]] ViewerFrameRenderMeasurements render(
        const ViewerFrameRenderContext& frame_context
    );

private:
    gs3d::platform::Window& window_;
    gs3d::render::VulkanContext& context_;
    gs3d::render::VulkanSwapchain& swapchain_;
    gs3d::render::VulkanRenderer& renderer_;
    gs3d::gui::ImGuiLayer& imgui_layer_;
    NavigationMapSystem& navigation_maps_;
    gs3d::render::PointPipeline& point_pipeline_;
    const gs3d::render::PointCloudGpu& navigation_cloud_;
    ViewerViewportRenderSystem& viewport_renderer_;
    ViewerPickSystem& pick_system_;
    ViewerPickFrameContext& pick_frame_context_;
    ViewerBenchmarkController& benchmark_controller_;
    ScreenshotService& screenshot_service_;
    float dataset_max_z_ = 0.0f;
};

} // namespace gs3d::app
