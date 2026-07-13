#include "app/ViewportLodController.hpp"

#include <algorithm>

namespace gs3d::app {

std::optional<ViewportLodSelection> ViewportLodController::select(
    gs3d::render::LodSelector& selector,
    const std::vector<float>& voxel_sizes,
    const float world_per_pixel,
    const bool interacting,
    const bool allow_coarse_lod,
    const double high_delay_seconds
) noexcept {
    if (voxel_sizes.empty()) {
        return std::nullopt;
    }

    const auto level_count = voxel_sizes.size();
    std::size_t spatial_level = 0;
    if (interacting) {
        if (frozen_spatial_level_ >= level_count) {
            frozen_spatial_level_ =
                gs3d::render::LodSelector::select_level_by_spacing(
                    world_per_pixel,
                    voxel_sizes,
                    last_spatial_level_
                );
        }
        spatial_level = frozen_spatial_level_;
    } else {
        spatial_level =
            gs3d::render::LodSelector::select_level_by_spacing(
                world_per_pixel,
                voxel_sizes,
                last_spatial_level_
            );
        frozen_spatial_level_ = spatial_level;
    }
    last_spatial_level_ = spatial_level;

    const auto temporal_level = selector.select_level(level_count);
    const auto requested_level = std::max(spatial_level, temporal_level);

    ViewportLodSelection selection;
    selection.spatial_level = spatial_level;
    selection.temporal_level = temporal_level;
    selection.requested_level = requested_level;
    selection.fallback_level = last_valid_level_;
    if (requested_level < level_count) {
        selection.level = requested_level;
    } else {
        selection.level = last_valid_level_;
        selection.used_fallback = true;
    }

    if (!allow_coarse_lod) {
        if (!interacting) {
            if (selection.level < frozen_display_level_) {
                frozen_display_level_ = selection.level;
            }
            if (selector.idle_seconds() >= high_delay_seconds) {
                frozen_display_level_ = std::max(
                    frozen_display_level_,
                    spatial_level
                );
            }
        }
        selection.level = std::min(selection.level, frozen_display_level_);
    }

    selection.frozen_display_level = frozen_display_level_;
    last_valid_level_ = selection.level;
    selection.changed = selection.level != last_level_;
    last_level_ = selection.level;
    return selection;
}

} // namespace gs3d::app
