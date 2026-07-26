#include "app/AppConfig.hpp"
#include "util/Log.hpp"
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
    const gs3d::app::ViewerAppConfig& viewer,
    std::uint32_t num_threads
) {
    gs3d::data::Gs3dLodBuildConfig config;
    config.include_full_resolution_level = false;
    config.finest_target_points = viewer.lod.finest_target_points;
    config.growth_factor = viewer.lod.growth_factor;
    config.min_points_per_level = viewer.lod.min_points_per_level;
    config.voxel_scale = viewer.lod.voxel_scale;
    config.num_threads = num_threads;
    config.verbose = viewer.lod.verbose;

    if (viewer.lod.voxel_mode == "XYZ") {
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
    paths.lod_enabled = app_config.viewer.lod.enabled;
    paths.tile_enabled = app_config.viewer.tile.enabled;
    return paths;
}

} // namespace

int main(int argc, char** argv) {
    try {
        auto app_config =
            gs3d::app::AppConfigLoader::load_from_args(argc, argv);

        if (app_config.input_mode != "csv" &&
            app_config.input_mode != "dat") {
            gs3d::util::log::error() << "[FAIL] input.mode must be \"csv\" "
                         "or \"dat\" for preprocessing.\n";
            return 1;
        }

        if (app_config.csv_input_path.empty()) {
            gs3d::util::log::error() << "[FAIL] an input CSV/DAT path is required.\n";
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

        gs3d::util::log::info() << "[PREPROCESS] input_path = "
                  << app_config.csv_input_path.string() << '\n';
        gs3d::util::log::info() << "[PREPROCESS] bundle_dir = "
                  << bundle_paths.bundle_dir.string() << '\n';
        gs3d::util::log::info() << "[PREPROCESS] gs3d_path = "
                  << app_config.viewer.input.gs3d_path.string() << '\n';

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
            app_config.viewer.input.gs3d_path);

        gs3d::util::log::info() << "[PREPROCESS] written_points = "
                  << convert_result.written_points << '\n';
        gs3d::util::log::info() << "[PREPROCESS] invalid_records = "
                  << convert_result.invalid_records << '\n';
        gs3d::util::log::info() << "[TIME] csv_convert_seconds = "
                  << convert_timer.elapsed_seconds() << '\n';

        if (app_config.viewer.lod.enabled) {
            gs3d::util::Stopwatch lod_timer;
            const auto lod_dataset =
                gs3d::data::Gs3dLodDataset::build(
                    dataset,
                    make_lod_build_config(
                        app_config.viewer,
                        app_config.csv_convert.num_threads
                    )
                );
            const auto lod_stats =
                gs3d::preprocess::Gs3dLodWriter::write(
                    app_config.viewer.lod.sidecar_path,
                    lod_dataset
                );

            gs3d::util::log::info() << "[PREPROCESS] lod_levels = "
                      << lod_stats.level_count << '\n';
            gs3d::util::log::info() << "[TIME] lod_write_seconds = "
                      << lod_timer.elapsed_seconds() << '\n';
        }

        if (app_config.viewer.tile.enabled) {
            gs3d::preprocess::Gs3dTileWriteConfig tile_config;
            tile_config.num_threads =
                app_config.tile_build.num_threads;
            tile_config.verbose =
                app_config.viewer.tile.verbose;

            gs3d::util::Stopwatch tile_timer;
            const auto tile_stats =
                gs3d::preprocess::Gs3dTileWriter::write(
                    app_config.viewer.tile.index_path,
                    app_config.viewer.tile.data_path,
                    dataset, tile_config);

            if (!tile_stats.success ||
                tile_stats.total_points != dataset.point_count()) {
                throw std::runtime_error(
                    "preprocessing verification failed: tile point count "
                    "does not match GS3D point count"
                );
            }

            gs3d::util::log::info() << "[PREPROCESS] tile_count = "
                      << tile_stats.tile_count << '\n';
            gs3d::util::log::info() << "[TIME] tile_write_seconds = "
                      << tile_timer.elapsed_seconds() << '\n';
        }

        gs3d::app::write_bundle_manifest(
            bundle_paths,
            app_config,
            dataset
        );

        gs3d::util::log::info() << "[TIME] total_seconds = "
                  << preprocess_timer.elapsed_seconds() << '\n';
        gs3d::util::log::info() << "[OK] preprocessing complete.\n";
        return 0;

    } catch (const std::exception& e) {
        gs3d::util::log::error() << "[FAIL] " << e.what() << '\n';
        return 1;
    }
}
