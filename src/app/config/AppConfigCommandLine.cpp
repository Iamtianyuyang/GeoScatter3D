#include "app/AppConfig.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>

namespace gs3d::app {
namespace {
std::string argument_at(int argc, char** argv, int index) {
    return index >= 0 && index < argc && argv[index] ? std::string(argv[index]) : std::string{};
}
[[nodiscard]]
bool has_flag(int argc, char** argv, const std::string& flag) {
    for (int i = 1; i < argc; ++i) {
        if (argument_at(argc, argv, i) == flag) {
            return true;
        }
    }

    return false;
}

} // namespace

void AppConfigLoader::apply_command_line_overrides(
    AppConfig& config,
    int argc,
    char** argv
) {
    if (has_flag(argc, argv, "--no-validation")) {
        config.viewer.graphics.enable_validation_layers = false;
    }

    if (has_flag(argc, argv, "--validation")) {
        config.viewer.graphics.enable_validation_layers = true;
    }
    if (has_flag(argc, argv, "--no-tiles")) config.viewer.tile.enabled = false;
    if (has_flag(argc, argv, "--no-lod")) {
        config.viewer.lod.enabled = false;
        config.viewer.lod.auto_load_sidecar = false;
    }

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argument_at(argc, argv, i);

        if (arg == "--threads" || arg == "--finest-target-points") {
            if (i + 1 >= argc) throw std::runtime_error("AppConfig: " + arg + " requires a positive integer");
            const auto text = argument_at(argc, argv, i + 1);
            if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos) {
                throw std::runtime_error("AppConfig: invalid integer for " + arg);
            }
            const auto value = std::stoull(text);
            if (value == 0 || (arg == "--threads" && value > 256)) {
                throw std::runtime_error("AppConfig: value out of range for " + arg);
            }
            if (arg == "--threads") {
                config.csv_convert.num_threads = static_cast<std::uint32_t>(value);
                config.tile_build.num_threads = static_cast<std::uint32_t>(value);
            } else {
                config.viewer.lod.enabled = true;
                config.viewer.lod.finest_target_points = value;
            }
        }
        if (arg == "--voxel-mode") {
            if (i + 1 >= argc) throw std::runtime_error("AppConfig: --voxel-mode requires XY or XYZ");
            const auto mode = argument_at(argc, argv, i + 1);
            if (mode != "XY" && mode != "XYZ") throw std::runtime_error("AppConfig: --voxel-mode requires XY or XYZ");
            config.viewer.lod.voxel_mode = mode;
        }

        if (arg == "--input") {
            if (i + 1 >= argc) {
                throw std::runtime_error(
                    "AppConfig: --input requires a file path"
                );
            }

            config.input_mode = "gs3d";
            config.viewer.input.gs3d_path =
                argument_at(argc, argv, i + 1);
        }

        if (arg == "--csv") {
            if (i + 1 >= argc) {
                throw std::runtime_error(
                    "AppConfig: --csv requires a file path"
                );
            }

            config.input_mode = "csv";
            config.csv_input_path =
                argument_at(argc, argv, i + 1);
        }

        if (arg == "--dat") {
            if (i + 1 >= argc) {
                throw std::runtime_error(
                    "AppConfig: --dat requires a file path"
                );
            }

            config.input_mode = "dat";
            config.csv_input_path =
                argument_at(argc, argv, i + 1);
        }

        if (arg == "--bundle") {
            if (i + 1 >= argc) {
                throw std::runtime_error("AppConfig: --bundle requires a directory path");
            }
            if (config.input_mode != "csv" && config.input_mode != "dat") {
                config.input_mode = "bundle";
            }
            config.bundle_dir = argument_at(argc, argv, i + 1);
        }
    }
}

} // namespace gs3d::app
