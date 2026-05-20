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

class Camera {
public:
    Camera() = default;

    void set_viewport(std::uint32_t width, std::uint32_t height) noexcept;

    void set_perspective(
        float fov_y_degrees,
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

private:
    Vec3 position_{0.0f, -5.0f, 2.0f};
    Vec3 target_{0.0f, 0.0f, 0.0f};
    Vec3 up_{0.0f, 0.0f, 1.0f};

    std::uint32_t viewport_width_ = 1280;
    std::uint32_t viewport_height_ = 720;

    float fov_y_degrees_ = 45.0f;
    float near_plane_ = 0.01f;
    float far_plane_ = 10000.0f;

private:
    void normalize_up() noexcept;
};

} // namespace gs3d::camera