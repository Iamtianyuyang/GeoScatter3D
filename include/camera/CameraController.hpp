#pragma once

#include "camera/Camera.hpp"

#include <cstdint>

namespace gs3d::camera {

struct CameraInput {
    std::uint32_t viewport_width = 0;
    std::uint32_t viewport_height = 0;

    float delta_x = 0.0f;
    float delta_y = 0.0f;
    float scroll_y = 0.0f;

    bool rotate = false;
    bool pan = false;

    [[nodiscard]]
    bool interacting() const noexcept {
        return
            (rotate && (delta_x != 0.0f || delta_y != 0.0f)) ||
            (pan && (delta_x != 0.0f || delta_y != 0.0f)) ||
            scroll_y != 0.0f;
    }
};

struct CameraControllerConfig {
    float rotate_speed = 1.0f;
    float pan_speed    = 1.0f;
    float zoom_speed   = 1.0f;

    bool invert_rotate_x = false;
    bool invert_rotate_y = false;
    bool invert_pan_x    = false;
    bool invert_pan_y    = false;

    /*
     * 球坐标 orbit 的俯仰角约束（弧度）。
     * min_pitch > 0 可防止相机翻入数据平面以下；
     * max_pitch < π/2 可防止极点翻转。
     * 默认允许从 −85° 仰视到 +89° 俯视。
     */
    float min_pitch = -1.483f;  // ≈ −85°
    float max_pitch =  1.553f;  // ≈ +89°
};

class CameraController {
public:
    CameraController() = default;

    explicit CameraController(CameraControllerConfig config);

    void set_config(const CameraControllerConfig& config) noexcept;

    void set_bounds(const CameraBounds& bounds) noexcept;

    void reset_view(Camera& camera) const noexcept;

    [[nodiscard]]
    bool update(
        Camera& camera,
        const CameraInput& input
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

    void adjust_near_far(Camera& camera) const noexcept;

    static Vec3 add(const Vec3& a, const Vec3& b) noexcept;
    static Vec3 sub(const Vec3& a, const Vec3& b) noexcept;
    static Vec3 mul(const Vec3& v, float s) noexcept;

    static float dot(const Vec3& a, const Vec3& b) noexcept;
    static Vec3 cross(const Vec3& a, const Vec3& b) noexcept;
    static float length(const Vec3& v) noexcept;
    static Vec3 normalize(const Vec3& v) noexcept;
};

} // namespace gs3d::camera
