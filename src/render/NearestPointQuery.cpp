#include "render/NearestPointQuery.hpp"

#include <cmath>
#include <limits>

namespace gs3d::render {

namespace {

// Thin wrapper that delegates to the shared MouseRay::world_to_screen,
// keeping the hot loop simple (pre-computed VP matrix, float args).
inline std::optional<gs3d::camera::ScreenPoint> project_to_screen(
    const gs3d::camera::Mat4& view_projection,
    const gs3d::data::Gs3dPoint& point,
    float width,
    float height
) noexcept {
    return gs3d::camera::MouseRay::world_to_screen(
        view_projection,
        point.x, point.y, point.z,
        width, height
    );
}

} // namespace

std::optional<NearestPointResult> find_nearest_point_on_screen(
    const std::vector<const std::vector<gs3d::data::Gs3dPoint>*>&
        candidate_point_sets,
    float mouse_x,
    float mouse_y,
    const gs3d::camera::Viewport& viewport,
    const gs3d::camera::Camera& camera,
    float max_screen_distance_px
) noexcept {
    const float width =
        static_cast<float>(viewport.width > 0 ? viewport.width : 1);
    const float height =
        static_cast<float>(viewport.height > 0 ? viewport.height : 1);

    const auto view_projection = camera.view_projection_matrix();
    const float max_distance_sq =
        max_screen_distance_px * max_screen_distance_px;

    float best_distance_sq = std::numeric_limits<float>::max();
    std::optional<NearestPointResult> best;

    for (const auto* points : candidate_point_sets) {
        if (points == nullptr) {
            continue;
        }

        for (const auto& point : *points) {
            const auto screen =
                project_to_screen(view_projection, point, width, height);
            if (!screen) {
                continue;
            }

            const float dx = screen->x - mouse_x;
            const float dy = screen->y - mouse_y;
            const float distance_sq = dx * dx + dy * dy;

            if (distance_sq < best_distance_sq &&
                distance_sq <= max_distance_sq) {
                best_distance_sq = distance_sq;
                best = NearestPointResult{
                    point,
                    std::sqrt(distance_sq)
                };
            }
        }
    }

    return best;
}

} // namespace gs3d::render
