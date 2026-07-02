#pragma once

#include "camera/Camera.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>

namespace gs3d::camera {

struct CameraInput {
    std::uint32_t viewport_width = 0;
    std::uint32_t viewport_height = 0;

    float delta_x = 0.0f;
    float delta_y = 0.0f;
    float scroll_y = 0.0f;

    // 鼠标在视口本地像素坐标（原点左上角，向下为正）。
    // 与 MouseRay::from_screen 的 mouse_x/mouse_y 约定一致。
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    bool mouse_position_valid = false;

    bool rotate = false;
    bool pan = false;

    // True only on the first frame after a left-drag crosses its activation
    // threshold. Rotation itself is incremental and does not depend on it.
    bool rotate_begin = false;

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
     * 仅在上下极点前保留 1° 安全余量，防止方位角翻转。
     * 相机可以越过 XY 数据平面，从下方向上观察点云。
     */
    float min_pitch = -1.553f;  // ≈ −89°
    float max_pitch =  1.553f;  // ≈ +89°
};

class CameraController {
public:
    CameraController() = default;

    explicit CameraController(CameraControllerConfig config);

    void set_config(const CameraControllerConfig& config) noexcept;

    void set_bounds(const CameraBounds& bounds) noexcept;

    void set_orbit_pivot(const Vec3& pivot) noexcept;

    void clear_orbit_pivot() noexcept;

    [[nodiscard]]
    std::optional<Vec3> orbit_pivot() const noexcept;

    void focus_on(Camera& camera, const Vec3& point) noexcept;

    // Animate camera to a new target and ortho_height.  Used by both
    // focus_on (ortho_height unchanged) and box-select zoom.
    void animate_to(
        Camera& camera,
        const Vec3& target,
        float ortho_height
    ) noexcept;

    void set_focus_anim_duration(float seconds) noexcept;

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
    std::optional<Vec3> orbit_pivot_{};

    // Focus animation: smooth camera transition so tile changes spread
    // across frames instead of all at once (ponytail: avoids multi-frame
    // stutter from massive tile reloads after instant camera jumps).
    std::optional<Vec3> anim_start_pos_{};
    std::optional<Vec3> anim_end_pos_{};
    std::optional<Vec3> anim_start_target_{};
    std::optional<Vec3> anim_end_target_{};
    std::optional<float> anim_start_ortho_h_{};
    std::optional<float> anim_end_ortho_h_{};
    std::chrono::steady_clock::time_point anim_start_time_{};
    float focus_anim_duration_s_ = 0.3f;

    void pan_view(
        Camera& camera,
        float delta_x,
        float delta_y,
        float viewport_height
    ) const noexcept;

    void zoom_view(
        Camera& camera,
        float scroll_y,
        float mouse_x,
        float mouse_y,
        float viewport_width,
        float viewport_height,
        bool mouse_position_valid
    ) const noexcept;

    void adjust_near_far(Camera& camera) const noexcept;

    [[nodiscard]]
    float min_distance() const noexcept {
        // Allow infinite zoom — only prevent division-by-zero.
        return 1.0e-6f;
    }

    [[nodiscard]]
    float max_distance() const noexcept {
        // 防止滚轮无限缩远把场景滚出视野。绑定到场景包围盒对角线的若干
        // 倍——没有 bounds 时不设上限（沿用旧行为，不强加假设）。
        if (!has_bounds_) {
            return 1.0e9f;
        }
        const Vec3 ext = sub(bounds_.max, bounds_.min);
        const float diag = length(ext);
        return std::max(diag * 50.0f, 1.0f);
    }

    static Vec3 add(const Vec3& a, const Vec3& b) noexcept;
    static Vec3 sub(const Vec3& a, const Vec3& b) noexcept;
    static Vec3 mul(const Vec3& v, float s) noexcept;

    static float dot(const Vec3& a, const Vec3& b) noexcept;
    static Vec3 cross(const Vec3& a, const Vec3& b) noexcept;
    static float length(const Vec3& v) noexcept;
    static Vec3 normalize(const Vec3& v) noexcept;
};

} // namespace gs3d::camera
