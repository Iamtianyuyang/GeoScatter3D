#include "camera/Camera.hpp"

#include <algorithm>
#include <cmath>

namespace gs3d::camera {

namespace {

constexpr float PI = 3.14159265358979323846f;
constexpr float MIN_DISTANCE = 1.0e-4f;
constexpr float MIN_PITCH_DOT = 0.98f;

[[nodiscard]]
float to_radians(float degrees) noexcept {
    return degrees * PI / 180.0f;
}

[[nodiscard]]
Vec3 add(const Vec3& a, const Vec3& b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]]
Vec3 sub(const Vec3& a, const Vec3& b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]]
Vec3 mul(const Vec3& v, float s) noexcept {
    return {v.x * s, v.y * s, v.z * s};
}

[[nodiscard]]
float dot(const Vec3& a, const Vec3& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]]
Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

[[nodiscard]]
float length(const Vec3& v) noexcept {
    return std::sqrt(dot(v, v));
}

[[nodiscard]]
Vec3 normalize(const Vec3& v) noexcept {
    const float len = length(v);
    if (len <= 0.0f) {
        return {0.0f, 0.0f, 0.0f};
    }

    return mul(v, 1.0f / len);
}

[[nodiscard]]
Mat4 multiply(const Mat4& a, const Mat4& b) noexcept {
    Mat4 out{};

    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;

            for (int k = 0; k < 4; ++k) {
                sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            }

            out.m[col * 4 + row] = sum;
        }
    }

    return out;
}

[[nodiscard]]
Mat4 look_at_matrix(
    const Vec3& eye,
    const Vec3& center,
    const Vec3& up
) noexcept {
    const Vec3 f = normalize(sub(center, eye));
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);

    Mat4 out = Mat4::identity();

    out.m[0] = s.x;
    out.m[1] = u.x;
    out.m[2] = -f.x;
    out.m[3] = 0.0f;

    out.m[4] = s.y;
    out.m[5] = u.y;
    out.m[6] = -f.y;
    out.m[7] = 0.0f;

    out.m[8] = s.z;
    out.m[9] = u.z;
    out.m[10] = -f.z;
    out.m[11] = 0.0f;

    out.m[12] = -dot(s, eye);
    out.m[13] = -dot(u, eye);
    out.m[14] = dot(f, eye);
    out.m[15] = 1.0f;

    return out;
}

[[nodiscard]]
Mat4 perspective_vulkan(
    float fov_y_radians,
    float aspect,
    float near_plane,
    float far_plane
) noexcept {
    const float tan_half = std::tan(fov_y_radians * 0.5f);

    Mat4 out{};

    out.m[0] = 1.0f / (aspect * tan_half);
    out.m[5] = 1.0f / tan_half;

    /*
     * Vulkan NDC:
     * x: [-1, 1]
     * y: [-1, 1]
     * z: [0, 1]
     *
     * 这里没有翻转 Y。
     * 后面如果 shader / viewport 需要 Vulkan 风格屏幕坐标翻转，
     * 可以在 projection 或 viewport 中统一处理。
     */
    out.m[10] = far_plane / (near_plane - far_plane);
    out.m[11] = -1.0f;
    out.m[14] = -(far_plane * near_plane) / (far_plane - near_plane);

    return out;
}

} // namespace

Mat4 Mat4::identity() noexcept {
    Mat4 out{};
    out.m[0] = 1.0f;
    out.m[5] = 1.0f;
    out.m[10] = 1.0f;
    out.m[15] = 1.0f;
    return out;
}

void Camera::set_viewport(
    std::uint32_t width,
    std::uint32_t height
) noexcept {
    viewport_width_ = std::max<std::uint32_t>(width, 1);
    viewport_height_ = std::max<std::uint32_t>(height, 1);
}

void Camera::set_perspective(
    float fov_y_degrees,
    float near_plane,
    float far_plane
) noexcept {
    fov_y_degrees_ = std::clamp(fov_y_degrees, 1.0f, 120.0f);
    near_plane_ = std::max(near_plane, 1.0e-6f);
    far_plane_ = std::max(far_plane, near_plane_ + 1.0f);
}

void Camera::set_position(const Vec3& position) noexcept {
    position_ = position;
}

void Camera::set_target(const Vec3& target) noexcept {
    target_ = target;
}

void Camera::set_up(const Vec3& up) noexcept {
    up_ = up;
    normalize_up();
}

void Camera::look_at(
    const Vec3& position,
    const Vec3& target,
    const Vec3& up
) noexcept {
    position_ = position;
    target_ = target;
    up_ = up;
    normalize_up();
}

void Camera::fit_bounds(const CameraBounds& bounds) noexcept {
    const Vec3 center{
        0.5f * (bounds.min.x + bounds.max.x),
        0.5f * (bounds.min.y + bounds.max.y),
        0.5f * (bounds.min.z + bounds.max.z)
    };

    const Vec3 extent{
        bounds.max.x - bounds.min.x,
        bounds.max.y - bounds.min.y,
        bounds.max.z - bounds.min.z
    };

    const float radius =
        0.5f * std::sqrt(
            extent.x * extent.x +
            extent.y * extent.y +
            extent.z * extent.z
        );

    const float safe_radius = std::max(radius, 1.0f);
    const float fov = to_radians(fov_y_degrees_);

    const float dist = safe_radius / std::tan(fov * 0.5f);

    target_ = center;

    /*
     * 默认从 -Y + Z 方向看向数据中心。
     * 对地学数据比较自然：XY 是平面，Z/elevation 是高度。
     */
    const Vec3 dir = normalize({0.0f, -1.0f, 0.6f});
    position_ = add(target_, mul(dir, dist * 1.5f));

    near_plane_ = std::max(dist * 0.001f, 0.001f);
    far_plane_ = std::max(dist * 10.0f + safe_radius * 4.0f, near_plane_ + 1.0f);

    up_ = {0.0f, 0.0f, 1.0f};
}

void Camera::orbit(
    float delta_yaw_radians,
    float delta_pitch_radians
) noexcept {
    Vec3 offset = sub(position_, target_);
    float r = std::max(length(offset), MIN_DISTANCE);

    float yaw = std::atan2(offset.y, offset.x);
    float pitch = std::asin(std::clamp(offset.z / r, -1.0f, 1.0f));

    yaw += delta_yaw_radians;
    pitch += delta_pitch_radians;

    const float pitch_limit = std::asin(MIN_PITCH_DOT);
    pitch = std::clamp(pitch, -pitch_limit, pitch_limit);

    const float cos_pitch = std::cos(pitch);

    offset.x = r * cos_pitch * std::cos(yaw);
    offset.y = r * cos_pitch * std::sin(yaw);
    offset.z = r * std::sin(pitch);

    position_ = add(target_, offset);
}

void Camera::zoom(float scale) noexcept {
    const float safe_scale = std::max(scale, 1.0e-3f);

    Vec3 offset = sub(position_, target_);
    float r = std::max(length(offset), MIN_DISTANCE);

    r *= safe_scale;
    r = std::max(r, MIN_DISTANCE);

    position_ = add(target_, mul(normalize(offset), r));
}

void Camera::pan(float delta_x, float delta_y) noexcept {
    const Vec3 forward = normalize(sub(target_, position_));
    const Vec3 right = normalize(cross(forward, up_));
    const Vec3 real_up = normalize(cross(right, forward));

    const float pan_scale = distance() * 0.001f;

    const Vec3 move = add(
        mul(right, -delta_x * pan_scale),
        mul(real_up, delta_y * pan_scale)
    );

    position_ = add(position_, move);
    target_ = add(target_, move);
}

Mat4 Camera::view_matrix() const noexcept {
    return look_at_matrix(position_, target_, up_);
}

Mat4 Camera::projection_matrix() const noexcept {
    return perspective_vulkan(
        to_radians(fov_y_degrees_),
        aspect_ratio(),
        near_plane_,
        far_plane_
    );
}

Mat4 Camera::view_projection_matrix() const noexcept {
    return multiply(projection_matrix(), view_matrix());
}

const Vec3& Camera::position() const noexcept {
    return position_;
}

const Vec3& Camera::target() const noexcept {
    return target_;
}

const Vec3& Camera::up() const noexcept {
    return up_;
}

float Camera::distance() const noexcept {
    return length(sub(position_, target_));
}

float Camera::aspect_ratio() const noexcept {
    return static_cast<float>(viewport_width_) /
           static_cast<float>(viewport_height_);
}

std::uint32_t Camera::viewport_width() const noexcept {
    return viewport_width_;
}

std::uint32_t Camera::viewport_height() const noexcept {
    return viewport_height_;
}

float Camera::fov_y_degrees() const noexcept {
    return fov_y_degrees_;
}

float Camera::near_plane() const noexcept {
    return near_plane_;
}

float Camera::far_plane() const noexcept {
    return far_plane_;
}

void Camera::normalize_up() noexcept {
    const Vec3 n = normalize(up_);
    if (length(n) <= 0.0f) {
        up_ = {0.0f, 0.0f, 1.0f};
    } else {
        up_ = n;
    }
}

} // namespace gs3d::camera