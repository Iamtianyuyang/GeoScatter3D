#include "camera/MouseRay.hpp"

#include <algorithm>
#include <cmath>

namespace gs3d::camera {

Ray MouseRay::from_screen(
    double mouse_x,
    double mouse_y,
    const Viewport& viewport,
    const Camera& camera
) noexcept {
    const RaySegment segment =
        near_far_points(mouse_x, mouse_y, viewport, camera);

    Ray ray{};
    ray.origin = segment.near_point;
    ray.direction = normalize(sub(segment.far_point, segment.near_point));

    return ray;
}

RaySegment MouseRay::near_far_points(
    double mouse_x,
    double mouse_y,
    const Viewport& viewport,
    const Camera& camera
) noexcept {
    const double width =
        static_cast<double>(std::max<std::uint32_t>(viewport.width, 1));

    const double height =
        static_cast<double>(std::max<std::uint32_t>(viewport.height, 1));

    /*
     * 屏幕坐标：
     *   x: [0, width]
     *   y: [0, height]，窗口顶部为 0
     *
     * Vulkan NDC (with Y-flipped projection):
     *   x: [-1, 1]
     *   y: [-1, 1]  (−1 = top, +1 = bottom)
     *   z: [0, 1]
     */
    const float ndc_x =
        static_cast<float>((2.0 * mouse_x) / width - 1.0);

    const float ndc_y =
        static_cast<float>((2.0 * mouse_y) / height - 1.0);

    const Mat4 inv_vp =
        inverse(camera.view_projection_matrix());

    return RaySegment{
        transform_point(inv_vp, ndc_x, ndc_y, 0.0f),
        transform_point(inv_vp, ndc_x, ndc_y, 1.0f)
    };
}

std::optional<ScreenPoint> MouseRay::to_screen(
    const Vec3& world_point,
    const Viewport& viewport,
    const Camera& camera
) noexcept {
    return world_to_screen(
        camera.view_projection_matrix(),
        world_point.x,
        world_point.y,
        world_point.z,
        static_cast<float>(std::max<std::uint32_t>(viewport.width, 1)),
        static_cast<float>(std::max<std::uint32_t>(viewport.height, 1))
    );
}

std::optional<ScreenPoint> MouseRay::world_to_screen(
    const Mat4& view_projection,
    float x, float y, float z,
    float width,
    float height
) noexcept {
    const float* m = view_projection.m.data();

    const float clip_x =
        m[0] * x + m[4] * y + m[8]  * z + m[12];
    const float clip_y =
        m[1] * x + m[5] * y + m[9]  * z + m[13];
    const float clip_w =
        m[3] * x + m[7] * y + m[11] * z + m[15];

    if (clip_w <= 1.0e-6f) {
        return std::nullopt;
    }

    const float ndc_x = clip_x / clip_w;
    const float ndc_y = clip_y / clip_w;

    return ScreenPoint{
        (ndc_x + 1.0f) * 0.5f * width,
        (ndc_y + 1.0f) * 0.5f * height
    };
}

std::optional<Vec3> MouseRay::intersect_plane(
    const Ray& ray,
    const Vec3& plane_point,
    const Vec3& plane_normal
) noexcept {
    const Vec3 n = normalize(plane_normal);

    const float denom = dot(n, ray.direction);

    if (std::abs(denom) < 1.0e-6f) {
        return std::nullopt;
    }

    const float t =
        dot(n, sub(plane_point, ray.origin)) / denom;

    if (t < 0.0f) {
        return std::nullopt;
    }

    return add(ray.origin, mul(ray.direction, t));
}

std::optional<Vec3> MouseRay::intersect_camera_facing_plane(
    double mouse_x,
    double mouse_y,
    const Viewport& viewport,
    const Camera& camera,
    const Vec3& plane_point
) noexcept {
    const Ray ray =
        from_screen(mouse_x, mouse_y, viewport, camera);

    const Vec3 camera_forward =
        normalize(sub(camera.target(), camera.position()));

    return intersect_plane(
        ray,
        plane_point,
        camera_forward
    );
}

Mat4 MouseRay::inverse(const Mat4& matrix) noexcept {
    const float* m = matrix.m.data();

    Mat4 inv{};

    inv.m[0] =
        m[5]  * m[10] * m[15] -
        m[5]  * m[11] * m[14] -
        m[9]  * m[6]  * m[15] +
        m[9]  * m[7]  * m[14] +
        m[13] * m[6]  * m[11] -
        m[13] * m[7]  * m[10];

    inv.m[4] =
       -m[4]  * m[10] * m[15] +
        m[4]  * m[11] * m[14] +
        m[8]  * m[6]  * m[15] -
        m[8]  * m[7]  * m[14] -
        m[12] * m[6]  * m[11] +
        m[12] * m[7]  * m[10];

    inv.m[8] =
        m[4]  * m[9] * m[15] -
        m[4]  * m[11] * m[13] -
        m[8]  * m[5] * m[15] +
        m[8]  * m[7] * m[13] +
        m[12] * m[5] * m[11] -
        m[12] * m[7] * m[9];

    inv.m[12] =
       -m[4]  * m[9] * m[14] +
        m[4]  * m[10] * m[13] +
        m[8]  * m[5] * m[14] -
        m[8]  * m[6] * m[13] -
        m[12] * m[5] * m[10] +
        m[12] * m[6] * m[9];

    inv.m[1] =
       -m[1]  * m[10] * m[15] +
        m[1]  * m[11] * m[14] +
        m[9]  * m[2] * m[15] -
        m[9]  * m[3] * m[14] -
        m[13] * m[2] * m[11] +
        m[13] * m[3] * m[10];

    inv.m[5] =
        m[0]  * m[10] * m[15] -
        m[0]  * m[11] * m[14] -
        m[8]  * m[2] * m[15] +
        m[8]  * m[3] * m[14] +
        m[12] * m[2] * m[11] -
        m[12] * m[3] * m[10];

    inv.m[9] =
       -m[0]  * m[9] * m[15] +
        m[0]  * m[11] * m[13] +
        m[8]  * m[1] * m[15] -
        m[8]  * m[3] * m[13] -
        m[12] * m[1] * m[11] +
        m[12] * m[3] * m[9];

    inv.m[13] =
        m[0]  * m[9] * m[14] -
        m[0]  * m[10] * m[13] -
        m[8]  * m[1] * m[14] +
        m[8]  * m[2] * m[13] +
        m[12] * m[1] * m[10] -
        m[12] * m[2] * m[9];

    inv.m[2] =
        m[1]  * m[6] * m[15] -
        m[1]  * m[7] * m[14] -
        m[5]  * m[2] * m[15] +
        m[5]  * m[3] * m[14] +
        m[13] * m[2] * m[7] -
        m[13] * m[3] * m[6];

    inv.m[6] =
       -m[0]  * m[6] * m[15] +
        m[0]  * m[7] * m[14] +
        m[4]  * m[2] * m[15] -
        m[4]  * m[3] * m[14] -
        m[12] * m[2] * m[7] +
        m[12] * m[3] * m[6];

    inv.m[10] =
        m[0]  * m[5] * m[15] -
        m[0]  * m[7] * m[13] -
        m[4]  * m[1] * m[15] +
        m[4]  * m[3] * m[13] +
        m[12] * m[1] * m[7] -
        m[12] * m[3] * m[5];

    inv.m[14] =
       -m[0]  * m[5] * m[14] +
        m[0]  * m[6] * m[13] +
        m[4]  * m[1] * m[14] -
        m[4]  * m[2] * m[13] -
        m[12] * m[1] * m[6] +
        m[12] * m[2] * m[5];

    inv.m[3] =
       -m[1] * m[6] * m[11] +
        m[1] * m[7] * m[10] +
        m[5] * m[2] * m[11] -
        m[5] * m[3] * m[10] -
        m[9] * m[2] * m[7] +
        m[9] * m[3] * m[6];

    inv.m[7] =
        m[0] * m[6] * m[11] -
        m[0] * m[7] * m[10] -
        m[4] * m[2] * m[11] +
        m[4] * m[3] * m[10] +
        m[8] * m[2] * m[7] -
        m[8] * m[3] * m[6];

    inv.m[11] =
       -m[0] * m[5] * m[11] +
        m[0] * m[7] * m[9] +
        m[4] * m[1] * m[11] -
        m[4] * m[3] * m[9] -
        m[8] * m[1] * m[7] +
        m[8] * m[3] * m[5];

    inv.m[15] =
        m[0] * m[5] * m[10] -
        m[0] * m[6] * m[9] -
        m[4] * m[1] * m[10] +
        m[4] * m[2] * m[9] +
        m[8] * m[1] * m[6] -
        m[8] * m[2] * m[5];

    const float det =
        m[0] * inv.m[0] +
        m[1] * inv.m[4] +
        m[2] * inv.m[8] +
        m[3] * inv.m[12];

    if (std::abs(det) < 1.0e-12f) {
        return Mat4::identity();
    }

    const float inv_det = 1.0f / det;

    for (float& value : inv.m) {
        value *= inv_det;
    }

    return inv;
}

Vec3 MouseRay::transform_point(
    const Mat4& matrix,
    float x,
    float y,
    float z
) noexcept {
    const float* m = matrix.m.data();

    const float rx =
        m[0] * x + m[4] * y + m[8]  * z + m[12];

    const float ry =
        m[1] * x + m[5] * y + m[9]  * z + m[13];

    const float rz =
        m[2] * x + m[6] * y + m[10] * z + m[14];

    const float rw =
        m[3] * x + m[7] * y + m[11] * z + m[15];

    if (std::abs(rw) < 1.0e-8f) {
        return {rx, ry, rz};
    }

    return {
        rx / rw,
        ry / rw,
        rz / rw
    };
}

Vec3 MouseRay::add(const Vec3& a, const Vec3& b) noexcept {
    return {
        a.x + b.x,
        a.y + b.y,
        a.z + b.z
    };
}

Vec3 MouseRay::sub(const Vec3& a, const Vec3& b) noexcept {
    return {
        a.x - b.x,
        a.y - b.y,
        a.z - b.z
    };
}

Vec3 MouseRay::mul(const Vec3& v, float s) noexcept {
    return {
        v.x * s,
        v.y * s,
        v.z * s
    };
}

float MouseRay::dot(const Vec3& a, const Vec3& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

float MouseRay::length(const Vec3& v) noexcept {
    return std::sqrt(dot(v, v));
}

Vec3 MouseRay::normalize(const Vec3& v) noexcept {
    const float len = length(v);

    if (len <= 1.0e-12f) {
        return {0.0f, 0.0f, 0.0f};
    }

    return mul(v, 1.0f / len);
}

} // namespace gs3d::camera