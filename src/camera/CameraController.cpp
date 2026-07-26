#include "camera/CameraController.hpp"
#include "camera/MouseRay.hpp"

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

void CameraController::set_orbit_pivot(const Vec3& pivot) noexcept {
    orbit_pivot_ = pivot;
}

void CameraController::clear_orbit_pivot() noexcept {
    orbit_pivot_.reset();
}

void CameraController::copy_pivot_from(const CameraController& src) noexcept {
    if (src.orbit_pivot_.has_value()) {
        orbit_pivot_ = *src.orbit_pivot_;
    } else {
        orbit_pivot_.reset();
    }
}

std::optional<Vec3> CameraController::orbit_pivot() const noexcept {
    return orbit_pivot_;
}

void CameraController::focus_on(
    Camera& camera,
    const Vec3& point
) noexcept {
    orbit_pivot_ = point;
    animate_to(camera, point, camera.ortho_height());
}

void CameraController::animate_to(
    Camera& camera,
    const Vec3& target,
    float ortho_height
) noexcept {
    if (focus_anim_duration_s_ <= 0.0f) {
        // Instant jump (test path or explicitly disabled).
        const Vec3 move = sub(target, camera.target());
        camera.look_at(
            add(camera.position(), move),
            target,
            camera.up()
        );
        camera.set_orthographic(
            ortho_height,
            camera.near_plane(),
            camera.far_plane()
        );
        adjust_near_far(camera);
        return;
    }

    // ponytail: animate the transition instead of jumping instantly.
    // An instant camera jump changes all visible tiles at once, causing
    // multi-frame stutter while the streaming system catches up.
    // Spreading the move across ~0.3s makes tile changes incremental,
    // same as a fast pan or zoom.
    anim_start_pos_ = camera.position();
    anim_start_target_ = camera.target();
    const Vec3 move = sub(target, camera.target());
    anim_end_pos_ = add(camera.position(), move);
    anim_end_target_ = target;
    anim_start_ortho_h_ = camera.ortho_height();
    anim_end_ortho_h_ = ortho_height;
    anim_start_time_ = std::chrono::steady_clock::now();
}

void CameraController::set_focus_anim_duration(float seconds) noexcept {
    focus_anim_duration_s_ = std::max(0.0f, seconds);
}

void CameraController::reset_view(Camera& camera) noexcept {
    if (has_bounds_) {
        // Drop any prior explicit pivot so the reset camera rotates around
        // its new target (= bounds centre) rather than a stale focus point.
        orbit_pivot_.reset();

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

    bool changed = false;

    // --- Focus animation: smooth camera transition ---
    // Spreads camera jumps (from focus_on) across frames so tile changes
    // are incremental, avoiding the multi-frame stutter caused by massive
    // tile reloads after an instant jump.
    if (anim_start_pos_.has_value()) {
        if (input.interacting()) {
            // User took control — cancel the animation.
            anim_start_pos_.reset();
            anim_end_pos_.reset();
            anim_start_target_.reset();
            anim_end_target_.reset();
            anim_start_ortho_h_.reset();
            anim_end_ortho_h_.reset();
        } else {
            const auto now = std::chrono::steady_clock::now();
            const float elapsed = std::chrono::duration<float>(
                now - anim_start_time_).count();
            float t = std::clamp(
                elapsed / focus_anim_duration_s_, 0.0f, 1.0f);
            // Ease-out cubic: fast initial move, smooth landing.
            const float ease = 1.0f - std::pow(1.0f - t, 3.0f);

            const auto lerp =
                [](const Vec3& a, const Vec3& b, float w) -> Vec3 {
                    return {a.x + (b.x - a.x) * w,
                            a.y + (b.y - a.y) * w,
                            a.z + (b.z - a.z) * w};
                };

            camera.look_at(
                lerp(*anim_start_pos_, *anim_end_pos_, ease),
                lerp(*anim_start_target_, *anim_end_target_, ease),
                camera.up()
            );
            if (anim_start_ortho_h_.has_value()) {
                const float ortho_h =
                    *anim_start_ortho_h_ +
                    (*anim_end_ortho_h_ - *anim_start_ortho_h_) * ease;
                camera.set_orthographic(
                    ortho_h,
                    camera.near_plane(),
                    camera.far_plane()
                );
            }
            adjust_near_far(camera);
            changed = true;

            if (t >= 1.0f) {
                anim_start_pos_.reset();
                anim_end_pos_.reset();
                anim_start_target_.reset();
                anim_end_target_.reset();
                anim_start_ortho_h_.reset();
                anim_end_ortho_h_.reset();
            }
        }
    }

    const float viewport_width =
        static_cast<float>(input.viewport_width);

    const float viewport_height =
        static_cast<float>(input.viewport_height);

    // --- Rotation: incremental rigid orbit around the orbit pivot ---
    // The pivot is either an explicit world-space focus point (set via
    // focus_on / set_orbit_pivot) or, by default, the camera's current
    // target. Pan moves position and target. Therefore the implicit pivot
    // follows a pan, while an explicitly selected point remains locked to
    // the same world coordinate.
    // The bounds centre is NOT used here; it is reserved for fit_bounds
    // and reset_view, which is the only time we want to recentre.

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

        // New policy: explicit pivot wins, otherwise pivot = camera target.
        // bounds centre is reserved for fit_bounds / reset_view only — using
        // it here would make rotation pivot around the original scene
        // centre even after a pan, breaking translation invariance.
        const Vec3 pivot = orbit_pivot_.value_or(camera.target());

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

            Vec3 forward = normalize(
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
            forward = rotate_axis(forward, world_up, angle_h);
            right = rotate_axis(right, world_up, angle_h);
            screen_up = rotate_axis(screen_up, world_up, angle_h);

            // Vertical mouse motion is defined in screen space. Clamp the
            // camera viewing elevation, then pitch the complete camera frame
            // around its screen-right axis through the selected pivot. Unlike
            // cross(position - pivot, world_up), this axis does not change
            // merely because the user panned an off-centre locked point.
            const float current_pitch = std::asin(std::clamp(
                -forward.z, -1.0f, 1.0f));
            const float clamped_pitch = std::clamp(
                current_pitch + angle_v,
                config_.min_pitch,
                config_.max_pitch
            );
            const float pitch_delta = clamped_pitch - current_pitch;

            const Vec3 pitch_axis = mul(right, -1.0f);

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
            input.mouse_x,
            input.mouse_y,
            viewport_width,
            viewport_height,
            input.mouse_position_valid
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
) noexcept {
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
    float mouse_x,
    float mouse_y,
    float viewport_width,
    float viewport_height,
    bool mouse_position_valid
) noexcept {
    /*
     * Orthographic zoom: scale the visible world height.
     * Uniform, no distance/FOV floor, no singularity — can zoom to
     * float precision limits.
     */
    const float zoom_factor =
        std::pow(0.75f, scroll_y * config_.zoom_speed);

    std::optional<Vec3> anchor;
    if (mouse_position_valid &&
        viewport_width > 0.0f &&
        viewport_height > 0.0f) {
        anchor = MouseRay::intersect_camera_facing_plane(
            static_cast<double>(mouse_x),
            static_cast<double>(mouse_y),
            Viewport{
                static_cast<std::uint32_t>(viewport_width),
                static_cast<std::uint32_t>(viewport_height)
            },
            camera,
            camera.target()
        );
    }

    float applied_scale = 1.0f;
    if (camera.projection_mode() == ProjectionMode::Orthographic) {
        const float old_ortho_h = camera.ortho_height();
        const float ortho_h = std::clamp(
            old_ortho_h * zoom_factor, 1.0e-6f, 1.0e9f);
        applied_scale = old_ortho_h > 0.0f
            ? ortho_h / old_ortho_h
            : 1.0f;
        camera.set_orthographic(
            ortho_h,
            camera.near_plane(),
            camera.far_plane()
        );
    } else {
        // Perspective path: FOV-based continuation (kept for future
        // re-enable but not currently wired).
        const float old_fov = camera.fov_y_degrees();
        float new_fov = old_fov * zoom_factor;
        new_fov = std::clamp(new_fov, 1.0f, 120.0f);
        const float old_half_tan =
            std::tan(0.5f * old_fov * PI / 180.0f);
        const float new_half_tan =
            std::tan(0.5f * new_fov * PI / 180.0f);
        applied_scale = old_half_tan > 0.0f
            ? new_half_tan / old_half_tan
            : 1.0f;
        camera.set_perspective(
            new_fov,
            camera.near_plane(),
            camera.far_plane()
        );
    }

    if (anchor.has_value()) {
        const Vec3 move = mul(
            sub(*anchor, camera.target()),
            1.0f - applied_scale
        );
        camera.look_at(
            add(camera.position(), move),
            add(camera.target(), move),
            camera.up()
        );
        // An explicit pivot is a selected world-space point and must not
        // follow this cursor-anchored camera translation.
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
