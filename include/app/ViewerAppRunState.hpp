#pragma once

#include "app/ViewerPickSystem.hpp"
#include "data/Gs3dFormat.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace gs3d::camera { class CameraController; }
namespace gs3d::camera { class CameraHub; }
namespace gs3d::data { class Gs3dDataset; }
namespace gs3d::render { class PointCloudGpu; }
namespace gs3d::render { class PointCloudLodGpu; }
namespace gs3d::render { class PointCloudTileGpu; }
namespace gs3d::render { class PointPipeline; }
namespace gs3d::render { struct TileSelectionResult; }
namespace gs3d::render { class ViewportManager; }
namespace gs3d::render { struct PointPushConstants; }
namespace gs3d::render { class VulkanSwapchain; }
namespace gs3d::scene { struct SceneState; }

namespace gs3d::app {

struct ViewerAppTileStreamState;

struct ViewerAppRenderViewContext {
    const gs3d::render::ViewportManager& viewport_manager;
    const gs3d::data::Gs3dDataset& dataset;
    const gs3d::camera::CameraBounds& bounds;
    const std::vector<gs3d::render::PointPushConstants>& viewport_pushes;
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

/*
 * Inputs for ViewerApp::record_viewport_passes() — the per-frame
 * offscreen render of every visible viewport (LOD safety net + active
 * LOD level or full cloud + resident tile overlay) plus GPU pick
 * request recording and the optional pick debug dump.
 */
struct ViewerAppViewportDrawContext {
    const std::vector<int>& visible_viewports;
    gs3d::render::ViewportManager& viewport_manager;
    gs3d::render::PointPipeline& point_pipeline;
    const std::vector<gs3d::render::PointPushConstants>& viewport_pushes;
    // Exactly one of lod_gpu_cloud / full_gpu_cloud is non-null.
    const gs3d::render::PointCloudLodGpu* lod_gpu_cloud = nullptr;
    const gs3d::render::PointCloudGpu* full_gpu_cloud = nullptr;
    // nullptr when tile mode is disabled.
    const gs3d::render::PointCloudTileGpu* tile_gpu_cloud = nullptr;
    const ViewerAppTileStreamState& tile_stream;
    const gs3d::render::TileSelectionResult& tile_result;
    const ViewerPickState& pick;
    GpuPickReadback& gpu_pick_readback;
    PickDebugFrameDumper& pick_debug_frame_dumper;
    std::vector<bool>& pending_hover_miss_dump;
    std::uint64_t& pick_debug_dump_count;
    bool& pick_debug_dump_completed;
    std::size_t lod_level_for_frame = 0;
    bool interacting = false;
    bool benchmark_pick_enabled = false;
    std::uint64_t app_frame_index = 0;
    std::vector<double>& benchmark_pick_issue_cpu_ms;
    std::vector<BenchmarkPickIssuedMetadata>& benchmark_pick_issue_metadata;
};

struct RegionStatsCommandContext {
    const gs3d::render::ViewportManager& viewport_manager;
    const gs3d::data::Gs3dDataset& dataset;
    const gs3d::camera::CameraBounds& bounds;
    const std::string& primary_value_name;
    const std::string& z_field_name;
};

} // namespace gs3d::app
