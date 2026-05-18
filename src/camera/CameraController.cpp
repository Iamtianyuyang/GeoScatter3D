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

void CameraController::update(
    Camera& camera,
    const gs3d::platform::Window& window
) noexcept {
    const auto framebuffer_size = window.framebuffer_size();

    if (framebuffer_size.width == 0 || framebuffer_size.height == 0) {
        return;
    }

    camera.set_viewport(
        framebuffer_size.width,
        framebuffer_size.height
    );

    const auto& mouse = window.mouse_state();

    const float viewport_width =
        static_cast<float>(framebuffer_size.width);

    const float viewport_height =
        static_cast<float>(framebuffer_size.height);

    if (mouse.left_pressed &&
        (mouse.delta_x != 0.0 || mouse.delta_y != 0.0)) {
        rotate_trackball(
            camera,
            static_cast<float>(mouse.delta_x),
            static_cast<float>(mouse.delta_y),
            viewport_width,
            viewport_height
        );
    }

    if ((mouse.right_pressed || mouse.middle_pressed) &&
        (mouse.delta_x != 0.0 || mouse.delta_y != 0.0)) {
        pan_view(
            camera,
            static_cast<float>(mouse.delta_x),
            static_cast<float>(mouse.delta_y),
            viewport_height
        );
    }

    if (mouse.scroll_y != 0.0) {
        zoom_view(
            camera,
            static_cast<float>(mouse.scroll_y)
        );
    }
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

    if (config_.invert_rotate_x) {
        delta_x = -delta_x;
    }

    if (config_.invert_rotate_y) {
        delta_y = -delta_y;
    }

    /*
     * 常见 Trackball / Orbit Camera 做法：
     * 鼠标横向移动映射为绕世界 up 轴旋转；
     * 鼠标纵向移动映射为绕当前 camera right 轴旋转。
     *
     * 用 viewport 尺寸归一化，避免不同分辨率下手感差异太大。
     */
    const float yaw =
        -delta_x / viewport_width * PI * config_.rotate_speed;

    const float pitch =
        -delta_y / viewport_height * PI * config_.rotate_speed;

    const Vec3 target = camera.target();
    const Vec3 position = camera.position();

    Vec3 offset = sub(position, target);

    const float distance = std::max(length(offset), MIN_DISTANCE);

    const Vec3 world_up{0.0f, 0.0f, 1.0f};

    Vec3 forward = normalize(sub(target, position));
    Vec3 right = normalize(cross(forward, camera.up()));

    if (length(right) < 1.0e-6f) {
        right = {1.0f, 0.0f, 0.0f};
    }

    /*
     * 先 yaw，再 pitch。
     */
    offset = rotate_vector(offset, world_up, yaw);
    offset = rotate_vector(offset, right, pitch);

    Vec3 new_up = rotate_vector(camera.up(), world_up, yaw);
    new_up = rotate_vector(new_up, right, pitch);

    /*
     * 防止相机翻到奇异位置。
     */
    if (length(offset) < MIN_DISTANCE) {
        offset = mul(normalize(offset), distance);
    }

    camera.look_at(
        add(target, offset),
        target,
        normalize(new_up)
    );
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
    /*
     * 常见 dolly zoom：
     * 沿 target-position 方向移动相机，不改变 target。
     */
    const Vec3 target = camera.target();
    const Vec3 position = camera.position();

    Vec3 offset = sub(position, target);
    float distance = std::max(length(offset), MIN_DISTANCE);

    const float base = 0.88f;
    const float zoom_factor =
        std::pow(base, scroll_y * config_.zoom_speed);

    distance *= zoom_factor;
    distance = std::max(distance, MIN_DISTANCE);

    const Vec3 direction = normalize(offset);

    camera.look_at(
        add(target, mul(direction, distance)),
        target,
        camera.up()
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

Vec3 CameraController::rotate_vector(
    const Vec3& v,
    const Vec3& axis,
    float angle
) noexcept {
    const Vec3 n = normalize(axis);

    const float c = std::cos(angle);
    const float s = std::sin(angle);

    const Vec3 term1 = mul(v, c);
    const Vec3 term2 = mul(cross(n, v), s);
    const Vec3 term3 = mul(n, dot(n, v) * (1.0f - c));

    return add(add(term1, term2), term3);
}

} // namespace gs3d::camera