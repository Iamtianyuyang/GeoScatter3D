#include "app/AppConfig.hpp"
#include "app/ViewerApp.hpp"

#include <exception>
#include <iostream>

namespace {

gs3d::app::ViewerAppConfig make_viewer_config(
    const gs3d::app::AppConfig& app_config
) {
    gs3d::app::ViewerAppConfig viewer = app_config.viewer;

    viewer.clear_color =
        app_config.render.clear_color;

    viewer.initial_point_size =
        app_config.render.initial_point_size;

    viewer.camera_mode =
        app_config.camera.mode;

    viewer.camera_position =
        app_config.camera.position;

    viewer.camera_target =
        app_config.camera.target;

    viewer.camera_up =
        app_config.camera.up;

    viewer.camera_fov_y =
        app_config.camera.fov_y;

    viewer.camera_near =
        app_config.camera.near_plane;

    viewer.camera_far =
        app_config.camera.far_plane;

    viewer.controller_rotate_speed =
        app_config.controller.rotate_speed;

    viewer.controller_pan_speed =
        app_config.controller.pan_speed;

    viewer.controller_zoom_speed =
        app_config.controller.zoom_speed;

    viewer.controller_invert_rotate_x =
        app_config.controller.invert_rotate_x;

    viewer.controller_invert_rotate_y =
        app_config.controller.invert_rotate_y;

    viewer.controller_invert_pan_x =
        app_config.controller.invert_pan_x;

    viewer.controller_invert_pan_y =
        app_config.controller.invert_pan_y;

    return viewer;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto app_config =
            gs3d::app::AppConfigLoader::load_from_args(
                argc,
                argv
            );

        gs3d::app::AppConfigPrinter::print(app_config);
        
        const auto viewer_config =
            make_viewer_config(app_config);


        gs3d::app::ViewerApp app(viewer_config);

        return app.run();

    } catch (const std::exception& e) {
        std::cerr << "[FAIL] " << e.what() << '\n';
        return 1;
    }
}