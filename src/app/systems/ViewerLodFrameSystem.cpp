#include "app/ViewerLodFrameSystem.hpp"

#include "app/ViewportLodController.hpp"
#include "camera/Camera.hpp"
#include "render/LodSelector.hpp"
#include "render/PointCloudLodGpu.hpp"
#include "render/ViewportManager.hpp"
#include "util/Log.hpp"

#include <cstdlib>

namespace gs3d::app {

std::size_t ViewerLodFrameSystem::current_level() const noexcept
{
    return current_level_;
}

float ViewerLodFrameSystem::world_per_pixel(
    const gs3d::camera::Camera& camera
) noexcept {
    const float viewport_height = static_cast<float>(camera.viewport_height());
    return viewport_height > 0.0f
        ? camera.ortho_height() / viewport_height
        : 0.0f;
}

std::size_t ViewerLodFrameSystem::update(
    const ViewerLodFrameContext& context
) {
    if (!context.options.enabled || context.lod_gpu_cloud == nullptr) {
        return current_level_;
    }

    const auto voxel_sizes = context.lod_gpu_cloud->voxel_sizes();
    const auto& camera = context.viewport_manager.camera(
        context.streaming_viewport_index
    );
    const float spatial_ortho_height = camera.ortho_height();
    const float world_units_per_pixel = world_per_pixel(camera);
    const auto selection = viewport_lod_.select(
        context.lod_selector,
        voxel_sizes,
        world_units_per_pixel,
        context.interacting,
        context.options.allow_coarse_while_interacting,
        context.options.high_delay_seconds
    );
    if (!selection.has_value()) {
        return current_level_;
    }

    current_level_ = selection->level;
    if (selection->used_fallback) {
        gs3d::util::log::warning()
            << "[WARN] LOD level out of range: "
            << selection->requested_level
            << " >= " << voxel_sizes.size()
            << ", falling back to "
            << selection->fallback_level << '\n';
    }

    if (!selection->changed) {
        return current_level_;
    }

    if (context.options.verbose) {
        const auto& level = context.lod_gpu_cloud->level(current_level_);
        gs3d::util::log::info() << "[LOD] active level = "
            << current_level_
            << ", points = " << level.gpu_point_count
            << ", idle_seconds = " << context.lod_selector.idle_seconds()
            << ", ortho_h = " << world_units_per_pixel
            << " m/px\n";
    }

    if (const char* environment = std::getenv("GS3D_LOD_DEBUG");
        environment != nullptr && environment[0] == '1') {
        gs3d::util::log::info()
            << "[LODDBG] ortho_h=" << spatial_ortho_height
            << " wpix=" << world_units_per_pixel
            << " spatial=" << selection->spatial_level
            << (context.interacting ? "(frozen)" : "")
            << " temporal=" << selection->temporal_level
            << " -> level=" << current_level_
            << " frozen=" << selection->frozen_display_level
            << (context.interacting ? " (interacting)\n" : " (idle)\n");
    }

    return current_level_;
}

} // namespace gs3d::app
