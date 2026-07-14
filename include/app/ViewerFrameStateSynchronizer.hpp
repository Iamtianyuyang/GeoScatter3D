#pragma once

#include "app/AppState.hpp"
#include "render/PointPipeline.hpp"
#include "scene/SceneState.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace gs3d::app {

struct ViewerFrameTileCacheMetrics {
    std::uint64_t resident_bytes = 0;
    std::uint64_t max_bytes = 0;
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
};

struct ViewerFrameStateMetrics {
    std::uint64_t dataset_point_count = 0;
    std::uint64_t gpu_resident_points = 0;
    std::uint64_t visible_points = 0;
    std::uint64_t gpu_buffer_bytes = 0;
    std::uint32_t loaded_tiles = 0;
    std::uint32_t pending_tiles = 0;
    float fps = 0.0f;
    float frame_time_ms = 0.0f;
    std::string camera_position;
};

// Mirrors render state into the UI model once per frame. Immutable viewer
// policy is captured in the constructor so frame updates do not depend on the
// broad ViewerAppConfig object.
class ViewerFrameStateSynchronizer {
public:
    ViewerFrameStateSynchronizer(
        double dataset_origin_z,
        std::uint32_t resident_tile_budget,
        bool lod_enabled
    ) noexcept;

    void synchronize(
        AppState& state,
        const std::vector<gs3d::render::PointPushConstants>& viewport_pushes,
        const std::vector<gs3d::scene::SceneState>& viewport_scenes,
        const std::vector<float>& viewport_height_exaggerations,
        const ViewerFrameTileCacheMetrics& cache,
        const ViewerFrameStateMetrics& metrics
    ) const;

private:
    double dataset_origin_z_ = 0.0;
    std::uint32_t resident_tile_budget_ = 0;
    bool lod_enabled_ = false;
};

} // namespace gs3d::app
