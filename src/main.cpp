#include "app/AppConfig.hpp"
#include "app/PreprocessedBundle.hpp"
#include "app/RecentProjects.hpp"
#include "app/ViewerApp.hpp"
#include "app/WelcomeWindow.hpp"
#include "data/Gs3dLodDataset.hpp"
#include "preprocess/CsvToGs3dConverter.hpp"
#include "preprocess/Gs3dLodWriter.hpp"
#include "preprocess/Gs3dTileWriter.hpp"
#include "util/Stopwatch.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

namespace {

[[nodiscard]]
gs3d::data::Gs3dLodBuildConfig make_lod_build_config(
    const gs3d::app::ViewerAppConfig& viewer
) {
    gs3d::data::Gs3dLodBuildConfig config;
    config.include_full_resolution_level = false;
    config.finest_target_points = viewer.lod_finest_target_points;
    config.growth_factor = viewer.lod_growth_factor;
    config.min_points_per_level = viewer.lod_min_points_per_level;
    config.voxel_scale = viewer.lod_voxel_scale;
    config.verbose = viewer.lod_verbose;

    if (viewer.lod_voxel_mode == "XYZ") {
        config.voxel_mode = gs3d::data::Gs3dLodVoxelMode::XYZ;
    } else {
        config.voxel_mode = gs3d::data::Gs3dLodVoxelMode::XY;
    }

    return config;
}

[[nodiscard]]
gs3d::app::PreprocessedBundlePaths resolve_bundle_paths(
    gs3d::app::AppConfig& app_config,
    const std::filesystem::path& source_path
) {
    if (app_config.bundle_dir.empty()) {
        app_config.bundle_dir =
            gs3d::app::derive_bundle_paths(source_path).bundle_dir;
    }

    auto paths =
        gs3d::app::make_bundle_paths(app_config.bundle_dir);
    paths.lod_enabled = app_config.viewer.lod_enabled;
    paths.tile_enabled = app_config.viewer.tile_enabled;
    return paths;
}

void preprocess_csv_input(
    gs3d::app::AppConfig& app_config
) {
    if (app_config.input_mode != "csv" &&
        app_config.input_mode != "dat") {
        return;
    }

    if (app_config.csv_input_path.empty()) {
        throw std::runtime_error(
            "AppConfig: input.mode is \"csv\" but input.csv_path is empty"
        );
    }

    auto bundle_paths =
        resolve_bundle_paths(
            app_config,
            app_config.csv_input_path
        );
    std::filesystem::create_directories(bundle_paths.bundle_dir);
    gs3d::app::apply_bundle_paths(app_config.viewer, bundle_paths);

    gs3d::util::Stopwatch preprocess_timer;

    std::cout << "[PREPROCESS] csv_path = "
              << app_config.csv_input_path.string()
              << '\n';
    std::cout << "[PREPROCESS] bundle_dir = "
              << bundle_paths.bundle_dir.string()
              << '\n';
    std::cout << "[PREPROCESS] gs3d_path = "
              << app_config.viewer.gs3d_path.string()
              << '\n';

    gs3d::data::CsvChunkPlanConfig csv_chunk_plan_config;
    csv_chunk_plan_config.num_threads =
        app_config.csv_convert.num_threads;
    csv_chunk_plan_config.target_chunk_bytes =
        app_config.csv_convert.chunk_bytes;
    csv_chunk_plan_config.min_parallel_file_bytes =
        app_config.csv_convert.min_parallel_file_bytes;

    gs3d::data::CsvReadConfig csv_read_config;
    csv_read_config.schema.x_field =
        app_config.csv_convert.x_field;
    csv_read_config.schema.y_field =
        app_config.csv_convert.y_field;
    csv_read_config.schema.z_field =
        app_config.csv_convert.z_field;
    csv_read_config.schema.primary_value_field =
        app_config.csv_convert.primary_value_field;

    gs3d::preprocess::CsvToGs3dConverter converter(
        csv_read_config,
        csv_chunk_plan_config
    );
    gs3d::util::Stopwatch convert_timer;
    auto [convert_result, dataset] =
        converter.convert(
            app_config.csv_input_path,
            app_config.viewer.gs3d_path
        );

    std::cout << "[PREPROCESS] written_points = "
              << convert_result.written_points
              << '\n';
    std::cout << "[TIME] preprocess.csv_convert_seconds = "
              << convert_timer.elapsed_seconds()
              << '\n';

    if (app_config.viewer.lod_enabled) {
        gs3d::util::Stopwatch lod_timer;
        const auto lod_dataset =
            gs3d::data::Gs3dLodDataset::build(
                dataset,
                make_lod_build_config(app_config.viewer)
            );
        if (lod_dataset.empty()) {
            app_config.viewer.lod_enabled = false;
            bundle_paths.lod_enabled = false;
            std::cout
                << "[PREPROCESS] LOD skipped: dataset is below "
                << "the configured minimum point count.\n";
        } else {
            const auto lod_stats =
                gs3d::preprocess::Gs3dLodWriter::write(
                    app_config.viewer.lod_sidecar_path,
                    lod_dataset
                );
            std::cout << "[PREPROCESS] lod_levels = "
                      << lod_stats.level_count
                      << '\n';
        }
        std::cout << "[TIME] preprocess.lod_write_seconds = "
                  << lod_timer.elapsed_seconds()
                  << '\n';
    }

    if (app_config.viewer.tile_enabled) {
        gs3d::preprocess::Gs3dTileWriteConfig tile_config;
        tile_config.num_threads = app_config.tile_build.num_threads;
        tile_config.verbose = app_config.viewer.tile_verbose;

        gs3d::util::Stopwatch tile_timer;
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
        std::cout << "[TIME] preprocess.tile_write_seconds = "
                  << tile_timer.elapsed_seconds()
                  << '\n';
    }

    gs3d::app::write_bundle_manifest(
        bundle_paths,
        app_config,
        dataset
    );

    std::cout << "[TIME] preprocess.total_seconds = "
              << preprocess_timer.elapsed_seconds()
              << '\n';
}

void load_bundle_input(
    gs3d::app::AppConfig& app_config
) {
    if (app_config.input_mode != "bundle") {
        return;
    }

    if (app_config.bundle_dir.empty()) {
        throw std::runtime_error(
            "AppConfig: input.mode is \"bundle\" but input.bundle_dir is "
            "empty"
        );
    }

    const auto bundle_paths =
        gs3d::app::load_bundle_manifest(app_config.bundle_dir);
    gs3d::app::apply_bundle_paths(app_config.viewer, bundle_paths);
}

gs3d::app::ViewerAppConfig make_viewer_config(
    const gs3d::app::AppConfig& app_config
) {
    gs3d::app::ViewerAppConfig viewer = app_config.viewer;

    viewer.clear_color =
        app_config.render.clear_color;

    viewer.initial_point_size =
        app_config.render.initial_point_size;

    viewer.primary_value_field_name =
        app_config.csv_convert.primary_value_field;

    viewer.z_field_name =
        app_config.csv_convert.z_field;

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

std::filesystem::path project_directory_from_selection(
    const std::filesystem::path& selection
) {
    if (selection.filename() == "manifest.toml") {
        return selection.parent_path();
    }
    return selection;
}

void apply_open_request(
    gs3d::app::AppConfig& app_config,
    const gs3d::app::ViewerOpenRequest& request
) {
    if (request.kind ==
        gs3d::app::ViewerOpenRequestKind::Project) {
        app_config.input_mode = "bundle";
        app_config.bundle_dir =
            project_directory_from_selection(request.path);
        app_config.csv_input_path.clear();
        return;
    }

    const auto extension = request.path.extension().string();
    app_config.input_mode =
        extension == ".dat" || extension == ".DAT"
            ? "dat"
            : "csv";
    app_config.csv_input_path = request.path;
    app_config.bundle_dir.clear();
}

} // namespace

int main(int argc, char** argv) {
    try {
        auto app_config =
            gs3d::app::AppConfigLoader::load_from_args(
                argc,
                argv
            );

        bool show_welcome_window =
            !app_config.viewer.benchmark_mode;
        for (;;) {
            if (show_welcome_window) {
                std::filesystem::path current_path;
                if (!app_config.bundle_dir.empty()) {
                    current_path = app_config.bundle_dir;
                } else if (!app_config.csv_input_path.empty()) {
                    current_path = app_config.csv_input_path;
                } else {
                    current_path = app_config.viewer.gs3d_path;
                }

                gs3d::app::WelcomeWindow welcome({
                    .enable_validation_layers =
                        app_config.viewer.enable_validation_layers,
                    .ui_scale_multiplier =
                        app_config.viewer.ui_scale_multiplier,
                    .current_path = std::move(current_path),
                    .recent_projects =
                        gs3d::app::load_recent_projects()
                });
                const auto welcome_result = welcome.run();
                if (welcome_result.kind ==
                    gs3d::app::WelcomeWindowResultKind::Cancelled) {
                    return 0;
                }
                if (welcome_result.kind ==
                    gs3d::app::WelcomeWindowResultKind::OpenProject) {
                    apply_open_request(
                        app_config,
                        {
                            .kind =
                                gs3d::app::ViewerOpenRequestKind::Project,
                            .path = welcome_result.path
                        }
                    );
                } else if (
                    welcome_result.kind ==
                    gs3d::app::WelcomeWindowResultKind::OpenRawData
                ) {
                    apply_open_request(
                        app_config,
                        {
                            .kind =
                                gs3d::app::ViewerOpenRequestKind::RawData,
                            .path = welcome_result.path
                        }
                    );
                }
                show_welcome_window = false;
            }

            preprocess_csv_input(app_config);
            load_bundle_input(app_config);

            if (!app_config.bundle_dir.empty()) {
                gs3d::app::remember_recent_project(
                    app_config.bundle_dir
                );
            }

            gs3d::app::AppConfigPrinter::print(app_config);

            auto viewer_config =
                make_viewer_config(app_config);

            gs3d::app::ViewerApp app(viewer_config);
            const int result = app.run();
            if (result != 0 || !app.open_request().has_value()) {
                return result;
            }

            if (app.open_request()->kind ==
                gs3d::app::ViewerOpenRequestKind::Welcome) {
                show_welcome_window = true;
                continue;
            }
            apply_open_request(app_config, *app.open_request());
        }
    } catch (const std::exception& e) {
        std::cerr << "[FAIL] " << e.what() << '\n';
        return 1;
    }
}
