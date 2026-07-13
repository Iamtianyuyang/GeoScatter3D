#pragma once

#include "render/LodSelector.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace gs3d::app {

/*
 * Keeps the stateful policy that combines the spatial LOD target with the
 * frame-time target.  Keeping it outside ViewerApp makes the interaction
 * freeze and high-quality floor independently testable.
 */
struct ViewportLodSelection {
    std::size_t level = 0;
    std::size_t spatial_level = 0;
    std::size_t temporal_level = 0;
    std::size_t requested_level = 0;
    std::size_t fallback_level = 0;
    std::size_t frozen_display_level = 0;
    bool changed = false;
    bool used_fallback = false;
};

class ViewportLodController {
public:
    [[nodiscard]] std::optional<ViewportLodSelection> select(
        gs3d::render::LodSelector& selector,
        const std::vector<float>& voxel_sizes,
        float world_per_pixel,
        bool interacting,
        bool allow_coarse_lod,
        double high_delay_seconds
    ) noexcept;

private:
    std::size_t last_level_ = static_cast<std::size_t>(-1);
    std::size_t last_valid_level_ = 0;
    std::size_t frozen_display_level_ = 0;
    std::size_t last_spatial_level_ = static_cast<std::size_t>(-1);
    std::size_t frozen_spatial_level_ = static_cast<std::size_t>(-1);
};

} // namespace gs3d::app
