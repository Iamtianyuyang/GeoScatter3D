#pragma once

#include <array>
#include <cstdint>

namespace gs3d::camera {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Mat4 {
    std::array<float, 16> m{};

    [[nodiscard]]
    const float* data() const noexcept {
        return m.data();
    }

    [[nodiscard]]
    float* data() noexcept {
        return m.data();
    }

    [[nodiscard]]
    static Mat4 identity() noexcept;
};

struct CameraBounds {
    Vec3 min;
    Vec3 max;
};

enum class ProjectionMode {
    Perspective,
    Orthographic
};

inline constexpr float kMinNearPlane = 1.0e-4f;
inline constexpr float kMaxDepthRatio = 1.0e5f;
inline constexpr float kNearDistanceFactor = 1.0e-3f;
inline constexpr float kFarDistanceFactor = 2.0f;
inline constexpr float kFarRadiusPadding = 1.0f;

struct ClipPlanes {
    float near_plane = kMinNearPlane;
    float far_plane = kMinNearPlane + 1.0f;
};

[[nodiscard]]
float bounding_sphere_radius(const CameraBounds& bounds) noexcept;

[[nodiscard]]
ClipPlanes compute_clip_planes(
    float distance,
    float scene_radius
) noexcept;

class Camera {
public:
    Camera() = default;

    void set_viewport(std::uint32_t width, std::uint32_t height) noexcept;

    void set_perspective(
        float fov_y_degrees,
        float near_plane,
        float far_plane
    ) noexcept;

    void set_orthographic(
        float height,
        float near_plane,
        float far_plane
    ) noexcept;

    void set_position(const Vec3& position) noexcept;
    void set_target(const Vec3& target) noexcept;
    void set_up(const Vec3& up) noexcept;

    void look_at(
        const Vec3& position,
        const Vec3& target,
        const Vec3& up
    ) noexcept;

    void fit_bounds(const CameraBounds& bounds) noexcept;

    /*
     * XY-only fit for map-style box-select zoom.
     * Preserves target.z, view direction, and camera.up().
     * Only target.x, target.y, and ortho_height are recomputed
     * from the given XY rectangle.  near/far are derived from
     * scene_bounds (not the selection) so the whole dataset
     * stays inside the clip volume.
     */
    void fit_xy_bounds(
        float x_min, float x_max,
        float y_min, float y_max,
        float padding,
        const CameraBounds& scene_bounds) noexcept;

    /*
     * Screen-space box zoom for orthographic cameras.
     * Computes new ortho_height and target directly from the
     * screen rectangle fraction — avoids the world-AABB inflation
     * that happens when unprojecting tilted-camera corners.
     *
     *   scale = max(rect_w/vp_w, rect_h/vp_h)
     *   ortho_height_ *= scale * padding
     *   target_.xy = from_screen(rect_center) ∩ z=target_.z
     *
     * Preserves target.z, view direction, camera.up(),
     * near_plane, and far_plane.
     */
    void fit_screen_rect(
        float rect_min_x, float rect_min_y,
        float rect_max_x, float rect_max_y,
        std::uint32_t viewport_w, std::uint32_t viewport_h,
        float padding) noexcept;

    void orbit(float delta_yaw_radians, float delta_pitch_radians) noexcept;
    void zoom(float scale) noexcept;
    void pan(float delta_x, float delta_y) noexcept;

    [[nodiscard]]
    Mat4 view_matrix() const noexcept;

    [[nodiscard]]
    Mat4 projection_matrix() const noexcept;

    [[nodiscard]]
    Mat4 view_projection_matrix() const noexcept;

    [[nodiscard]]
    const Vec3& position() const noexcept;

    [[nodiscard]]
    const Vec3& target() const noexcept;

    [[nodiscard]]
    const Vec3& up() const noexcept;

    [[nodiscard]]
    float distance() const noexcept;

    [[nodiscard]]
    float aspect_ratio() const noexcept;

    [[nodiscard]]
    std::uint32_t viewport_width() const noexcept;

    [[nodiscard]]
    std::uint32_t viewport_height() const noexcept;

    [[nodiscard]]
    float fov_y_degrees() const noexcept;

    [[nodiscard]]
    float near_plane() const noexcept;

    [[nodiscard]]
    float far_plane() const noexcept;

    [[nodiscard]]
    ProjectionMode projection_mode() const noexcept;

    [[nodiscard]]
    float ortho_height() const noexcept;

    void set_projection_mode(ProjectionMode mode) noexcept;

private:
    Vec3 position_{0.0f, -5.0f, 2.0f};
    Vec3 target_{0.0f, 0.0f, 0.0f};
    Vec3 up_{0.0f, 0.0f, 1.0f};

    std::uint32_t viewport_width_ = 1280;
    std::uint32_t viewport_height_ = 720;

    ProjectionMode projection_mode_ = ProjectionMode::Orthographic;

    float fov_y_degrees_ = 45.0f;
    float ortho_height_ = 10.0f;
    float near_plane_ = 0.01f;
    float far_plane_ = 10000.0f;

private:
    void normalize_up() noexcept;
};

} // namespace gs3d::camera
