#pragma once

#include "app/ViewerAppInternal.hpp"
#include "data/Gs3dFormat.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace gs3d::camera { class CameraController; }
namespace gs3d::camera { class CameraHub; }
namespace gs3d::data { class Gs3dDataset; }
namespace gs3d::render { class ViewportManager; }
namespace gs3d::render { struct PointPushConstants; }
namespace gs3d::render { class VulkanSwapchain; }
namespace gs3d::scene { struct SceneState; }

namespace gs3d::app {

struct ViewerAppPickState {
    std::vector<GpuPickRequest> requests;
    std::vector<std::optional<gs3d::data::Gs3dPoint>> latest_hover_points;
    std::vector<float> latest_capture_x;
    std::vector<float> latest_capture_y;
    std::vector<int> hover_timeout;
    std::vector<int> consecutive_no_hit;
    std::uint32_t frame_slot = 0;
};

struct ViewerAppPickLookupContext {
    const std::vector<gs3d::data::Gs3dPoint>& runtime_points_by_id;
    const std::vector<std::uint8_t>& runtime_points_valid_by_id;
};

struct ViewerAppPickCameraContext {
    std::vector<gs3d::camera::CameraController>& controllers;
    std::vector<std::optional<gs3d::camera::Vec3>>& selected_focus_points;
    gs3d::render::ViewportManager& viewport_manager;
    const gs3d::camera::CameraBounds& bounds;
    gs3d::render::PointPushConstants& push;
    int& streaming_viewport_index;
    bool& tile_selection_dirty;
};

struct ViewerAppBenchmarkPickContext {
    bool enabled = false;
    const std::vector<double>& issue_cpu_ms;
    const std::vector<BenchmarkPickIssuedMetadata>& issue_metadata;
    std::vector<BenchmarkPickObservedResult>& results;
};

struct ViewerAppRenderViewContext {
    const gs3d::render::ViewportManager& viewport_manager;
    const gs3d::data::Gs3dDataset& dataset;
    const gs3d::camera::CameraBounds& bounds;
    const gs3d::render::PointPushConstants& push;
    const std::string& primary_value_name;
    const std::string& z_field_name;
    std::uint64_t visible_points = 0;
    int n_viewports = 0;
};

struct ViewerAppRenderSettingsContext {
    gs3d::render::PointPushConstants& push;
    gs3d::scene::SceneState& scene_state;
    NavigationMapState& navigation_map;
    const std::vector<AttrDescriptor>& attr_list;
    const gs3d::data::Gs3dDataset& dataset;
    float& height_exag;
};

struct ViewerAppCameraCommandContext {
    int n_viewports = 0;
    std::vector<gs3d::camera::CameraController>& controllers;
    gs3d::render::ViewportManager& viewport_manager;
    gs3d::camera::CameraHub& camera_hub;
    const gs3d::camera::CameraBounds& bounds;
    int& streaming_viewport_index;
    bool& tile_selection_dirty;
};

struct ViewerAppScreenshotContext {
    const AppState& app_state;
    const gs3d::render::VulkanSwapchain& swapchain;
    VkExtent2D& screenshot_offset;
    VkExtent2D& screenshot_extent;
    bool& screenshot_pending;
};

struct RegionStatsCommandContext {
    const gs3d::render::ViewportManager& viewport_manager;
    const gs3d::data::Gs3dDataset& dataset;
    const gs3d::camera::CameraBounds& bounds;
    const std::string& primary_value_name;
    const std::string& z_field_name;
};

} // namespace gs3d::app
