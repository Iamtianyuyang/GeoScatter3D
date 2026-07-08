#pragma once

#include "app/ViewerAppInternal.hpp"
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

/*
 * Per-frame benchmark timing samples collected across the run and
 * summarized as percentiles by ViewerApp::print_benchmark_report().
 */
struct ViewerAppBenchmarkFrameSamples {
    std::vector<double> wall_frame_times_ms;
    std::vector<double> cpu_frame_times_ms;
    std::vector<double> gpu_frame_times_ms;
    std::vector<double> camera_update_ms;
    std::vector<double> lod_tile_select_ms;
    std::vector<double> cpu_cull_ms;
    std::vector<double> upload_record_ms;
    std::vector<double> draw_record_ms;
    std::vector<double> acquire_wait_ms;
    std::vector<double> frame_fence_wait_ms;
    std::vector<double> upload_fence_wait_ms;
    std::vector<double> reload_seconds;

    void reserve_frames(std::size_t frame_count) {
        wall_frame_times_ms.reserve(frame_count);
        cpu_frame_times_ms.reserve(frame_count);
        gpu_frame_times_ms.reserve(frame_count);
        camera_update_ms.reserve(frame_count);
        lod_tile_select_ms.reserve(frame_count);
        cpu_cull_ms.reserve(frame_count);
        upload_record_ms.reserve(frame_count);
        draw_record_ms.reserve(frame_count);
        acquire_wait_ms.reserve(frame_count);
        frame_fence_wait_ms.reserve(frame_count);
        upload_fence_wait_ms.reserve(frame_count);
    }
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

/*
 * Screenshot capture round-trip state: staging buffer allocated on
 * demand in the post-pass copy, read back and freed after draw_frame.
 * Only one screenshot is in flight at a time.
 */
struct ViewerAppScreenshotCaptureState {
    VkBuffer staging_buf = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    VkExtent2D offset{};
    VkExtent2D extent{};
    bool pending = false;
};

/*
 * Fixed inputs for rendering the navigation-map thumbnail: the point
 * pipeline, the cloud drawn into the thumbnail (lowest LOD level, or
 * the full cloud when LOD is disabled), the live push-constant
 * template (colormap / clip flags follow the main view), and the
 * dataset's max Z for placing the top-down ortho camera.
 */
struct ViewerAppNavThumbnailContext {
    gs3d::render::PointPipeline& point_pipeline;
    const gs3d::render::PointCloudGpu& nav_cloud;
    const gs3d::render::PointPushConstants& push;
    float dataset_bbox_max_z = 0.0f;
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
    const gs3d::render::PointPushConstants& push;
    // Exactly one of lod_gpu_cloud / full_gpu_cloud is non-null.
    const gs3d::render::PointCloudLodGpu* lod_gpu_cloud = nullptr;
    const gs3d::render::PointCloudGpu* full_gpu_cloud = nullptr;
    // nullptr when tile mode is disabled.
    const gs3d::render::PointCloudTileGpu* tile_gpu_cloud = nullptr;
    const ViewerAppTileStreamState& tile_stream;
    const gs3d::render::TileSelectionResult& tile_result;
    const ViewerAppPickState& pick;
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
