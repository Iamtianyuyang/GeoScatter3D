#pragma once

#include "app/ViewerAppGpuPick.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace gs3d::app {
class ViewerBenchmarkController;
struct AppState;
struct UiActions;
}
namespace gs3d::camera { class CameraController; }
namespace gs3d::render { class ViewportManager; }
namespace gs3d::render { struct PointPushConstants; }
namespace gs3d::render { class VulkanContext; }

namespace gs3d::app {

struct ViewerPickState {
    std::vector<GpuPickRequest> requests;
    std::vector<std::optional<gs3d::data::Gs3dPoint>> latest_hover_points;
    std::vector<float> latest_capture_x;
    std::vector<float> latest_capture_y;
    std::vector<int> hover_timeout;
    std::vector<int> consecutive_no_hit;
    std::uint32_t frame_slot = 0;
};

struct ViewerPickLookupContext {
    const std::vector<gs3d::data::Gs3dPoint>& runtime_points_by_id;
    const std::vector<std::uint8_t>& runtime_points_valid_by_id;
};

struct ViewerPickCameraContext {
    std::vector<gs3d::camera::CameraController>& controllers;
    std::vector<std::optional<gs3d::camera::Vec3>>& selected_focus_points;
    gs3d::render::ViewportManager& viewport_manager;
    const gs3d::camera::CameraBounds& bounds;
    const std::vector<gs3d::render::PointPushConstants>& viewport_pushes;
    int& streaming_viewport_index;
    bool& tile_selection_dirty;
};

// Owns GPU pick requests/readback, hover debounce state, and optional debug
// dumps. The application supplies only the current frame's UI and scene data.
class ViewerPickSystem {
public:
    ViewerPickSystem(
        const gs3d::render::VulkanContext& context,
        std::uint32_t frames_in_flight,
        std::size_t viewport_count,
        bool debug_dump_enabled,
        std::filesystem::path debug_dump_dir
    );

    [[nodiscard]] ViewerPickState& state() noexcept;
    [[nodiscard]] const ViewerPickState& state() const noexcept;
    [[nodiscard]] GpuPickReadback& gpu_readback() noexcept;
    [[nodiscard]] PickDebugFrameDumper& debug_frame_dumper() noexcept;
    [[nodiscard]] std::vector<bool>& pending_hover_miss_dump() noexcept;
    [[nodiscard]] std::uint64_t& debug_dump_count() noexcept;
    [[nodiscard]] bool& debug_dump_completed() noexcept;

    void prepare_requests(
        const gs3d::render::ViewportManager& viewport_manager,
        AppState& app_state,
        const UiActions& actions,
        const std::vector<float>& viewport_point_sizes,
        ViewerBenchmarkController& benchmark_controller
    );

    void consume_ready_frame(
        std::uint32_t frame_slot,
        const ViewerPickLookupContext& lookup,
        ViewerPickCameraContext& camera,
        ViewerBenchmarkController& benchmark_controller,
        const VisibleTilePickResolver& resolve_hover_point_from_visible_tiles
    );

private:
    GpuPickReadback gpu_readback_;
    PickDebugFrameDumper debug_frame_dumper_;
    ViewerPickState state_;
    bool debug_dump_enabled_ = false;
    std::filesystem::path debug_dump_dir_;
    std::vector<bool> pending_hover_miss_dump_;
    std::uint64_t debug_dump_count_ = 0;
    bool debug_dump_completed_ = false;
};

} // namespace gs3d::app
