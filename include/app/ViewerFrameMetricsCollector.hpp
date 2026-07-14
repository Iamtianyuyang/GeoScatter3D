#pragma once

#include "app/ViewerFrameStateSynchronizer.hpp"

#include <cstdint>
#include <string>

namespace gs3d::app { struct ViewerAppTileStreamState; }
namespace gs3d::data { class Gs3dTileReader; }
namespace gs3d::render { class PointCloudGpu; }
namespace gs3d::render { class PointCloudLodGpu; }
namespace gs3d::render { class PointCloudTileGpu; }
namespace gs3d::render { struct TileSelectionResult; }

namespace gs3d::app {

struct ViewerFrameMetricsContext {
    bool tile_enabled = false;
    const gs3d::render::PointCloudTileGpu* tile_gpu_cloud = nullptr;
    const gs3d::render::PointCloudGpu* full_gpu_cloud = nullptr;
    const gs3d::render::PointCloudLodGpu* lod_gpu_cloud = nullptr;
    const ViewerAppTileStreamState& tile_stream;
    const gs3d::render::TileSelectionResult& tile_result;
    const gs3d::data::Gs3dTileReader* tile_reader = nullptr;
    std::uint64_t dataset_point_count = 0;
    float fps = 0.0f;
    double delta_seconds = 0.0;
    std::string camera_position;
};

struct ViewerFrameMetricsSnapshot {
    ViewerFrameTileCacheMetrics tile_cache;
    ViewerFrameStateMetrics frame_state;
};

// Collects telemetry from GPU residency, tile streaming, and the current
// frame clock. It returns values only; ViewerFrameStateSynchronizer remains
// responsible for copying those values into AppState for the UI.
class ViewerFrameMetricsCollector {
public:
    [[nodiscard]] ViewerFrameMetricsSnapshot collect(
        const ViewerFrameMetricsContext& context
    ) const;
};

} // namespace gs3d::app
