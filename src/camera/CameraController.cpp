#include "camera/CameraController.hpp"

#include <algorithm>
#include <cmath>

namespace gs3d::camera {

namespace {

constexpr float PI = 3.14159265358979323846f;
constexpr float MIN_DISTANCE = 1.0e-3f;

} // namespace

CameraController::CameraController(CameraControllerConfig config)
    : config_(config)
{
}

void CameraController::set_config(
    const CameraControllerConfig& config
) noexcept {
    config_ = config;
}

void CameraController::set_bounds(
    const CameraBounds& bounds
) noexcept {
    bounds_ = bounds;
    has_bounds_ = true;
}

void CameraController::reset_view(Camera& camera) const noexcept {
    if (has_bounds_) {
        camera.fit_bounds(bounds_);
    }
}

bool CameraController::update(
    Camera& camera,
    const CameraInput& input
) noexcept {
    if (input.viewport_width == 0 || input.viewport_height == 0) {
        return false;
    }

    camera.set_viewport(
        input.viewport_width,
        input.viewport_height
    );

    const float viewport_width =
        static_cast<float>(input.viewport_width);

    const float viewport_height =
        static_cast<float>(input.viewport_height);

    bool changed = false;

    if (input.rotate &&
        (input.delta_x != 0.0f || input.delta_y != 0.0f)) {
        rotate_trackball(
            camera,
            input.delta_x,
            input.delta_y,
            viewport_width,
            viewport_height
        );
        changed = true;
    }

    if (input.pan &&
        (input.delta_x != 0.0f || input.delta_y != 0.0f)) {
        pan_view(
            camera,
            input.delta_x,
            input.delta_y,
            viewport_height
        );
        changed = true;
    }

    if (input.scroll_y != 0.0f) {
        zoom_view(
            camera,
            input.scroll_y
        );
        changed = true;
    }

    return changed;
}

void CameraController::rotate_trackball(
    Camera& camera,
    float delta_x,
    float delta_y,
    float viewport_width,
    float viewport_height
) const noexcept {
    if (viewport_width <= 0.0f || viewport_height <= 0.0f) {
        return;
    }

    if (config_.invert_rotate_x) delta_x = -delta_x;
    if (config_.invert_rotate_y) delta_y = -delta_y;

    const float d_yaw   = -delta_x / viewport_width  * PI * config_.rotate_speed;
    const float d_pitch = -delta_y / viewport_height * PI * config_.rotate_speed;

    const Vec3 target = camera.target();
    const Vec3 offset = sub(camera.position(), target);
    const float dist  = std::max(length(offset), MIN_DISTANCE);

    /*
     * 球坐标 orbit（Blender/Maya 同款）：
     * 把 (dx, dy, dz) 分解为 (yaw, pitch)，独立增减后重建，
     * 完全消除 up 向量漂移和 Rodrigues 旋转的极点翻转问题。
     * world-up 始终强制为 (0,0,1)，不随旋转积累误差。
     */
    float yaw   = std::atan2(offset.y, offset.x);
    float pitch = std::asin(std::clamp(offset.z / dist, -1.0f, 1.0f));

    yaw   += d_yaw;
    pitch  = std::clamp(
        pitch + d_pitch,
        config_.min_pitch,
        config_.max_pitch
    );

    const float cos_p = std::cos(pitch);
    const Vec3 new_pos = add(target, Vec3{
        dist * cos_p * std::cos(yaw),
        dist * cos_p * std::sin(yaw),
        dist * std::sin(pitch)
    });

    camera.look_at(new_pos, target, {0.0f, 0.0f, 1.0f});
}

void CameraController::pan_view(
    Camera& camera,
    float delta_x,
    float delta_y,
    float viewport_height
) const noexcept {
    if (viewport_height <= 0.0f) {
        return;
    }

    if (config_.invert_pan_x) {
        delta_x = -delta_x;
    }

    if (config_.invert_pan_y) {
        delta_y = -delta_y;
    }

    const Vec3 forward =
        normalize(sub(camera.target(), camera.position()));

    Vec3 right =
        normalize(cross(forward, camera.up()));

    if (length(right) < 1.0e-6f) {
        right = {1.0f, 0.0f, 0.0f};
    }

    const Vec3 up =
        normalize(cross(right, forward));

    /*
     * 常见 pan 手感：
     * 视图高度对应 2 * distance * tan(fov/2) 的世界长度。
     * 鼠标移动多少像素，就移动对应比例的世界距离。
     */
    const float fov_y_rad =
        camera.fov_y_degrees() * PI / 180.0f;

    const float view_height_world =
        2.0f * camera.distance() * std::tan(0.5f * fov_y_rad);

    const float world_per_pixel =
        view_height_world / viewport_height * config_.pan_speed;

    const Vec3 move = add(
        mul(right, -delta_x * world_per_pixel),
        mul(up, delta_y * world_per_pixel)
    );

    camera.look_at(
        add(camera.position(), move),
        add(camera.target(), move),
        camera.up()
    );
}

void CameraController::zoom_view(
    Camera& camera,
    float scroll_y
) const noexcept {
    const Vec3 offset = sub(camera.position(), camera.target());
    float dist = std::max(length(offset), MIN_DISTANCE);

    // 指数缩放：每格滚轮缩放 ~12%，手感平滑且远近一致
    const float zoom_factor =
        std::pow(0.88f, scroll_y * config_.zoom_speed);

    dist = std::max(dist * zoom_factor, MIN_DISTANCE);

    camera.look_at(
        add(camera.target(), mul(normalize(offset), dist)),
        camera.target(),
        camera.up()
    );

    adjust_near_far(camera);
}

void CameraController::adjust_near_far(Camera& camera) const noexcept {
    const float dist = camera.distance();

    float scene_radius = 1.0f;
    if (has_bounds_) {
        const Vec3 ext = sub(bounds_.max, bounds_.min);
        scene_radius = std::max({ ext.x, ext.y, ext.z }) * 0.5f;
    }

    /*
     * 动态近/远平面：
     *   near = distance × 0.001，保证深度精度在任意缩放级别下充足；
     *   far  = distance + scene_radius × 4，保证整个数据集始终可见。
     * 对应 Cesium 和 Potree 的自动 near/far 策略。
     */
    const float near_plane = std::max(0.1f, dist * 0.001f);
    const float far_plane  = std::max(
        near_plane * 1000.0f,
        dist + scene_radius * 4.0f
    );

    camera.set_perspective(
        camera.fov_y_degrees(),
        near_plane,
        far_plane
    );
}

Vec3 CameraController::add(
    const Vec3& a,
    const Vec3& b
) noexcept {
    return {
        a.x + b.x,
        a.y + b.y,
        a.z + b.z
    };
}

Vec3 CameraController::sub(
    const Vec3& a,
    const Vec3& b
) noexcept {
    return {
        a.x - b.x,
        a.y - b.y,
        a.z - b.z
    };
}

Vec3 CameraController::mul(
    const Vec3& v,
    float s
) noexcept {
    return {
        v.x * s,
        v.y * s,
        v.z * s
    };
}

float CameraController::dot(
    const Vec3& a,
    const Vec3& b
) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 CameraController::cross(
    const Vec3& a,
    const Vec3& b
) noexcept {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

float CameraController::length(
    const Vec3& v
) noexcept {
    return std::sqrt(dot(v, v));
}

Vec3 CameraController::normalize(
    const Vec3& v
) noexcept {
    const float len = length(v);

    if (len <= 1.0e-8f) {
        return {0.0f, 0.0f, 0.0f};
    }

    return mul(v, 1.0f / len);
}

} // namespace gs3d::camera
