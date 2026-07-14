#pragma once

#include "app/ViewerApp.hpp"
#include "camera/CameraController.hpp"
#include "platform/Window.hpp"
#include "render/LodSelector.hpp"
#include "render/PointPipeline.hpp"
#include "render/TileSelection.hpp"
#include "render/VulkanContext.hpp"
#include "render/VulkanRenderer.hpp"

namespace gs3d::app {

[[nodiscard]] gs3d::platform::WindowConfig make_window_config(
    const ViewerWindowConfig& config
);

[[nodiscard]] gs3d::render::VulkanContextConfig make_vulkan_context_config(
    const ViewerGraphicsConfig& config
);

[[nodiscard]] gs3d::render::ClearColor make_clear_color(
    const ViewerGraphicsConfig& config
) noexcept;

[[nodiscard]] gs3d::render::PointPipelineConfig make_point_pipeline_config(
    const ViewerGraphicsConfig& config
);

[[nodiscard]] gs3d::camera::CameraControllerConfig
make_camera_controller_config(const ViewerControllerConfig& config) noexcept;

[[nodiscard]] gs3d::render::LodSelectorConfig make_lod_selector_config(
    const ViewerLodConfig& config
);

[[nodiscard]] gs3d::render::TileSelectionConfig make_tile_selection_config(
    const ViewerTileConfig& config
);

} // namespace gs3d::app
