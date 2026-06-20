#pragma once

#include "camera/Camera.hpp"

#include <cstdint>
#include <optional>

namespace gs3d::camera {

struct Ray {
    Vec3 origin;
    Vec3 direction;
};

struct Viewport {
    std::uint32_t width = 1;
    std::uint32_t height = 1;
};

struct ScreenPoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct RaySegment {
    Vec3 near_point;
    Vec3 far_point;
};

class MouseRay {
public:
    [[nodiscard]]
    static Ray from_screen(
        double mouse_x,
        double mouse_y,
        const Viewport& viewport,
        const Camera& camera
    ) noexcept;

    /*
     * Inverse of from_screen(): projects a world point to viewport-local
     * screen pixels (top-left origin, y down — same convention as
     * from_screen's mouse_x/mouse_y). Returns nullopt if the point is
     * behind the camera (w <= 0 after projection).
     */
    [[nodiscard]]
    static std::optional<ScreenPoint> to_screen(
        const Vec3& world_point,
        const Viewport& viewport,
        const Camera& camera
    ) noexcept;

    /*
     * Core world→screen projection with a pre-computed view-projection
     * matrix (avoids recomputing camera.view_projection_matrix() in
     * hot loops).  Used by both to_screen() and NearestPointQuery.
     */
    [[nodiscard]]
    static std::optional<ScreenPoint> world_to_screen(
        const Mat4& view_projection,
        float x, float y, float z,
        float viewport_width,
        float viewport_height
    ) noexcept;

    [[nodiscard]]
    static std::optional<Vec3> intersect_plane(
        const Ray& ray,
        const Vec3& plane_point,
        const Vec3& plane_normal
    ) noexcept;

    [[nodiscard]]
    static std::optional<Vec3> intersect_camera_facing_plane(
        double mouse_x,
        double mouse_y,
        const Viewport& viewport,
        const Camera& camera,
        const Vec3& plane_point
    ) noexcept;

private:
    // Unprojects a screen point to its near-plane and far-plane world
    // positions (the two ends of the visible ray segment, bounded by the
    // camera's actual near/far clip planes). from_screen() builds its Ray
    // (origin + normalized direction) from this.
    [[nodiscard]]
    static RaySegment near_far_points(
        double mouse_x,
        double mouse_y,
        const Viewport& viewport,
        const Camera& camera
    ) noexcept;

    [[nodiscard]]
    static Mat4 inverse(const Mat4& matrix) noexcept;

    [[nodiscard]]
    static Vec3 transform_point(
        const Mat4& matrix,
        float x,
        float y,
        float z
    ) noexcept;

    [[nodiscard]]
    static Vec3 add(const Vec3& a, const Vec3& b) noexcept;

    [[nodiscard]]
    static Vec3 sub(const Vec3& a, const Vec3& b) noexcept;

    [[nodiscard]]
    static Vec3 mul(const Vec3& v, float s) noexcept;

    [[nodiscard]]
    static float dot(const Vec3& a, const Vec3& b) noexcept;

    [[nodiscard]]
    static float length(const Vec3& v) noexcept;

    [[nodiscard]]
    static Vec3 normalize(const Vec3& v) noexcept;
};

} // namespace gs3d::camera