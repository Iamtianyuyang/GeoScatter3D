#pragma once

#include "app/ViewerAppInternal.hpp"
#include "data/Gs3dFormat.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace gs3d::camera { class CameraController; }
namespace gs3d::render { class ViewportManager; }
namespace gs3d::render { struct PointPushConstants; }

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

} // namespace gs3d::app
