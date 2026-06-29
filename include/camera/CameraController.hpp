#pragma once

#include "camera/Camera.hpp"

#include <algorithm>
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

    bool rotate = false;
    bool pan = false;

    // True only on the first frame of a left-drag (mouse just pressed).
    // Used by CameraController to compute and lock a rotation anchor.
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

    // --- Cumulative orbit state (per-viewport, per-drag) ---
    // Locked at drag-start when cursor moves past activation threshold;
    // all discarded on release.
    std::optional<Vec3> active_rotate_center_{};
    Vec3 position_at_lock_{};
    Vec3 target_at_lock_{};
    float cumulative_theta_ = 0.0f;
    float cumulative_phi_   = 0.0f;

private:
    // Apply a cumulative spherical rotation of (position_ref, target_ref)
    // around |pivot| and commit via look_at.  When cumulative angles are
    // zero this is a no-op — the view does not jump.
    void orbit_around_pivot(
        Camera& camera,
        float cumulative_theta,
        float cumulative_phi,
        const Vec3& pivot,
        const Vec3& position_ref,
        const Vec3& target_ref
    ) const noexcept;

    void pan_view(
        Camera& camera,
        float delta_x,
        float delta_y,
        float viewport_height
    ) const noexcept;

    void zoom_view(
        Camera& camera,
        float scroll_y,
        float viewport_width,
        float viewport_height
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
