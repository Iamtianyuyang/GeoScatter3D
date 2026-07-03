#include "app/AppConfig.hpp"
#include "preprocess/CsvToGs3dConverter.hpp"
#include "preprocess/Gs3dTileWriter.hpp"
#include "util/Stopwatch.hpp"

#include <exception>
#include <filesystem>
#include <iostream>

namespace {

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

        apply_generated_paths_from_csv(
            app_config.viewer, app_config.csv_input_path);

        gs3d::util::Stopwatch preprocess_timer;

        std::cout << "[PREPROCESS] input_path = "
                  << app_config.csv_input_path.string() << '\n';
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

        if (!app_config.viewer.lod_sidecar_path.empty() &&
            std::filesystem::exists(
                app_config.viewer.lod_sidecar_path)) {
            std::error_code ec;
            const bool removed = std::filesystem::remove(
                app_config.viewer.lod_sidecar_path, ec);
            if (removed) {
                std::cout << "[PREPROCESS] removed stale lod "
                             "sidecar = "
                          << app_config.viewer.lod_sidecar_path
                                 .string()
                          << '\n';
            } else if (ec) {
                std::cout << "[WARN] Failed to remove stale LOD "
                             "sidecar: "
                          << ec.message() << '\n';
            }
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

        std::cout << "[TIME] total_seconds = "
                  << preprocess_timer.elapsed_seconds() << '\n';
        std::cout << "[OK] preprocessing complete.\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "[FAIL] " << e.what() << '\n';
        return 1;
    }
}
