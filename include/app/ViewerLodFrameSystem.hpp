#pragma once

#include "app/ViewportLodController.hpp"

#include <cstddef>

namespace gs3d::camera { class Camera; }
namespace gs3d::render { class LodSelector; }
namespace gs3d::render { class PointCloudLodGpu; }
namespace gs3d::render { class ViewportManager; }

namespace gs3d::app {

struct ViewerLodFrameOptions {
    bool enabled = false;
    bool allow_coarse_while_interacting = false;
    double high_delay_seconds = 0.0;
    bool verbose = false;
};

struct ViewerLodFrameContext {
    const gs3d::render::PointCloudLodGpu* lod_gpu_cloud = nullptr;
    gs3d::render::LodSelector& lod_selector;
    const gs3d::render::ViewportManager& viewport_manager;
    int streaming_viewport_index = 0;
    bool interacting = false;
    ViewerLodFrameOptions options;
};

// Owns the last displayed LOD level and converts a camera viewport into the
// spatial metric consumed by ViewportLodController.
class ViewerLodFrameSystem {
public:
    [[nodiscard]] std::size_t current_level() const noexcept;

    [[nodiscard]] std::size_t update(
        const ViewerLodFrameContext& context
    );

    [[nodiscard]] static float world_per_pixel(
        const gs3d::camera::Camera& camera
    ) noexcept;

private:
    ViewportLodController viewport_lod_;
    std::size_t current_level_ = 0;
};

} // namespace gs3d::app
