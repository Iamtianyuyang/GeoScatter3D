#include "app/AppConfig.hpp"
#include "app/ViewerApp.hpp"
#include "data/Gs3dDataset.hpp"
#include "preprocess/Gs3dTileWriter.hpp"
#include "preprocess/Gs3dWriter.hpp"
#include "preprocess/StatisticsPass.hpp"

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

void apply_generated_paths_from_csv(
    gs3d::app::ViewerAppConfig& viewer,
    const std::filesystem::path& csv_path
) {
    viewer.gs3d_path = csv_path;
    viewer.gs3d_path.replace_extension(".gs3d");

    viewer.lod_sidecar_path = csv_path;
    viewer.lod_sidecar_path.replace_extension(".gs3dlod");

    viewer.tile_data_path = csv_path;
    viewer.tile_data_path.replace_extension(".gs3dtiles");

    viewer.tile_index_path = csv_path;
    viewer.tile_index_path.replace_extension(".gs3dtiles.index");
}

void preprocess_csv_input(
    gs3d::app::AppConfig& app_config
) {
    if (app_config.input_mode != "csv") {
        return;
    }

    if (app_config.csv_input_path.empty()) {
        throw std::runtime_error(
            "AppConfig: input.mode is \"csv\" but input.csv_path is empty"
        );
    }

    apply_generated_paths_from_csv(
        app_config.viewer,
        app_config.csv_input_path
    );

    std::cout << "[PREPROCESS] csv_path = "
              << app_config.csv_input_path.string()
              << '\n';
    std::cout << "[PREPROCESS] gs3d_path = "
              << app_config.viewer.gs3d_path.string()
              << '\n';

    gs3d::preprocess::StatisticsPass statistics_pass;
    const auto statistics =
        statistics_pass.run(app_config.csv_input_path);

    gs3d::preprocess::Gs3dWriter writer;
    const auto write_result =
        writer.write(
            app_config.csv_input_path,
            app_config.viewer.gs3d_path,
            statistics
        );

    std::cout << "[PREPROCESS] written_points = "
              << write_result.written_points
              << '\n';

    if (app_config.viewer.tile_enabled) {
        const auto dataset =
            gs3d::data::Gs3dDatasetLoader::load(
                app_config.viewer.gs3d_path
            );

        gs3d::preprocess::Gs3dTileWriteConfig tile_config;
        tile_config.num_threads = 0;
        tile_config.verbose = app_config.viewer.tile_verbose;

        const auto tile_stats =
            gs3d::preprocess::Gs3dTileWriter::write(
                app_config.viewer.tile_index_path,
                app_config.viewer.tile_data_path,
                dataset,
                tile_config
            );

        std::cout << "[PREPROCESS] tile_count = "
                  << tile_stats.tile_count
                  << '\n';
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        auto app_config =
            gs3d::app::AppConfigLoader::load_from_args(
                argc,
                argv
            );

        preprocess_csv_input(app_config);

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
