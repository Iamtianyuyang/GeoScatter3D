#include "camera/BoxSelect.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace gs3d::camera {

namespace {

[[nodiscard]]
Vec3 sub(const Vec3& a, const Vec3& b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]]
float length(const Vec3& v) noexcept {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

[[nodiscard]]
Vec3 normalize(const Vec3& v) noexcept {
    const float len = length(v);
    if (len <= 1.0e-8f) {
        return {0.0f, 0.0f, 0.0f};
    }
    return {v.x / len, v.y / len, v.z / len};
}

// Tries to intersect all 4 screen-space corners with a single plane.
// Returns nullopt as soon as any corner misses (keeps the 4 hits on a
// consistent plane rather than mixing planes per corner).
[[nodiscard]]
std::optional<std::array<Vec3, 4>> intersect_all_corners(
    const std::array<std::pair<double, double>, 4>& corners,
    const Viewport& viewport,
    const Camera& camera,
    const Vec3& plane_point,
    const Vec3& plane_normal
) noexcept {
    std::array<Vec3, 4> hits;
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const auto& [x, y] = corners[i];
        const Ray ray = MouseRay::from_screen(x, y, viewport, camera);
        const auto hit = MouseRay::intersect_plane(ray, plane_point, plane_normal);
        if (!hit) {
            return std::nullopt;
        }
        hits[i] = *hit;
    }
    return hits;
}

// Clamps `value` into [lo, hi] only if lo <= hi; otherwise leaves it
// unclamped (a degenerate/inverted scene_bounds axis must not destroy an
// otherwise-valid selection).
[[nodiscard]]
float clamp_if_valid(float value, float lo, float hi) noexcept {
    if (lo > hi) {
        return value;
    }
    return std::clamp(value, lo, hi);
}

} // namespace

std::optional<CameraBounds> box_select_world_bounds(
    float screen_min_x,
    float screen_min_y,
    float screen_max_x,
    float screen_max_y,
    const Viewport& viewport,
    const Camera& camera,
    const CameraBounds& scene_bounds,
    float plane_height_z
) noexcept {
    const Vec3 plane_point{0.0f, 0.0f, plane_height_z};

    const std::array<std::pair<double, double>, 4> corners = {{
        {static_cast<double>(screen_min_x), static_cast<double>(screen_min_y)},
        {static_cast<double>(screen_max_x), static_cast<double>(screen_min_y)},
        {static_cast<double>(screen_min_x), static_cast<double>(screen_max_y)},
        {static_cast<double>(screen_max_x), static_cast<double>(screen_max_y)},
    }};

    auto hits = intersect_all_corners(
        corners, viewport, camera, plane_point, {0.0f, 0.0f, 1.0f}
    );
    if (!hits) {
        const Vec3 forward = normalize(sub(camera.target(), camera.position()));
        if (length(forward) > 1.0e-6f) {
            hits = intersect_all_corners(
                corners, viewport, camera, plane_point, forward
            );
        }
    }
    if (!hits) {
        return std::nullopt;
    }

    float min_x = std::numeric_limits<float>::max();
    float min_y = std::numeric_limits<float>::max();
    float min_z = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float max_y = std::numeric_limits<float>::lowest();
    float max_z = std::numeric_limits<float>::lowest();

    for (const auto& hit : *hits) {
        min_x = std::min(min_x, hit.x);
        min_y = std::min(min_y, hit.y);
        min_z = std::min(min_z, hit.z);
        max_x = std::max(max_x, hit.x);
        max_y = std::max(max_y, hit.y);
        max_z = std::max(max_z, hit.z);
    }

    if (max_x <= min_x || max_y <= min_y) {
        return std::nullopt;
    }

    // Horizontal-plane hits all share z == plane_height_z (zero thickness);
    // the camera-facing fallback's 4 corners do have real z spread already.
    // Either way, give the result a sane minimum vertical thickness so
    // fit_bounds() never receives a zero-thickness slab.
    const float vertical_margin =
        std::max(1.0f, 0.1f * std::max(max_x - min_x, max_y - min_y));

    CameraBounds bounds;
    if (max_z - min_z < vertical_margin) {
        const float center_z = 0.5f * (min_z + max_z);
        bounds.min = {min_x, min_y, center_z - vertical_margin};
        bounds.max = {max_x, max_y, center_z + vertical_margin};
    } else {
        bounds.min = {min_x, min_y, min_z};
        bounds.max = {max_x, max_y, max_z};
    }

    // Safety net: keep the result from spilling outside the real scene,
    // without letting a degenerate scene_bounds destroy a valid selection.
    bounds.min.x = clamp_if_valid(bounds.min.x, scene_bounds.min.x, scene_bounds.max.x);
    bounds.max.x = clamp_if_valid(bounds.max.x, scene_bounds.min.x, scene_bounds.max.x);
    bounds.min.y = clamp_if_valid(bounds.min.y, scene_bounds.min.y, scene_bounds.max.y);
    bounds.max.y = clamp_if_valid(bounds.max.y, scene_bounds.min.y, scene_bounds.max.y);
    bounds.min.z = clamp_if_valid(bounds.min.z, scene_bounds.min.z, scene_bounds.max.z);
    bounds.max.z = clamp_if_valid(bounds.max.z, scene_bounds.min.z, scene_bounds.max.z);

    return bounds;
}

} // namespace gs3d::camera
