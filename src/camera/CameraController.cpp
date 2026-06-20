#include "camera/CameraController.hpp"
#include "camera/MouseRay.hpp"

#include <algorithm>
#include <cmath>

namespace gs3d::camera {

namespace {

constexpr float PI = 3.14159265358979323846f;

// Fallback minimum camera distance when no scene bounds are known.
constexpr float MIN_DISTANCE_FALLBACK = 0.01f;

// Fraction of the scene diagonal to use as minimum camera distance.
constexpr float MIN_DISTANCE_SCENE_FRACTION = 0.001f;

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

    bool changed = false;

    if (input.rotate &&
        (input.delta_x != 0.0f || input.delta_y != 0.0f)) {
        rotate_trackball(
            camera,
            input.delta_x,
            input.delta_y,
            viewport_width,
            viewport_height
        );
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
        changed = true;
    }

    if (input.scroll_y != 0.0f) {
        zoom_view(
            camera,
            input.scroll_y,
            input.mouse_x,
            input.mouse_y,
            viewport_width,
            viewport_height
        );
        changed = true;
    }

    return changed;
}

void CameraController::rotate_trackball(
    Camera& camera,
    float delta_x,
    float delta_y,
    float viewport_width,
    float viewport_height
) const noexcept {
    if (viewport_height <= 0.0f) {
        return;
    }

    if (config_.invert_rotate_x) delta_x = -delta_x;
    if (config_.invert_rotate_y) delta_y = -delta_y;

    /*
     * Three.js OrbitControls convention:
     *   angle = 2π × pixels × rotateSpeed / clientHeight
     * Both axes use height so speed is independent of aspect ratio.
     * A full-viewport-height drag = one full 360° orbit.
     */
    constexpr float kTwoPi = 2.0f * PI;
    const float angle_h = -kTwoPi * delta_x / viewport_height
                         * config_.rotate_speed;
    const float angle_v = -kTwoPi * delta_y / viewport_height
                         * config_.rotate_speed;

    // --- Spherical coordinates in world-Z-up frame ---
    // theta: azimuth in XY plane (from +X toward +Y)
    // phi:   polar angle from XY plane (0 = horizon, +π/2 = straight up)
    const Vec3 target = camera.target();
    Vec3 offset       = sub(camera.position(), target);
    float r           = std::max(length(offset), min_distance());

    float theta = std::atan2(offset.y, offset.x);
    float phi   = std::asin(std::clamp(offset.z / r, -1.0f, 1.0f));

    theta += angle_h;
    phi   += angle_v;

    // Clamp polar angle (Three.js style: clamp then makeSafe).
    phi = std::clamp(phi, config_.min_pitch, config_.max_pitch);

    // makeSafe: nudge away from poles to avoid gimbal-lock (Three.js EPS = 1e-6).
    constexpr float kPoleEps = 1.0e-6f;
    const float half_pi     = PI * 0.5f;
    phi = std::clamp(phi, -half_pi + kPoleEps, half_pi - kPoleEps);

    // Spherical → Cartesian.
    const float cos_phi = std::cos(phi);
    offset = Vec3{
        r * cos_phi * std::cos(theta),
        r * cos_phi * std::sin(theta),
        r * std::sin(phi)
    };

    // lookAt recomputes the camera's up from the world-up reference
    // (0,0,1), guaranteeing no roll accumulation (Three.js convention).
    camera.look_at(add(target, offset), target, {0.0f, 0.0f, 1.0f});
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
    float mouse_x,
    float mouse_y,
    float viewport_width,
    float viewport_height
) const noexcept {
    /*
     * 缩放到光标（Potree EarthControls / Three.js zoomToCursor 惯例）：
     * 先把光标射线打到 target 高度的水平面上，取交点作为缩放枢轴，再把
     * 相机位置和 target 一起朝枢轴收缩/拉伸——枢轴点缩放前后停在屏幕同一
     * 位置，而不是像旧实现那样永远朝固定的 target 收缩（那样的效果是
     * "画面内容跟着漂"，必须缩放/平移反复修正才能对准想看的位置）。
     *
     * 射线与水平面几乎平行（贴近地平线视角）或在相机后方时交点退化，
     * fallback 到旧的 target 枢轴。
     */
    const Vec3 position = camera.position();
    const Vec3 target = camera.target();

    Vec3 pivot = target;
    if (viewport_width > 0.0f && viewport_height > 0.0f) {
        const Viewport viewport{
            static_cast<std::uint32_t>(viewport_width),
            static_cast<std::uint32_t>(viewport_height)
        };
        const Ray ray = MouseRay::from_screen(
            mouse_x, mouse_y, viewport, camera
        );
        const auto hit = MouseRay::intersect_plane(
            ray, {0.0f, 0.0f, target.z}, {0.0f, 0.0f, 1.0f}
        );
        if (hit) {
            pivot = *hit;
        }
    }

    // 指数缩放：每格滚轮缩放 ~25%，比默认 12% 更灵敏
    const float zoom_factor =
        std::pow(0.75f, scroll_y * config_.zoom_speed);

    Vec3 new_position = add(pivot, mul(sub(position, pivot), zoom_factor));
    Vec3 new_target   = add(pivot, mul(sub(target,   pivot), zoom_factor));

    // 距离上下限：下限只防除零（允许无限靠近观察点间距），上限绑定到场景
    // 包围盒对角线的若干倍，防止滚轮无限缩远把场景滚出视野。
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
    const float dist = camera.distance();

    float scene_radius = 1.0f;
    if (has_bounds_) {
        const Vec3 ext = sub(bounds_.max, bounds_.min);
        scene_radius = std::max({ ext.x, ext.y, ext.z }) * 0.5f;
    }

    /*
     * 动态近/远平面（参考 Potree/Cesium flyToBoundingSphere 惯例）：
     *   near = distance × 0.001，随距离缩放以保持深度精度，地板降到 1e-4
     *   允许无限放大到 0.1mm 级别观察点间距。
     *   far 不分距离区间——统一用 "距离 + 场景半径的若干倍"，保证远裁面
     *   始终能覆盖整个场景包围球，不随 dist 是否跨过 1.0 而跳变。
     *   （旧版本在 dist<1 时把系数从 scene_radius*4 砍到 scene_radius*0.5，
     *   缩小到 1/8，导致刚放大到很近时场景里离相机较远的点突然被远裁面
     *   切掉、瞬间消失。）
     */
    const float near_plane = std::max(1.0e-4f, dist * 0.001f);

    const float far_plane = std::max(
        near_plane * 1000.0f,
        dist + scene_radius * 4.0f
    );

    camera.set_perspective(
        camera.fov_y_degrees(),
        near_plane,
        far_plane
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
