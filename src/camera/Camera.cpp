#include "camera/Camera.hpp"
#include "camera/MouseRay.hpp"

#include <algorithm>
#include <cmath>

namespace gs3d::camera {

namespace {

constexpr float PI = 3.14159265358979323846f;
constexpr float MIN_DISTANCE = 1.0e-6f;
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
    // Negate Y: Vulkan viewport maps NDC y=-1→top, y=+1→bottom.
    // Flipping this sign makes world +Y / eye +Y map to screen top,
    // consistent with MouseRay::from_screen / to_screen and ImGui.
    out.m[5] = -1.0f / tan_half;

    out.m[10] = far_plane / (near_plane - far_plane);
    out.m[11] = -1.0f;
    out.m[14] = -(far_plane * near_plane) / (far_plane - near_plane);

    return out;
}

[[nodiscard]]
Mat4 orthographic_vulkan(
    float half_width,
    float half_height,
    float near_plane,
    float far_plane
) noexcept {
    /*
     * Vulkan NDC: x∈[-1,1], y∈[-1,1], z∈[0,1].
     * Map world [−half_w, +half_w] → NDC [−1, 1], etc.
     * Y is negated so world +Y / eye +Y maps to screen top,
     * consistent with MouseRay and ImGui coordinate conventions.
     */
    Mat4 out{};

    out.m[0]  = 1.0f / half_width;
    out.m[5]  = -1.0f / half_height;
    // Vulkan NDC z∈[0,1] mapping:
    //   z_ndc = (z_view + near) / (near − far)
    // where z_view is negative in front of the camera (Vulkan −Z axis).
    out.m[10] = 1.0f / (near_plane - far_plane);
    out.m[14] = near_plane / (near_plane - far_plane);
    out.m[15] = 1.0f;

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

float bounding_sphere_radius(const CameraBounds& bounds) noexcept {
    const Vec3 extent{
        bounds.max.x - bounds.min.x,
        bounds.max.y - bounds.min.y,
        bounds.max.z - bounds.min.z
    };

    return 0.5f * std::sqrt(
        extent.x * extent.x +
        extent.y * extent.y +
        extent.z * extent.z
    );
}

ClipPlanes compute_clip_planes(
    float distance,
    float scene_radius
) noexcept {
    const float safe_distance = std::max(distance, 0.0f);
    const float safe_radius = std::max(scene_radius, 1.0f);

    const float far_plane = std::max(
        safe_distance * kFarDistanceFactor +
            safe_radius * kFarRadiusPadding,
        kMinNearPlane + 1.0f
    );

    float near_plane = std::max(
        kMinNearPlane,
        safe_distance * kNearDistanceFactor
    );
    near_plane = std::max(near_plane, far_plane / kMaxDepthRatio);

    return {near_plane, far_plane};
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
    projection_mode_ = ProjectionMode::Perspective;
    fov_y_degrees_ = std::clamp(fov_y_degrees, 1.0f, 120.0f);
    near_plane_ = std::max(near_plane, 1.0e-6f);
    far_plane_ = std::max(far_plane, near_plane_ + 1.0f);
}

void Camera::set_orthographic(
    float height,
    float near_plane,
    float far_plane
) noexcept {
    projection_mode_ = ProjectionMode::Orthographic;
    ortho_height_ = std::max(height, 1.0e-6f);
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

    const float scene_diag = length(extent);
    const float safe_radius = std::max(
        bounding_sphere_radius(bounds), 1.0f);

    target_ = center;
    up_ = {0.0f, 0.0f, 1.0f};

    if (projection_mode_ == ProjectionMode::Orthographic) {
        // Ortho: set height to fit the larger of the XY extents,
        // plus 5 % padding so the data doesn't touch the viewport edge.
        const float aspect = aspect_ratio();
        const float fit_h = std::max(extent.x / aspect, extent.y);
        ortho_height_ = fit_h * 1.05f;

        // Ortho near/far: near small, far covers camera-to-data distance
        // plus generous margin.  (Vulkan NDC z∈[0,1], depth is linear.)
        near_plane_ = 0.01f;
        far_plane_  = std::max(
            length(sub(position_, target_)) + safe_radius * 3.0f,
            near_plane_ + 1.0f);

        // Map-style orthographic reset is a true top-down view.  Using an
        // oblique direction here foreshortens large, flat terrain datasets
        // into a narrow strip and makes the Z=0 map-axis intersection explode.
        up_ = {0.0f, 1.0f, 0.0f};
        position_ = add(
            target_,
            Vec3{0.0f, 0.0f, safe_radius * 2.0f}
        );

    } else {
        const float fov   = to_radians(fov_y_degrees_);
        const float aspect = aspect_ratio();
        const float tan_half = std::tan(fov * 0.5f);

        const float half_fov_v = tan_half;
        const float half_fov_h = tan_half * aspect;
        const float limiting_half_fov = std::min(half_fov_v, half_fov_h);
        const float dist = safe_radius / limiting_half_fov;

        const Vec3 current_offset = sub(position_, target_);
        const float current_dist = length(current_offset);
        Vec3 dir;
        if (current_dist > 1.0e-6f) {
            dir = normalize(current_offset);
        } else {
            dir = normalize({0.0f, -1.0f, 0.6f});
        }
        position_ = add(target_, mul(dir, dist * 1.0f));

        const ClipPlanes clip_planes =
            compute_clip_planes(dist, safe_radius);
        near_plane_ = clip_planes.near_plane;
        far_plane_ = clip_planes.far_plane;
    }
}

void Camera::fit_xy_bounds(
    float x_min, float x_max,
    float y_min, float y_max,
    float padding,
    const CameraBounds& scene_bounds) noexcept
{
    const float extent_x = std::max(x_max - x_min, 0.0f);
    const float extent_y = std::max(y_max - y_min, 0.0f);
    if (extent_x <= 0.0f && extent_y <= 0.0f) {
        return;  // degenerate selection — no-op
    }

    // Save viewing direction and Z before changing anything.
    const Vec3 old_target = target_;
    const Vec3 offset = sub(position_, target_);
    const float dist = std::max(length(offset), 1.0f);
    const Vec3 view_dir = normalize(offset);

    // New camera target: XY centre of the selection, preserve Z.
    target_.x = 0.5f * (x_min + x_max);
    target_.y = 0.5f * (y_min + y_max);
    // target_.z unchanged

    // Ortho height to fit the XY rectangle.
    const float aspect = aspect_ratio();
    const float fit_h = std::max(
        extent_y,
        extent_x / std::max(aspect, 1.0e-6f));
    ortho_height_ = std::max(fit_h * padding, 0.01f);

    // Position: same distance and direction from the new target.
    position_ = add(target_, mul(view_dir, dist));

    // near/far from the full scene bounds so the whole dataset
    // remains inside the clip volume.
    const float scene_radius = std::max(
        bounding_sphere_radius(scene_bounds), 1.0f);
    const ClipPlanes clip =
        compute_clip_planes(dist, scene_radius);
    near_plane_ = clip.near_plane;
    far_plane_  = clip.far_plane;
}

void Camera::fit_screen_rect(
    float rect_min_x, float rect_min_y,
    float rect_max_x, float rect_max_y,
    std::uint32_t viewport_w, std::uint32_t viewport_h,
    float padding) noexcept
{
    const float rect_w = std::max(rect_max_x - rect_min_x, 1.0f);
    const float rect_h = std::max(rect_max_y - rect_min_y, 1.0f);
    const float vp_w = static_cast<float>(std::max(viewport_w, 1u));
    const float vp_h = static_cast<float>(std::max(viewport_h, 1u));

    // Screen-space scale: fraction of viewport the rect covers.
    const float scale_x = rect_w / vp_w;
    const float scale_y = rect_h / vp_h;
    const float max_scale = std::max(scale_x, scale_y);

    const float new_ortho_h =
        std::max(ortho_height_ * max_scale * padding, 0.01f);

    // Preserve view direction and distance.
    const Vec3 offset = sub(position_, target_);
    const float dist = std::max(length(offset), 1.0f);
    const Vec3 view_dir = normalize(offset);

    // Project rect centre to world BEFORE changing ortho_height,
    // so the ray uses the projection the user actually saw when
    // drawing the box.  Otherwise the target drifts further off
    // the larger the zoom ratio and the further the rect is from
    // the viewport centre.
    const float cx = 0.5f * (rect_min_x + rect_max_x);
    const float cy = 0.5f * (rect_min_y + rect_max_y);
    const Viewport vp{viewport_w, viewport_h};
    const Ray ray = MouseRay::from_screen(
        static_cast<double>(cx), static_cast<double>(cy), vp, *this);
    const auto hit = MouseRay::intersect_plane(
        ray, {0.0f, 0.0f, target_.z}, {0.0f, 0.0f, 1.0f});

    // Apply new ortho_height and target.
    ortho_height_ = new_ortho_h;
    if (hit) {
        target_.x = hit->x;
        target_.y = hit->y;
    }
    // target_.z unchanged

    // Position: same direction and distance from new target.
    position_ = add(target_, mul(view_dir, dist));

    // near/far unchanged — preserves existing clip volume.
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
    if (projection_mode_ == ProjectionMode::Orthographic) {
        const float half_h = ortho_height_ * 0.5f;
        const float half_w = half_h * aspect_ratio();
        return orthographic_vulkan(
            half_w, half_h,
            near_plane_, far_plane_
        );
    }
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

ProjectionMode Camera::projection_mode() const noexcept {
    return projection_mode_;
}

float Camera::ortho_height() const noexcept {
    return ortho_height_;
}

void Camera::set_projection_mode(ProjectionMode mode) noexcept {
    projection_mode_ = mode;
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
