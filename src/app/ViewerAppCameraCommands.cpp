#include "app/ViewerApp.hpp"
#include "app/ViewerAppRunState.hpp"

#include "app/AppState.hpp"
#include "app/UiActions.hpp"
#include "camera/Camera.hpp"
#include "camera/CameraController.hpp"
#include "camera/CameraHub.hpp"
#include "render/ViewportManager.hpp"

#include <array>
#include <cmath>

namespace gs3d::app {

namespace {

gs3d::camera::Vec3 to_vec3(
    const std::array<float, 3>& value
) {
    return {
        value[0],
        value[1],
        value[2]
    };
}

} // namespace

void initialize_camera_from_config(
    gs3d::camera::Camera& camera,
    const ViewerCameraConfig& config,
    const gs3d::camera::CameraBounds& bounds
) {
    // Default: orthographic projection, fit to data bounds.
    // fit_bounds() sets ortho_height, near/far, position, target, up.
    camera.set_orthographic(10.0f, 0.01f, 10000.0f);

    if (config.mode == "fit") {
        camera.fit_bounds(bounds);
        return;
    }

    // Explicit camera overrides: still use ortho by default.
    camera.look_at(
        to_vec3(config.position),
        to_vec3(config.target),
        to_vec3(config.up)
    );
    // Derive ortho_height from distance and FOV for backwards compat.
    const float fov_rad = config.fov_y * 3.14159265f / 180.0f;
    const float view_h =
        2.0f * camera.distance() * std::tan(fov_rad * 0.5f);
    // Ortho near/far: small near, huge far — covers any practical depth.
    camera.set_orthographic(view_h, 0.01f, 1.0e7f);
}

void ViewerApp::apply_reset_camera_command(
    const UiActions& gui_cmds,
    ViewerAppCameraCommandContext& ctx
) {
    if (gui_cmds.reset_camera_index < 0 ||
        gui_cmds.reset_camera_index >= ctx.n_viewports) {
        return;
    }
    ctx.controllers[
        static_cast<std::size_t>(gui_cmds.reset_camera_index)
    ].clear_orbit_pivot();
    initialize_camera_from_config(
        ctx.viewport_manager.camera(gui_cmds.reset_camera_index),
        ctx.camera_config,
        ctx.bounds
    );
    ctx.camera_hub.propagate(gui_cmds.reset_camera_index);
    ctx.streaming_viewport_index =
        gui_cmds.reset_camera_index;
    ctx.tile_selection_dirty = true;
}

} // namespace gs3d::app
