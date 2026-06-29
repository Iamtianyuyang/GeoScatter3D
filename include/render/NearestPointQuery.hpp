#pragma once

#include "core/PointData.hpp"
#include "camera/Camera.hpp"
#include "camera/MouseRay.hpp"

#include <optional>
#include <vector>

namespace gs3d::render {

struct NearestPointResult {
    gs3d::core::PointRecord point;
    float screen_distance_px = 0.0f;
};

/*
 * Finds the point — across all given candidate point sets — whose screen
 * projection is closest to (mouse_x, mouse_y), within max_screen_distance_px.
 * Points behind the camera (negative w after projection) are skipped.
 *
 * Intended to run only against currently CPU-resident point data (loaded
 * tiles, or the active LOD level's points) — never the full dataset — so
 * cost stays bounded by what's actually resident, not total point count.
 */
[[nodiscard]]
std::optional<NearestPointResult> find_nearest_point_on_screen(
    const std::vector<gs3d::core::PointDataView>& candidate_point_sets,
    float mouse_x,
    float mouse_y,
    const gs3d::camera::Viewport& viewport,
    const gs3d::camera::Camera& camera,
    float max_screen_distance_px = 12.0f
) noexcept;

} // namespace gs3d::render
