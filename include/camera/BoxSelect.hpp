#pragma once

#include "camera/Camera.hpp"
#include "camera/MouseRay.hpp"

#include <optional>

namespace gs3d::camera {

/*
 * Box-select-to-zoom: unprojects the screen rectangle's 4 corners onto a
 * horizontal reference plane at `plane_height_z` and returns the resulting
 * world-space bounding box. `plane_height_z` must be the REAL surface
 * height under the selection (e.g. from a nearest-point query at the
 * rect's center) — for this viewer's height-field-style 2.5D data (real
 * elevation relief can span ~half the X/Y extent), using the camera orbit
 * target's Z instead of the actual local terrain height was the original
 * bug: the selection plane could sit thousands of units away from the
 * surface actually under the dragged rectangle, so the camera would zoom
 * to a world location far from what was visually selected on screen.
 *
 * At grazing/near-horizontal pitch the ray-to-horizontal-plane
 * intersection degenerates, so this falls back to a plane facing the
 * camera (normal = view direction, anchored at the same height) when any
 * corner fails to hit the horizontal plane.
 *
 * The result is finally clamped (per-axis, non-destructively — an axis is
 * left unclamped if clamping would invert min/max) to `scene_bounds`, as
 * a safety net against the plane assumption producing a box that spills
 * outside the actual data.
 *
 * Returns nullopt if both the horizontal-plane and fallback attempts
 * fail, or the resulting box is degenerate.
 */
[[nodiscard]]
std::optional<CameraBounds> box_select_world_bounds(
    float screen_min_x,
    float screen_min_y,
    float screen_max_x,
    float screen_max_y,
    const Viewport& viewport,
    const Camera& camera,
    const CameraBounds& scene_bounds,
    float plane_height_z
) noexcept;

} // namespace gs3d::camera
