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

class MouseRay {
public:
    [[nodiscard]]
    static Ray from_screen(
        double mouse_x,
        double mouse_y,
        const Viewport& viewport,
        const Camera& camera
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