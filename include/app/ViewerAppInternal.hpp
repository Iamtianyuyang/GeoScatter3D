#pragma once

#include "app/AppState.hpp"
#include "camera/Camera.hpp"
#include "data/Gs3dFormat.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace gs3d::app {

class GpuPickReadback;
class PickDebugFrameDumper;

constexpr int kMaxViewportCount = 4;

constexpr std::uint32_t kDefaultGpuPickRadiusPx = 3;
constexpr std::uint32_t kMaxGpuPickRadiusPx = 5;
constexpr int kNoHitClearThreshold = 3;

enum class GpuPickRequestKind {
    None,
    Hover,
    SetOrbitPivot,
    BoxSelectAnchor
};

struct GpuPickRequest {
    int viewport_index = -1;
    GpuPickRequestKind kind = GpuPickRequestKind::None;
    int benchmark_query_index = -1;
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    std::uint32_t pick_radius_px = kDefaultGpuPickRadiusPx;
    std::uint32_t viewport_width = 0;
    std::uint32_t viewport_height = 0;
    float box_select_min_x = 0.0f;
    float box_select_min_y = 0.0f;
    float box_select_max_x = 0.0f;
    float box_select_max_y = 0.0f;
    gs3d::camera::Camera anchor_camera{};

    [[nodiscard]]
    bool valid() const noexcept {
        return kind != GpuPickRequestKind::None &&
               viewport_index >= 0 &&
               viewport_width > 0 &&
               viewport_height > 0;
    }
};

struct GpuPickResult {
    GpuPickRequest request{};
    std::uint32_t point_id = 0;
    float depth = 1.0f;
    bool has_hit = false;
};

struct ResolvedPickPoint {
    std::optional<gs3d::data::Gs3dPoint> point;
    bool via_runtime_lookup = false;
};

struct BenchmarkPickScriptQuery {
    std::size_t query_index = 0;
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
};

struct BenchmarkPickObservedResult {
    std::size_t query_index = 0;
    bool has_hit = false;
    bool gpu_has_hit = false;
    bool all_tiles_resident = false;
    std::uint32_t point_id = 0;
    float depth = 1.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float value = 0.0f;
    double issue_cpu_ms = 0.0;
    double collect_cpu_ms = 0.0;
    std::vector<std::uint64_t> resident_tile_ids{};
};

struct BenchmarkPickIssuedMetadata {
    bool all_tiles_resident = false;
    std::vector<std::uint64_t> resident_tile_ids{};
};

struct PickDebugDumpMetadata {
    std::uint64_t dump_index = 0;
    std::uint64_t frame_index = 0;
    int viewport_index = -1;
    std::uint32_t viewport_width = 0;
    std::uint32_t viewport_height = 0;
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    std::uint32_t sample_left = 0;
    std::uint32_t sample_top = 0;
    std::uint32_t sample_width = 0;
    std::uint32_t sample_height = 0;
    std::size_t active_lod_level = 0;
    bool tile_overlay_rendered = false;
    bool all_tiles_resident = false;
    std::string render_source{};
    std::string trigger_reason{};
    std::vector<std::uint64_t> selected_tile_ids{};
    std::vector<std::uint64_t> resident_tile_ids{};
};

struct PickDebugDumpFrame {
    PickDebugDumpMetadata metadata{};
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> color_pixels{};
    std::vector<std::uint32_t> pick_ids{};
};

void write_pick_debug_dump(
    const std::filesystem::path& output_dir,
    const PickDebugDumpFrame& dump
);

using VisibleTilePickResolver = std::function<
    std::optional<gs3d::data::Gs3dPoint>(std::size_t, std::uint32_t, float, float)
>;

[[nodiscard]]
std::uint32_t compute_hover_pick_radius_px(float point_size) noexcept;

[[nodiscard]]
ResolvedPickPoint resolve_pick_point(
    const GpuPickResult& result,
    const std::vector<gs3d::data::Gs3dPoint>& points_by_id,
    const std::vector<std::uint8_t>& valid_by_id
);

void compute_gizmo_axes(
    gs3d::app::RenderViewState& view,
    const gs3d::camera::Camera& camera
);

void compute_axis_overlay(
    gs3d::app::RenderViewState& view,
    const gs3d::camera::CameraBounds& bounds,
    const gs3d::camera::Camera& camera,
    double origin_x,
    double origin_y,
    double origin_z,
    float  z_label_mult        = 1.0f,
    float  z_label_offset      = 0.0f,
    bool   z_axis_add_origin_z = false
);

void compute_map_axis_overlay(
    gs3d::app::RenderViewState& view,
    const gs3d::camera::Camera& camera,
    double origin_x,
    double origin_y
);

} // namespace gs3d::app
