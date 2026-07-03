#include "app/AppConfig.hpp"
#include "app/PreprocessedBundle.hpp"
#include "data/Gs3dLodDataset.hpp"
#include "preprocess/CsvToGs3dConverter.hpp"
#include "preprocess/Gs3dLodWriter.hpp"
#include "preprocess/Gs3dTileWriter.hpp"
#include "util/Stopwatch.hpp"

#include <exception>
#include <filesystem>
#include <iostream>

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
    gs3d::app::AppConfig& app_config
) {
    if (app_config.bundle_dir.empty()) {
        app_config.bundle_dir =
            gs3d::app::derive_bundle_paths(
                app_config.csv_input_path
            ).bundle_dir;
    }

    auto paths =
        gs3d::app::make_bundle_paths(app_config.bundle_dir);
    paths.lod_enabled = app_config.viewer.lod_enabled;
    paths.tile_enabled = app_config.viewer.tile_enabled;
    return paths;
}

} // namespace

int main(int argc, char** argv) {
    try {
        auto app_config =
            gs3d::app::AppConfigLoader::load_from_args(argc, argv);

        if (app_config.input_mode != "csv" &&
            app_config.input_mode != "dat") {
            std::cerr << "[FAIL] input.mode must be \"csv\" "
                         "or \"dat\" for preprocessing.\n";
            return 1;
        }

        if (app_config.csv_input_path.empty()) {
            std::cerr << "[FAIL] an input CSV/DAT path is required.\n";
            return 1;
        }

        const auto bundle_paths =
            resolve_bundle_paths(app_config);
        std::filesystem::create_directories(bundle_paths.bundle_dir);
        gs3d::app::apply_bundle_paths(
            app_config.viewer,
            bundle_paths
        );

        gs3d::util::Stopwatch preprocess_timer;

        std::cout << "[PREPROCESS] input_path = "
                  << app_config.csv_input_path.string() << '\n';
        std::cout << "[PREPROCESS] bundle_dir = "
                  << bundle_paths.bundle_dir.string() << '\n';
        std::cout << "[PREPROCESS] gs3d_path = "
                  << app_config.viewer.gs3d_path.string() << '\n';

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
            csv_read_config, csv_chunk_plan_config);
        gs3d::util::Stopwatch convert_timer;
        auto [convert_result, dataset] = converter.convert(
            app_config.csv_input_path,
            app_config.viewer.gs3d_path);

        std::cout << "[PREPROCESS] written_points = "
                  << convert_result.written_points << '\n';
        std::cout << "[PREPROCESS] invalid_records = "
                  << convert_result.invalid_records << '\n';
        std::cout << "[TIME] csv_convert_seconds = "
                  << convert_timer.elapsed_seconds() << '\n';

        if (app_config.viewer.lod_enabled) {
            gs3d::util::Stopwatch lod_timer;
            const auto lod_dataset =
                gs3d::data::Gs3dLodDataset::build(
                    dataset,
                    make_lod_build_config(app_config.viewer)
                );
            const auto lod_stats =
                gs3d::preprocess::Gs3dLodWriter::write(
                    app_config.viewer.lod_sidecar_path,
                    lod_dataset
                );

            std::cout << "[PREPROCESS] lod_levels = "
                      << lod_stats.level_count << '\n';
            std::cout << "[TIME] lod_write_seconds = "
                      << lod_timer.elapsed_seconds() << '\n';
        }

        if (app_config.viewer.tile_enabled) {
            gs3d::preprocess::Gs3dTileWriteConfig tile_config;
            tile_config.num_threads =
                app_config.tile_build.num_threads;
            tile_config.verbose =
                app_config.viewer.tile_verbose;

            gs3d::util::Stopwatch tile_timer;
            const auto tile_stats =
                gs3d::preprocess::Gs3dTileWriter::write(
                    app_config.viewer.tile_index_path,
                    app_config.viewer.tile_data_path,
                    dataset, tile_config);

            if (!tile_stats.success ||
                tile_stats.total_points != dataset.point_count()) {
                throw std::runtime_error(
                    "preprocessing verification failed: tile point count "
                    "does not match GS3D point count"
                );
            }

            std::cout << "[PREPROCESS] tile_count = "
                      << tile_stats.tile_count << '\n';
            std::cout << "[TIME] tile_write_seconds = "
                      << tile_timer.elapsed_seconds() << '\n';
        }

        gs3d::app::write_bundle_manifest(
            bundle_paths,
            app_config,
            dataset
        );

        std::cout << "[TIME] total_seconds = "
                  << preprocess_timer.elapsed_seconds() << '\n';
        std::cout << "[OK] preprocessing complete.\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "[FAIL] " << e.what() << '\n';
        return 1;
    }
}
