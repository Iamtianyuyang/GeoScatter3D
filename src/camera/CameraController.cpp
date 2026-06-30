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

    // --- Rotation: cumulative orbit around a pivot locked at drag-start ---
    // At rotate_begin the current (position, target) is snapshotted as a
    // reference.  Each frame accumulates delta angles and applies the
    // *total* rotation to the reference vectors around the pivot.
    // Frame 1 (cumulative = 0): new_pos = position_ref, new_tgt = target_ref
    // — zero jump.  Pivot and target participate in the same rotation.

    bool rotated_this_frame = false;

    if (input.rotate &&
        (input.delta_x != 0.0f || input.delta_y != 0.0f)) {

        if (input.rotate_begin) {
            // Pivot = screen-centre ray ∩ camera-facing plane.
            // The user orbits around whatever is at the centre of the
            // view, not under the cursor — turntable behaviour.
            const double centre_x =
                static_cast<double>(input.viewport_width) * 0.5;
            const double centre_y =
                static_cast<double>(input.viewport_height) * 0.5;
            const auto anchor =
                MouseRay::intersect_camera_facing_plane(
                    centre_x,
                    centre_y,
                    Viewport{
                        input.viewport_width,
                        input.viewport_height
                    },
                    camera,
                    camera.target()
                );
            if (anchor.has_value()) {
                // Snap pivot to target when nearly coincident — avoids
                // float-noise rotation of target around a pivot that
                // differs from target by ~1e-4 (inv-VP matrix error).
                Vec3 pivot_candidate = *anchor;
                if (length(sub(pivot_candidate, camera.target()))
                    < 1.0e-3f) {
                    pivot_candidate = camera.target();
                }
                active_rotate_center_ = pivot_candidate;
            } else {
                active_rotate_center_.reset();
            }

            // Snapshot reference vectors at lock time.
            position_at_lock_ = camera.position();
            target_at_lock_   = camera.target();
            cumulative_theta_ = 0.0f;
            cumulative_phi_   = 0.0f;
        }

        // Per-frame delta → cumulative angles (Three.js convention).
        constexpr float kTwoPi = 2.0f * PI;
        float angle_h = -kTwoPi * input.delta_x / viewport_height
                      * config_.rotate_speed;
        float angle_v = -kTwoPi * input.delta_y / viewport_height
                      * config_.rotate_speed;
        if (config_.invert_rotate_x) angle_h = -angle_h;
        if (config_.invert_rotate_y) angle_v = -angle_v;

        cumulative_theta_ += angle_h;
        cumulative_phi_   += angle_v;

        const Vec3 pivot =
            active_rotate_center_.has_value()
                ? *active_rotate_center_
                : camera.target();

        // --- Clamp total pitch (phi0 + cumulative) to config limits ---
        // Clamping cumulative_phi_ alone is wrong: phi0 may be 20°–40°,
        // so cumulative ∈ [-85°,+89°] lets total φ exceed 90° → cos(φ)
        // flips sign → azimuth reverses.
        //
        // We clamp total φ = phi0 + cumulative to the configured pitch
        // range (default −85° … +89°).  This keeps cos(φ) ≥ 0.017 even
        // at the limit — enough for θ to have visible effect (no gimbal
        // lock).  Feedback writes cumulative_phi_ back so reverse drag
        // responds immediately (no dead zone).
        {
            const Vec3 offset_pos =
                sub(position_at_lock_, pivot);
            const float r_pos = length(offset_pos);
            if (r_pos > 1.0e-8f) {
                const float phi0 =
                    std::asin(std::clamp(
                        offset_pos.z / r_pos, -1.0f, 1.0f));
                const float phi_raw = phi0 + cumulative_phi_;
                const float phi_clamped = std::clamp(
                    phi_raw,
                    config_.min_pitch,
                    config_.max_pitch);
                cumulative_phi_ = phi_clamped - phi0;
            }
        }

        orbit_around_pivot(
            camera,
            cumulative_theta_,
            cumulative_phi_,
            pivot,
            position_at_lock_,
            target_at_lock_
        );
        adjust_near_far(camera);
        rotated_this_frame = true;
    }

    if (!input.rotate) {
        active_rotate_center_.reset();
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

void CameraController::orbit_around_pivot(
    Camera& camera,
    float cumulative_theta,
    float cumulative_phi,
    const Vec3& pivot,
    const Vec3& position_ref,
    const Vec3& target_ref
) const noexcept {
    /*
     * Apply a cumulative spherical rotation to the reference vectors
     * around |pivot|.  Both position and target participate in the same
     * rotation so the view stays coherent.
     *
     * At cumulative = 0 the output strictly equals (position_ref, target_ref)
     * — no jump.  As the user drags, cumulative grows and the camera orbits
     * smoothly around the locked pivot.
     *
     * Spherical frame: Z-up world.
     *   theta: azimuth in XY plane (from +X toward +Y)
     *   phi:   polar angle from XY plane (0 = horizon, +π/2 = straight up)
     */

    auto rotated = [&](const Vec3& ref) -> Vec3 {
        const Vec3 offset = sub(ref, pivot);
        const float r = length(offset);
        if (r < 1.0e-8f) {
            return ref;  // coincident with pivot — rotation is a no-op
        }

        const float theta0 = std::atan2(offset.y, offset.x);
        const float phi0 =
            std::asin(std::clamp(offset.z / r, -1.0f, 1.0f));

        const float theta = theta0 + cumulative_theta;
        const float phi   = phi0   + cumulative_phi;

        const float cos_phi = std::cos(phi);
        return add(pivot, Vec3{
            r * cos_phi * std::cos(theta),
            r * cos_phi * std::sin(theta),
            r * std::sin(phi)
        });
    };

    const Vec3 new_position = rotated(position_ref);
    const Vec3 new_target   = rotated(target_ref);

    // lookAt recomputes the camera's up from the world-up reference
    // (0,0,1), guaranteeing no roll accumulation (Three.js convention).
    camera.look_at(new_position, new_target, {0.0f, 0.0f, 1.0f});
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
    float scroll_y,
    float viewport_width,
    float viewport_height
) const noexcept {
    /*
     * Zoom toward screen-centre anchor.
     *
     * Every scroll event computes the intersection of the screen-centre ray
     * with the camera-facing plane through the current target.  Both
     * position and target contract toward that anchor — this is the
     * non-singular pivot zoom the user spec asks for.
     *
     * Camera-facing plane: normal = forward, passes through target.
     * Ray–plane angle is always ≈90°, denom ≈ 1, no horizon singularity.
     */
    const Vec3 position = camera.position();
    const Vec3 target   = camera.target();

    // --- Screen-centre anchor (camera-facing plane through target) ---
    const Viewport viewport{
        static_cast<std::uint32_t>(viewport_width),
        static_cast<std::uint32_t>(viewport_height)
    };
    const double centre_x =
        static_cast<double>(viewport_width) * 0.5;
    const double centre_y =
        static_cast<double>(viewport_height) * 0.5;

    const auto anchor_opt =
        MouseRay::intersect_camera_facing_plane(
            centre_x, centre_y, viewport, camera, target);

    // Fallback to current target if intersection fails (should not happen;
    // camera-facing plane is always non-singular).
    const Vec3 pivot = anchor_opt.has_value() ? *anchor_opt : target;

    // --- Zoom factor ---
    const float zoom_factor =
        std::pow(0.75f, scroll_y * config_.zoom_speed);

    // --- Pivot zoom: both position and target contract toward anchor ---
    Vec3 new_position = add(pivot, mul(sub(position, pivot), zoom_factor));
    Vec3 new_target   = add(pivot, mul(sub(target,   pivot), zoom_factor));

    // --- Clamp distance ---
    const Vec3 new_offset = sub(new_position, new_target);
    const float new_dist = length(new_offset);
    const float clamped_dist =
        std::clamp(new_dist, min_distance(), max_distance());

    if (new_dist > 1.0e-6f && clamped_dist != new_dist) {
        const Vec3 dir = normalize(new_offset);
        new_position = add(new_target, mul(dir, clamped_dist));
    }

    camera.look_at(new_position, new_target, camera.up());

    adjust_near_far(camera);
}

void CameraController::adjust_near_far(Camera& camera) const noexcept {
    float scene_radius = 1.0f;
    if (has_bounds_) {
        scene_radius = std::max(
            bounding_sphere_radius(bounds_),
            1.0f
        );
    }

    /*
     * 统一与 Camera::fit_bounds() 相同的深度范围公式：
     *   - far 主要跟随当前 camera distance 收紧，避免被全场景尺度长期主导；
     *   - far 仍保留与包围球半径相关的 padding，保证全景视角不会把远端裁掉；
     *   - near 在基础距离缩放之外，再受 kMaxDepthRatio 硬上限约束，避免
     *     极端缩放时 far/near 比值失控到 1e8 量级。
     */
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
