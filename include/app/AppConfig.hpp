#pragma once

#include "app/ViewerApp.hpp"
#include "data/CsvChunkPlanner.hpp"

#include <array>
#include <filesystem>
#include <string>

namespace gs3d::app {

struct RenderConfig {
    std::array<float, 4> clear_color{
        0.015f,
        0.018f,
        0.025f,
        1.0f
    };

    float initial_point_size = 1.0f;
};

struct CameraConfig {
    std::string mode = "fit";

    std::array<float, 3> position{
        0.0f,
        -14000.0f,
        6000.0f
    };

    std::array<float, 3> target{
        0.0f,
        0.0f,
        0.0f
    };

    std::array<float, 3> up{
        0.0f,
        0.0f,
        1.0f
    };

    float fov_y = 45.0f;
    float near_plane = 1.0f;
    float far_plane = 100000.0f;
};

struct ControllerConfig {
    float rotate_speed = 1.0f;
    float pan_speed = 1.0f;
    float zoom_speed = 1.0f;

    bool invert_rotate_x = false;
    bool invert_rotate_y = false;
    bool invert_pan_x = false;
    bool invert_pan_y = true;
};

struct CsvConvertConfig {
    std::uint32_t num_threads = 0;
    std::uint64_t chunk_bytes = 16ull * 1024ull * 1024ull;
    std::uint64_t min_parallel_file_bytes = 64ull * 1024ull * 1024ull;

    // Column name mapping (case-insensitive).
    std::string x_field = "x";
    std::string y_field = "y";
    std::string z_field = "elevation";
    std::string primary_value_field = "fold";
};

struct TileBuildConfig {
    std::uint32_t num_threads = 0;
};

struct AppConfig {
    ViewerAppConfig viewer;
    RenderConfig render;
    CameraConfig camera;
    ControllerConfig controller;
    CsvConvertConfig csv_convert;
    TileBuildConfig tile_build;

    std::string input_mode = "gs3d";
    std::filesystem::path csv_input_path;
    std::filesystem::path bundle_dir;
};

class AppConfigLoader {
public:
    [[nodiscard]]
    static AppConfig load_from_file(
        const std::filesystem::path& path
    );

    [[nodiscard]]
    static AppConfig load_from_args(
        int argc,
        char** argv
    );

    [[nodiscard]]
    static std::filesystem::path default_config_path();

private:
    [[nodiscard]]
    static std::filesystem::path parse_config_path_from_args(
        int argc,
        char** argv
    );

    static void apply_command_line_overrides(
        AppConfig& config,
        int argc,
        char** argv
    );
};

class AppConfigPrinter {
public:
    static void print(const AppConfig& config);
};

} // namespace gs3d::app
