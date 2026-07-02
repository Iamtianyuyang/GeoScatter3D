#include "camera/CameraController.hpp"

#include <algorithm>
#include <cmath>

namespace gs3d::camera {

namespace {

constexpr float PI = 3.14159265358979323846f;

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
        // Give the camera a canonical viewing direction before fit_bounds,
        // which now preserves whatever direction it finds.
        const Vec3 center{
            0.5f * (bounds_.min.x + bounds_.max.x),
            0.5f * (bounds_.min.y + bounds_.max.y),
            0.5f * (bounds_.min.z + bounds_.max.z)
        };
        camera.look_at(
            add(center, Vec3{0.0f, -1.0f, 0.6f}),
            center,
            Vec3{0.0f, 0.0f, 1.0f}
        );
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

    // --- Rotation: incremental rigid orbit around a stable scene pivot ---
    // Pan moves position + target for composition, but must not move the
    // object's rotation centre. When scene bounds are known, rotate both
    // position and target by the same rigid transform around bounds centre.
    // This keeps the panned object at the same screen location while it
    // rotates. Without bounds, camera.target() remains the fallback pivot.

    bool rotated_this_frame = false;

    if (input.rotate &&
        (input.delta_x != 0.0f || input.delta_y != 0.0f)) {

        // Per-frame mouse delta → spherical angle delta.
        constexpr float kTwoPi = 2.0f * PI;
        float angle_h = -kTwoPi * input.delta_x / viewport_height
                      * config_.rotate_speed;
        // Mouse Y grows downward. Three.js stores polar angle (down from the
        // up axis), while this controller stores elevation above the XY
        // plane, so the equivalent elevation delta has the opposite sign.
        float angle_v = kTwoPi * input.delta_y / viewport_height
                      * config_.rotate_speed;
        if (config_.invert_rotate_x) angle_h = -angle_h;
        if (config_.invert_rotate_y) angle_v = -angle_v;

        const Vec3 pivot = has_bounds_
            ? Vec3{
                  0.5f * (bounds_.min.x + bounds_.max.x),
                  0.5f * (bounds_.min.y + bounds_.max.y),
                  0.5f * (bounds_.min.z + bounds_.max.z)
              }
            : camera.target();

        Vec3 position_offset = sub(camera.position(), pivot);
        Vec3 target_offset = sub(camera.target(), pivot);
        const float orbit_radius = length(position_offset);
        if (orbit_radius > 1.0e-8f) {
            const Vec3 world_up{0.0f, 0.0f, 1.0f};

            const auto rotate_axis =
                [&](const Vec3& value, const Vec3& axis, float angle) {
                    const float c = std::cos(angle);
                    const float s = std::sin(angle);
                    return add(
                        add(
                            mul(value, c),
                            mul(cross(axis, value), s)
                        ),
                        mul(axis, dot(axis, value) * (1.0f - c))
                    );
                };

            const Vec3 forward = normalize(
                sub(camera.target(), camera.position()));
            Vec3 right = normalize(cross(forward, camera.up()));
            if (length(right) < 1.0e-6f) {
                right = {1.0f, 0.0f, 0.0f};
            }
            Vec3 screen_up = normalize(cross(right, forward));

            // Turntable yaw around the fixed world-up axis.
            position_offset =
                rotate_axis(position_offset, world_up, angle_h);
            target_offset =
                rotate_axis(target_offset, world_up, angle_h);
            right = rotate_axis(right, world_up, angle_h);
            screen_up = rotate_axis(screen_up, world_up, angle_h);

            // Clamp elevation of the camera around the scene pivot, then use
            // the effective delta as a rigid pitch for position and target.
            const float current_pitch = std::asin(std::clamp(
                position_offset.z / orbit_radius, -1.0f, 1.0f));
            const float clamped_pitch = std::clamp(
                current_pitch + angle_v,
                config_.min_pitch,
                config_.max_pitch
            );
            const float pitch_delta = clamped_pitch - current_pitch;

            Vec3 pitch_axis =
                normalize(cross(position_offset, world_up));
            if (length(pitch_axis) < 1.0e-6f) {
                pitch_axis = right;
            }

            position_offset =
                rotate_axis(position_offset, pitch_axis, pitch_delta);
            target_offset =
                rotate_axis(target_offset, pitch_axis, pitch_delta);
            screen_up =
                rotate_axis(screen_up, pitch_axis, pitch_delta);

            camera.look_at(
                add(pivot, position_offset),
                add(pivot, target_offset),
                screen_up
            );
            adjust_near_far(camera);
            rotated_this_frame = true;
        }
    }

    bool changed = false;
    if (rotated_this_frame) {
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
        adjust_near_far(camera);
        changed = true;
    }

    if (input.scroll_y != 0.0f) {
        zoom_view(
            camera,
            input.scroll_y,
            viewport_width,
            viewport_height
        );
        changed = true;
    }

    return changed;
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

    const Vec3 screen_up =
        normalize(cross(right, forward));

    /*
     * Pan: mouse pixels → world distance.
     * Ortho: view_height_world = ortho_height (exact).
     * Perspective: 2 × distance × tan(fov/2).
     */
    float view_height_world;
    const bool is_ortho =
        camera.projection_mode() == ProjectionMode::Orthographic;
    if (is_ortho) {
        view_height_world = camera.ortho_height();
    } else {
        const float fov_y_rad =
            camera.fov_y_degrees() * PI / 180.0f;
        view_height_world =
            2.0f * camera.distance() * std::tan(0.5f * fov_y_rad);
    }

    float world_per_pixel =
        view_height_world / viewport_height * config_.pan_speed;

    // Screen-space pan for both projection modes. Moving the camera opposite
    // horizontal mouse motion and with vertical mouse motion makes scene
    // points follow the cursor pixel-for-pixel, independent of world axes.
    const Vec3 move = add(
        mul(right, -delta_x * world_per_pixel),
        mul(screen_up, delta_y * world_per_pixel)
    );

    camera.look_at(
        add(camera.position(), move),
        add(camera.target(), move),
        camera.up()
    );
}

void CameraController::zoom_view(
    Camera& camera,
    float scroll_y,
    float /*viewport_width*/,
    float /*viewport_height*/
) const noexcept {
    /*
     * Orthographic zoom: scale the visible world height.
     * Uniform, no distance/FOV floor, no singularity — can zoom to
     * float precision limits.
     */
    const float zoom_factor =
        std::pow(0.75f, scroll_y * config_.zoom_speed);

    if (camera.projection_mode() == ProjectionMode::Orthographic) {
        float ortho_h = camera.ortho_height();
        ortho_h = std::clamp(
            ortho_h * zoom_factor, 1.0e-6f, 1.0e9f);
        camera.set_orthographic(
            ortho_h,
            camera.near_plane(),
            camera.far_plane()
        );
    } else {
        // Perspective path: FOV-based continuation (kept for future
        // re-enable but not currently wired).
        float new_fov = camera.fov_y_degrees() * zoom_factor;
        new_fov = std::clamp(new_fov, 1.0f, 120.0f);
        camera.set_perspective(
            new_fov,
            camera.near_plane(),
            camera.far_plane()
        );
    }
}

void CameraController::adjust_near_far(Camera& camera) const noexcept {
    // Orthographic near/far are set once in fit_bounds() and left alone.
    // Perspective path (below) only runs when projection mode is
    // explicitly perspective.
    if (camera.projection_mode() == ProjectionMode::Orthographic) {
        return;
    }

    float scene_radius = 1.0f;
    if (has_bounds_) {
        scene_radius = std::max(
            bounding_sphere_radius(bounds_),
            1.0f
        );
    }

    const ClipPlanes clip_planes =
        compute_clip_planes(camera.distance(), scene_radius);

    camera.set_perspective(
        camera.fov_y_degrees(),
        clip_planes.near_plane,
        clip_planes.far_plane
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
