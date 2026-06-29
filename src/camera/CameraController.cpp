#include "camera/CameraController.hpp"
#include "camera/MouseRay.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

namespace gs3d::camera {

namespace {

constexpr float PI = 3.14159265358979323846f;

// Fallback minimum camera distance when no scene bounds are known.
constexpr float MIN_DISTANCE_FALLBACK = 0.01f;

// Fraction of the scene diagonal to use as minimum camera distance.
constexpr float MIN_DISTANCE_SCENE_FRACTION = 0.001f;

// #region debug-point A:zoom-horizon-helper
struct DebugEndpoint {
    std::string host = "127.0.0.1";
    std::string path = "/event";
    std::string session_id = "zoom-horizon-singularity";
    int port = 7777;
};

std::string json_escape(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size() + 16);
    for (const char ch : text) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        default:
            escaped += ch;
            break;
        }
    }
    return escaped;
}

DebugEndpoint load_debug_endpoint() {
    DebugEndpoint endpoint;

    std::ifstream env_file(".dbg/zoom-horizon-singularity.env");
    if (!env_file) {
        return endpoint;
    }

    std::string line;
    while (std::getline(env_file, line)) {
        const auto pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }

        const std::string key = line.substr(0, pos);
        const std::string value = line.substr(pos + 1);
        if (key == "DEBUG_SESSION_ID") {
            endpoint.session_id = value;
            continue;
        }

        if (key != "DEBUG_SERVER_URL") {
            continue;
        }

        constexpr std::string_view kPrefix = "http://";
        if (value.compare(0, kPrefix.size(), kPrefix) != 0) {
            continue;
        }

        const std::string remainder = value.substr(kPrefix.size());
        const auto slash_pos = remainder.find('/');
        const std::string host_port = remainder.substr(0, slash_pos);
        endpoint.path =
            slash_pos == std::string::npos
            ? "/event"
            : remainder.substr(slash_pos);

        const auto colon_pos = host_port.rfind(':');
        if (colon_pos == std::string::npos) {
            endpoint.host = host_port;
            continue;
        }

        endpoint.host = host_port.substr(0, colon_pos);
        endpoint.port = std::atoi(host_port.substr(colon_pos + 1).c_str());
    }

    return endpoint;
}

void post_debug_event(
    const char* hypothesis_id,
    const char* location,
    const std::string& data_json
) {
    const DebugEndpoint endpoint = load_debug_endpoint();

    std::ostringstream body;
    body << "{"
         << "\"sessionId\":\"" << json_escape(endpoint.session_id) << "\","
         << "\"runId\":\"pre-fix\","
         << "\"hypothesisId\":\"" << json_escape(hypothesis_id) << "\","
         << "\"location\":\"" << json_escape(location) << "\","
         << "\"msg\":\"[DEBUG] zoom horizon diagnostic\","
         << "\"data\":" << data_json
         << "}";

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* result = nullptr;
    const std::string port = std::to_string(endpoint.port);
    if (getaddrinfo(
            endpoint.host.c_str(),
            port.c_str(),
            &hints,
            &result
        ) != 0) {
        return;
    }

    int sock = -1;
    for (addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
        sock = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock == -1) {
            continue;
        }
        if (::connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }
        ::close(sock);
        sock = -1;
    }

    if (sock == -1) {
        freeaddrinfo(result);
        return;
    }

    const std::string body_text = body.str();
    std::ostringstream request;
    request << "POST " << endpoint.path << " HTTP/1.1\r\n"
            << "Host: " << endpoint.host << ':' << endpoint.port << "\r\n"
            << "Content-Type: application/json\r\n"
            << "Content-Length: " << body_text.size() << "\r\n"
            << "Connection: close\r\n\r\n"
            << body_text;

    const std::string request_text = request.str();
    const char* data = request_text.c_str();
    std::size_t remaining = request_text.size();
    while (remaining > 0) {
        const ssize_t written =
            ::send(sock, data, remaining, MSG_NOSIGNAL);
        if (written <= 0) {
            break;
        }
        data += written;
        remaining -= static_cast<std::size_t>(written);
    }

    ::close(sock);
    freeaddrinfo(result);
}

float pitch_degrees(const Vec3& direction) {
    return std::asin(
               std::clamp(direction.z, -1.0f, 1.0f)
           ) *
           180.0f / PI;
}
// #endregion

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
        adjust_near_far(camera);
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
     * Zoom-to-cursor (Three.js OrbitControls convention):
     *
     * 1. Move both position and target forward together along the view axis
     *    (dolly = distance × (1 − zoom_factor)).  Unlike the old approach
     *    that only moved position toward a fixed target (causing it to
     *    converge to distance 0 over repeated zooms), moving both together
     *    keeps camera–target distance invariant: the camera "flies" into
     *    the scene, and successive zoom steps compound instead of stalling.
     * 2. Zoom-to-cursor compensation: intersect the cursor ray with a
     *    camera-facing plane through the *original* target before/after the
     *    dolly.  The world-space delta shifts target + position so the
     *    world point under the cursor stays put on screen.
     * 3. Pitch-angle guard: when the camera pitch is within
     *    kZoomToCursorHorizonThresholdDeg of horizontal, skip the
     *    compensation — the camera-facing plane is nearly vertical and the
     *    geometry is unstable (Three.js #27110).  The forward-together
     *    movement still applies (just without cursor delta).
     * 4. Clamp distance after zoom (safety net; distance is invariant under
     *    the dolly, but the delta compensation can perturb it slightly).
     *
     * This replaces the old horizontal-plane (z = target.z) intersection,
     * which was singular when the view ray was nearly parallel to the
     * ground: ray.direction.z → 0, pivot → ∞.
     */
    const Vec3 position = camera.position();
    const Vec3 target   = camera.target();
    const Vec3 forward  = normalize(sub(target, position));
    const float dist    = length(sub(target, position));

    // --- Zoom factor (unchanged) ---
    const float zoom_factor =
        std::pow(0.75f, scroll_y * config_.zoom_speed);

    // --- Forward-together dolly ---
    // Both position and target advance along the view direction by the same
    // amount, so the camera "flies" into the scene rather than converging
    // toward a fixed point.  Distance is invariant under this step.
    const float dolly = dist * (1.0f - zoom_factor);
    Vec3 new_position = add(position, mul(forward, dolly));
    Vec3 new_target   = add(target,   mul(forward, dolly));

    // --- Zoom-to-cursor compensation ---
    // Three.js OrbitControls: abandon zoom-to-cursor when near the horizon
    // to avoid degenerate geometry on the camera-facing plane.
    constexpr float kZoomToCursorHorizonThresholdDeg = 15.0f;
    const float pitch_deg = pitch_degrees(forward);
    const bool near_horizon =
        std::abs(pitch_deg) < kZoomToCursorHorizonThresholdDeg;

    if (!near_horizon &&
        viewport_width > 0.0f && viewport_height > 0.0f) {
        const Viewport viewport{
            static_cast<std::uint32_t>(viewport_width),
            static_cast<std::uint32_t>(viewport_height)
        };

        // Cursor world position on camera-facing plane through original
        // target BEFORE the dolly (plane normal = forward, point = target).
        const auto cursor_before =
            MouseRay::intersect_camera_facing_plane(
                mouse_x, mouse_y, viewport, camera, target);

        // Camera state AFTER dolly: new position AND new target (both
        // moved forward together).  FOV / near / far / up are unchanged.
        // ponytail: Camera is a value type — default copy is correct.
        Camera temp_camera = camera;
        temp_camera.set_position(new_position);
        temp_camera.set_target(new_target);

        // Intersect with the SAME plane (point = original target, normal =
        // original forward) so the delta is the screen-space drift of the
        // cursor's world point caused by the dolly.
        const auto cursor_after =
            MouseRay::intersect_camera_facing_plane(
                mouse_x, mouse_y, viewport, temp_camera, target);

        if (cursor_before && cursor_after) {
            const Vec3 delta = sub(*cursor_before, *cursor_after);
            new_position = add(new_position, delta);
            new_target   = add(new_target,   delta);
        }

        // #region debug-point A:zoom-horizon-entry
        {
            const Vec3 pt = cursor_before.value_or(target);
            std::ostringstream data;
            data << "{"
                 << "\"pitch_deg\":" << pitch_deg << ','
                 << "\"near_horizon\":false,"
                 << "\"scroll_y\":" << scroll_y << ','
                 << "\"mouse_x\":" << mouse_x << ','
                 << "\"mouse_y\":" << mouse_y << ','
                 << "\"cursor_before_x\":" << pt.x << ','
                 << "\"cursor_before_y\":" << pt.y << ','
                 << "\"cursor_before_z\":" << pt.z << ','
                 << "\"camera_distance_before\":" << camera.distance()
                 << "}";
            post_debug_event(
                "A",
                "CameraController.cpp:zoom_view",
                data.str()
            );
        }
        // #endregion
    } else {
        // #region debug-point A:zoom-horizon-entry (fallback)
        {
            const char* reason =
                near_horizon ? "near_horizon" : "zero_viewport";
            std::ostringstream data;
            data << "{"
                 << "\"pitch_deg\":" << pitch_deg << ','
                 << "\"near_horizon\":"
                 << (near_horizon ? "true" : "false") << ','
                 << "\"reason\":\"" << reason << "\","
                 << "\"scroll_y\":" << scroll_y << ','
                 << "\"camera_distance_before\":" << camera.distance()
                 << "}";
            post_debug_event(
                "A",
                "CameraController.cpp:zoom_view",
                data.str()
            );
        }
        // #endregion
    }

    // --- Clamp distance ---
    // Lower bound: prevent division-by-zero.
    // Upper bound: prevent scrolling the scene out of view (bound to scene
    // bounding-box diagonal, or unbounded when no bounds set).
    const Vec3 new_offset = sub(new_position, new_target);
    const float new_dist = length(new_offset);
    const float clamped_dist =
        std::clamp(new_dist, min_distance(), max_distance());

    if (new_dist > 1.0e-6f && clamped_dist != new_dist) {
        const Vec3 dir = normalize(new_offset);
        new_position = add(new_target, mul(dir, clamped_dist));
    }

    // #region debug-point C:zoom-horizon-result
    {
        const float position_delta =
            length(sub(new_position, position));
        const float target_delta =
            length(sub(new_target, target));
        std::ostringstream data;
        const bool was_clamped = (clamped_dist != new_dist);
        data << "{"
             << "\"zoom_factor\":" << zoom_factor << ','
             << "\"old_distance\":" << camera.distance() << ','
             << "\"new_distance_before_clamp\":" << new_dist << ','
             << "\"new_distance_after_clamp\":" << clamped_dist << ','
             << "\"min_distance\":" << min_distance() << ','
             << "\"max_distance\":" << max_distance() << ','
             << "\"was_clamped\":" << (was_clamped ? "true" : "false") << ','
             << "\"near_plane\":" << camera.near_plane() << ','
             << "\"far_plane\":" << camera.far_plane() << ','
             << "\"position_delta\":" << position_delta << ','
             << "\"target_delta\":" << target_delta << ','
             << "\"new_position_x\":" << new_position.x << ','
             << "\"new_position_y\":" << new_position.y << ','
             << "\"new_position_z\":" << new_position.z << ','
             << "\"new_target_x\":" << new_target.x << ','
             << "\"new_target_y\":" << new_target.y << ','
             << "\"new_target_z\":" << new_target.z
             << "}";
        post_debug_event(
            "C",
            "CameraController.cpp:zoom_view",
            data.str()
        );
    }
    // #endregion

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
