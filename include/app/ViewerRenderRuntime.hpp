#pragma once

#include "camera/Camera.hpp"
#include "gui/ImGuiLayer.hpp"
#include "render/VulkanContext.hpp"
#include "render/VulkanRenderer.hpp"
#include "render/VulkanSwapchain.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace gs3d::app {
struct ViewerBenchmarkConfig;
struct ViewerCameraConfig;
struct ViewerGraphicsConfig;
struct ViewerLodConfig;
struct ViewerTileConfig;
struct ViewerWindowConfig;
}
namespace gs3d::data {
class Gs3dDataset;
class Gs3dLodDataset;
}
namespace gs3d::platform { class Window; }
namespace gs3d::render {
class PointCloudGpu;
class PointCloudLodGpu;
class PointCloudTileGpu;
class PointPipeline;
class ViewportManager;
}
namespace gs3d::ui { class SvgLogoTexture; }

namespace gs3d::app {

// The graphics bootstrap depends only on the configuration domains and data
// it actually consumes. Keeping these references explicit prevents the render
// runtime from depending on the full ViewerAppConfig.
struct ViewerRenderRuntimeInput {
    const ViewerWindowConfig& window_config;
    const ViewerGraphicsConfig& graphics_config;
    const ViewerBenchmarkConfig& benchmark_config;
    const ViewerCameraConfig& camera_config;
    const ViewerLodConfig& lod_config;
    const ViewerTileConfig& tile_config;
    const gs3d::data::Gs3dDataset& dataset;
    const std::vector<std::uint32_t>& full_point_ids;
    const gs3d::data::Gs3dLodDataset& lod_dataset;
    const std::vector<std::vector<std::uint32_t>>& lod_point_ids;
    bool tile_reader_available = false;
    std::function<void()> on_context_created;
};

// Owns device-backed viewer resources in dependency order. Window lifetime is
// external; all Vulkan, ImGui and GPU objects are destroyed before it.
class ViewerRenderRuntime {
public:
    ViewerRenderRuntime(
        gs3d::platform::Window& window,
        const ViewerRenderRuntimeInput& input
    );
    ~ViewerRenderRuntime();

    ViewerRenderRuntime(const ViewerRenderRuntime&) = delete;
    ViewerRenderRuntime& operator=(const ViewerRenderRuntime&) = delete;

    [[nodiscard]] gs3d::render::VulkanContext& context() noexcept;
    [[nodiscard]] gs3d::render::VulkanSwapchain& swapchain() noexcept;
    [[nodiscard]] gs3d::render::VulkanRenderer& renderer() noexcept;
    [[nodiscard]] gs3d::gui::ImGuiLayer& imgui_layer() noexcept;
    [[nodiscard]] gs3d::render::ViewportManager& viewport_manager() noexcept;
    [[nodiscard]] gs3d::render::PointPipeline& point_pipeline() noexcept;
    [[nodiscard]] const gs3d::camera::CameraBounds& bounds() const noexcept;
    [[nodiscard]] const gs3d::ui::SvgLogoTexture& logo_texture() const noexcept;
    [[nodiscard]] gs3d::render::PointCloudGpu* full_gpu_cloud() noexcept;
    [[nodiscard]] gs3d::render::PointCloudLodGpu* lod_gpu_cloud() noexcept;
    [[nodiscard]] gs3d::render::PointCloudTileGpu* tile_gpu_cloud() noexcept;
    [[nodiscard]] const gs3d::render::PointCloudGpu& navigation_cloud() const;

private:
    gs3d::platform::Window& window_;
    gs3d::render::VulkanContext context_;
    gs3d::render::VulkanSwapchain swapchain_;
    gs3d::render::VulkanRenderer renderer_;
    gs3d::gui::ImGuiLayer imgui_layer_;
    std::unique_ptr<gs3d::ui::SvgLogoTexture> logo_texture_;
    std::unique_ptr<gs3d::render::PointCloudGpu> full_gpu_cloud_;
    std::unique_ptr<gs3d::render::PointCloudLodGpu> lod_gpu_cloud_;
    std::unique_ptr<gs3d::render::PointCloudTileGpu> tile_gpu_cloud_;
    std::unique_ptr<gs3d::render::ViewportManager> viewport_manager_;
    std::unique_ptr<gs3d::render::PointPipeline> point_pipeline_;
    gs3d::camera::CameraBounds bounds_{};
};

} // namespace gs3d::app
