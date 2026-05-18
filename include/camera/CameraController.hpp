#pragma once

#include "camera/Camera.hpp"
#include "platform/Window.hpp"

namespace gs3d::camera {

struct CameraControllerConfig {
    // 经典 trackball/orbit 控制参数
    float rotate_speed = 1.0f;
    float pan_speed = 1.0f;
    float zoom_speed = 1.0f;

    bool invert_rotate_x = false;
    bool invert_rotate_y = false;
    bool invert_pan_x = false;
    bool invert_pan_y = false;
};

class CameraController {
public:
    CameraController() = default;

    explicit CameraController(CameraControllerConfig config);

    void set_config(const CameraControllerConfig& config) noexcept;

    void set_bounds(const CameraBounds& bounds) noexcept;

    void reset_view(Camera& camera) const noexcept;

    void update(
        Camera& camera,
        const gs3d::platform::Window& window
    ) noexcept;

private:
    CameraControllerConfig config_{};
    CameraBounds bounds_{};
    bool has_bounds_ = false;

private:
    void rotate_trackball(
        Camera& camera,
        float delta_x,
        float delta_y,
        float viewport_width,
        float viewport_height
    ) const noexcept;

    void pan_view(
        Camera& camera,
        float delta_x,
        float delta_y,
        float viewport_height
    ) const noexcept;

    void zoom_view(
        Camera& camera,
        float scroll_y
    ) const noexcept;

    static Vec3 add(const Vec3& a, const Vec3& b) noexcept;
    static Vec3 sub(const Vec3& a, const Vec3& b) noexcept;
    static Vec3 mul(const Vec3& v, float s) noexcept;

    static float dot(const Vec3& a, const Vec3& b) noexcept;
    static Vec3 cross(const Vec3& a, const Vec3& b) noexcept;
    static float length(const Vec3& v) noexcept;
    static Vec3 normalize(const Vec3& v) noexcept;

    static Vec3 rotate_vector(
        const Vec3& v,
        const Vec3& axis,
        float angle
    ) noexcept;
};

} // namespace gs3d::camera