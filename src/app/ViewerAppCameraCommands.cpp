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
    ctx.camera_hub.propagate(
        gui_cmds.reset_camera_index,
        [&](int src, int dst) {
            ctx.controllers.at(dst).copy_pivot_from(
                ctx.controllers.at(src));
        });
    ctx.streaming_viewport_index =
        gui_cmds.reset_camera_index;
    ctx.tile_selection_dirty = true;
}


void ViewerApp::apply_camera_view_axis_command(
    const UiActions& gui_cmds,
    ViewerAppCameraCommandContext& ctx
) {
    if (gui_cmds.camera_view_axis < 0 || gui_cmds.camera_view_axis >= 6) return;
    int vi = ctx.streaming_viewport_index;
    if (vi < 0 || vi >= ctx.n_viewports) return;
    auto& camera = ctx.viewport_manager.camera(vi);
    ctx.controllers[static_cast<std::size_t>(vi)].clear_orbit_pivot();
    static constexpr gs3d::camera::Vec3 kDirs[6] = {
        {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    const auto& d = kDirs[static_cast<std::size_t>(gui_cmds.camera_view_axis)];
    float dist = std::max(camera.distance(), 1e-3f);
    gs3d::camera::Vec3 up = std::abs(d.z) > 0.999f
        ? gs3d::camera::Vec3{0,1,0} : gs3d::camera::Vec3{0,0,1};
    camera.look_at({camera.target().x+d.x*dist, camera.target().y+d.y*dist, camera.target().z+d.z*dist}, camera.target(), up);
    ctx.camera_hub.propagate(vi, [&](int src, int dst) { ctx.controllers.at(dst).copy_pivot_from(ctx.controllers.at(src)); });
    ctx.tile_selection_dirty = true;
}



} // namespace gs3d::app
