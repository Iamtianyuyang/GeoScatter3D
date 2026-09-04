#include "app/AppConfig.hpp"
#include "app/AppConfigTomlValue.hpp"
#include "app/AppConfigValidation.hpp"
#include "app/AppConfigViewerToml.hpp"
#include "util/Log.hpp"
#include "app/ResourcePath.hpp"

#include <toml++/toml.hpp>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace gs3d::app {

namespace {

[[nodiscard]]
std::string argument_at(int argc, char** argv, int index) {
    if (index < 0 || index >= argc || argv[index] == nullptr) {
        return {};
    }

    return std::string(argv[index]);
}

[[nodiscard]]
std::filesystem::path executable_path_from_args(
    int argc,
    char** argv
) {
    if (argc <= 0 || argv == nullptr || argv[0] == nullptr) {
        return {};
    }

    return std::filesystem::path(argv[0]);
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

[[nodiscard]]
std::filesystem::path resolve_config_file_path(
    const std::filesystem::path& path,
    const std::filesystem::path& executable_path
) {
    ResourcePathContext context;
    context.config_path = std::filesystem::path{};
    context.executable_path = executable_path;

    return ResourcePath::resolve_existing_file(
        path,
        context
    );
}

[[nodiscard]]
std::vector<std::uint64_t> uint64_array_or_default(
    const toml::table& table,
    std::string_view key,
    const std::vector<std::uint64_t>& default_value
) {
    const auto* array = table[key].as_array();

    if (!array) {
        return default_value;
    }

    std::vector<std::uint64_t> result;
    result.reserve(array->size());

    for (std::size_t i = 0; i < array->size(); ++i) {
        const auto node = array->get(i);

        if (!node) {
            throw std::runtime_error(
                "AppConfig: invalid uint64 array element"
            );
        }

        if (const auto value = node->value<std::int64_t>()) {
            if (*value < 0) {
                throw std::runtime_error(
                    "AppConfig: uint64 array contains negative value"
                );
            }

            result.push_back(static_cast<std::uint64_t>(*value));
        } else {
            throw std::runtime_error(
                "AppConfig: uint64 array contains non-integer value"
            );
        }
    }

    return result;
}

[[nodiscard]]
std::vector<double> double_array_or_default(
    const toml::table& table,
    std::string_view key,
    const std::vector<double>& default_value
) {
    const auto* array = table[key].as_array();

    if (!array) {
        return default_value;
    }

    std::vector<double> result;
    result.reserve(array->size());

    for (std::size_t i = 0; i < array->size(); ++i) {
        const auto node = array->get(i);

        if (!node) {
            throw std::runtime_error(
                "AppConfig: invalid double array element"
            );
        }

        if (const auto value = node->value<double>()) {
            result.push_back(*value);
        } else {
            throw std::runtime_error(
                "AppConfig: double array contains non-numeric value"
            );
        }
    }

    return result;
}

[[nodiscard]]
bool input_mode_is_csv(const AppConfig& config) noexcept {
    return config.input_mode == "csv" || config.input_mode == "dat";
}

[[nodiscard]]
bool input_mode_is_bundle(const AppConfig& config) noexcept {
    return config.input_mode == "bundle";
}

void validate_input_mode(const std::string& mode) {
    if (mode == "csv" ||
        mode == "dat" ||
        mode == "gs3d" ||
        mode == "bundle") {
        return;
    }

    throw std::runtime_error(
        "AppConfig: input.mode must be \"csv\", \"dat\", \"gs3d\", or "
        "\"bundle\""
    );
}

[[nodiscard]]
std::filesystem::path resolve_non_existing_path(
    const std::filesystem::path& path,
    const std::filesystem::path& config_path
) {
    if (path.empty() || path.is_absolute()) {
        return path;
    }

    const auto config_dir =
        ResourcePath::config_directory(config_path);

    if (!config_dir.empty() && config_dir.has_parent_path()) {
        return (config_dir.parent_path() / path).lexically_normal();
    }

    return (ResourcePath::current_working_directory() / path)
        .lexically_normal();
}

void resolve_viewer_resource_paths(
    AppConfig& config,
    const std::filesystem::path& config_path,
    const std::filesystem::path& executable_path
) {
    ResourcePathContext context;
    context.config_path = config_path;
    context.executable_path = executable_path;

    if (!config.bundle_dir.empty()) {
        config.bundle_dir = resolve_non_existing_path(
            config.bundle_dir,
            config_path
        );
    }

    if (input_mode_is_bundle(config)) {
        if (config.bundle_dir.empty()) {
            throw std::runtime_error(
                "AppConfig: input.mode is \"bundle\" but input.bundle_dir "
                "is empty"
            );
        }

    } else if (!input_mode_is_csv(config)) {
        config.viewer.input.gs3d_path =
            ResourcePath::resolve_existing_file(
                config.viewer.input.gs3d_path,
                context
            );
    } else {
        config.csv_input_path =
            ResourcePath::resolve_existing_file(
                config.csv_input_path,
                context
            );
    }

    config.viewer.graphics.vertex_shader_path =
        ResourcePath::resolve_existing_file(
            config.viewer.graphics.vertex_shader_path,
            context
        );

    config.viewer.graphics.fragment_shader_path =
        ResourcePath::resolve_existing_file(
            config.viewer.graphics.fragment_shader_path,
            context
        );

    /*
     * .gs3dlod 由预处理工具（preprocess / engine）生成，可能在配置时
     * 尚不存在，所以不能用 resolve_existing_file。
     */
    if (!input_mode_is_bundle(config)) {
        config.viewer.lod.sidecar_path = resolve_non_existing_path(
            config.viewer.lod.sidecar_path,
            config_path
        );
    }

    config.viewer.window.ui_layout_ini_path =
        ResourcePath::resolve_writable_file(
            config.viewer.window.ui_layout_ini_path, context);

    if (!input_mode_is_csv(config) &&
        !input_mode_is_bundle(config) &&
        config.viewer.tile.enabled) {
            config.viewer.tile.index_path =
                ResourcePath::resolve_existing_file(
                    config.viewer.tile.index_path,
                    context
                );

            config.viewer.tile.data_path =
                ResourcePath::resolve_existing_file(
                    config.viewer.tile.data_path,
                    context
                );
    }
}

} // namespace

AppConfig AppConfigLoader::load_from_file(
    const std::filesystem::path& path
) {
    toml::table root;

    try {
        root = toml::parse_file(path.string());
    } catch (const toml::parse_error& e) {
        throw std::runtime_error(
            "AppConfig: failed to parse TOML file: " +
            path.string() +
            "\nReason: " +
            std::string(e.description())
        );
    }

    AppConfig config;

    if (const auto* input = root["input"].as_table()) {
        config.input_mode = detail::string_or_default(
            *input,
            "mode",
            config.input_mode
        );

        config.viewer.input.gs3d_path = detail::path_or_default(
            *input,
            "gs3d_path",
            config.viewer.input.gs3d_path
        );

        config.csv_input_path = detail::path_or_default(
            *input,
            "csv_path",
            config.csv_input_path
        );

        config.bundle_dir = detail::path_or_default(
            *input,
            "bundle_dir",
            config.bundle_dir
        );
    }

    if (const auto* csv_convert = root["csv_convert"].as_table()) {
        config.csv_convert.num_threads = detail::uint_or_default(
            *csv_convert,
            "num_threads",
            config.csv_convert.num_threads
        );

        config.csv_convert.chunk_bytes = detail::uint64_or_default(
            *csv_convert,
            "chunk_bytes",
            config.csv_convert.chunk_bytes
        );

        config.csv_convert.min_parallel_file_bytes = detail::uint64_or_default(
            *csv_convert,
            "min_parallel_file_bytes",
            config.csv_convert.min_parallel_file_bytes
        );

        config.csv_convert.x_field = detail::string_or_default(
            *csv_convert,
            "x_field",
            config.csv_convert.x_field
        );

        config.csv_convert.y_field = detail::string_or_default(
            *csv_convert,
            "y_field",
            config.csv_convert.y_field
        );

        config.csv_convert.z_field = detail::string_or_default(
            *csv_convert,
            "z_field",
            config.csv_convert.z_field
        );

        config.csv_convert.primary_value_field = detail::string_or_default(
            *csv_convert,
            "primary_value_field",
            config.csv_convert.primary_value_field
        );
    }

    config.tile_build.num_threads =
        config.csv_convert.num_threads;

    validate_input_mode(config.input_mode);

    detail::apply_viewer_toml_sections(root, config);

    validate_app_config(config);
    return config;
}

AppConfig AppConfigLoader::load_from_args(
    int argc,
    char** argv
) {
    const auto executable_path =
        executable_path_from_args(argc, argv);

    const auto requested_config_path =
        parse_config_path_from_args(argc, argv);

    const auto resolved_config_path =
        resolve_config_file_path(
            requested_config_path,
            executable_path
        );

    AppConfig config =
        load_from_file(resolved_config_path);

    apply_command_line_overrides(
        config,
        argc,
        argv
    );

    resolve_viewer_resource_paths(
        config,
        resolved_config_path,
        executable_path
    );

    return config;
}

std::filesystem::path AppConfigLoader::default_config_path() {
    return "config/viewer.toml";
}

std::filesystem::path AppConfigLoader::parse_config_path_from_args(
    int argc,
    char** argv
) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argument_at(argc, argv, i);

        if (arg == "--config" || arg == "-c") {
            if (i + 1 >= argc) {
                throw std::runtime_error(
                    "AppConfig: --config requires a file path"
                );
            }

            return argument_at(argc, argv, i + 1);
        }
    }

    if (argc >= 2) {
        const std::string first = argument_at(argc, argv, 1);

        if (!first.empty() && first[0] != '-') {
            return first;
        }
    }

    return default_config_path();
}

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

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argument_at(argc, argv, i);

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

void AppConfigPrinter::print(const AppConfig& config) {
    gs3d::util::log::info() << "[CONFIG] input.gs3d_path = "
              << config.viewer.input.gs3d_path.string() << '\n';

    gs3d::util::log::info() << "[CONFIG] input.mode = "
              << config.input_mode << '\n';

    gs3d::util::log::info() << "[CONFIG] input.csv_path = "
              << config.csv_input_path.string() << '\n';

    gs3d::util::log::info() << "[CONFIG] input.bundle_dir = "
              << config.bundle_dir.string() << '\n';

    gs3d::util::log::info() << "[CONFIG] csv_convert.num_threads = "
              << config.csv_convert.num_threads << '\n';

    gs3d::util::log::info() << "[CONFIG] preprocess.num_threads = "
              << config.tile_build.num_threads << '\n';

    gs3d::util::log::info() << "[CONFIG] csv_convert.chunk_bytes = "
              << config.csv_convert.chunk_bytes << '\n';

    gs3d::util::log::info() << "[CONFIG] csv_convert.min_parallel_file_bytes = "
              << config.csv_convert.min_parallel_file_bytes << '\n';

    gs3d::util::log::info() << "[CONFIG] shader.vertex_shader_path = "
              << config.viewer.graphics.vertex_shader_path.string() << '\n';

    gs3d::util::log::info() << "[CONFIG] shader.fragment_shader_path = "
              << config.viewer.graphics.fragment_shader_path.string() << '\n';

    gs3d::util::log::info() << "[CONFIG] window.width = "
              << config.viewer.window.width << '\n';

    gs3d::util::log::info() << "[CONFIG] window.height = "
              << config.viewer.window.height << '\n';

    gs3d::util::log::info() << "[CONFIG] window.title = "
              << config.viewer.window.title << '\n';

    gs3d::util::log::info() << "[CONFIG] window.resizable = "
              << (config.viewer.window.resizable ? "true" : "false") << '\n';

    gs3d::util::log::info() << "[CONFIG] window.ui_layout_ini_path = "
              << config.viewer.window.ui_layout_ini_path.string() << '\n';

    gs3d::util::log::info() << "[CONFIG] window.ui_scale_multiplier = "
              << config.viewer.window.ui_scale_multiplier << '\n';

    gs3d::util::log::info() << "[CONFIG] window.theme = "
              << config.viewer.window.theme << '\n';

    gs3d::util::log::info() << "[CONFIG] window.multi_viewports = "
              << (config.viewer.window.enable_multi_viewports ? "true" : "false")
              << '\n';

    gs3d::util::log::info() << "[CONFIG] debug.pick_debug_dump_enabled = "
              << (config.viewer.pick_debug.dump_enabled ? "true" : "false")
              << '\n';

    gs3d::util::log::info() << "[CONFIG] debug.pick_debug_dump_dir = "
              << config.viewer.pick_debug.dump_dir.string() << '\n';

    gs3d::util::log::info() << "[CONFIG] debug.pick_debug_dump_once_on_hover = "
              << (config.viewer.pick_debug.dump_once_on_hover
                      ? "true"
                      : "false")
              << '\n';

    gs3d::util::log::info() << "[CONFIG] vulkan.validation_layers = "
              << (config.viewer.graphics.enable_validation_layers ? "true" : "false")
              << '\n';

    gs3d::util::log::info() << "[CONFIG] graphics.preferred_gpu = "
              << config.viewer.graphics.preferred_gpu
              << '\n';

    gs3d::util::log::info() << "[CONFIG] render.clear_color = ["
              << config.render.clear_color[0] << ", "
              << config.render.clear_color[1] << ", "
              << config.render.clear_color[2] << ", "
              << config.render.clear_color[3] << "]\n";

    gs3d::util::log::info() << "[CONFIG] render.initial_point_size = "
              << config.render.initial_point_size << '\n';

    gs3d::util::log::info() << "[CONFIG] camera.mode = "
              << config.camera.mode << '\n';

    gs3d::util::log::info() << "[CONFIG] camera.position = ["
              << config.camera.position[0] << ", "
              << config.camera.position[1] << ", "
              << config.camera.position[2] << "]\n";

    gs3d::util::log::info() << "[CONFIG] camera.target = ["
              << config.camera.target[0] << ", "
              << config.camera.target[1] << ", "
              << config.camera.target[2] << "]\n";

    gs3d::util::log::info() << "[CONFIG] camera.up = ["
              << config.camera.up[0] << ", "
              << config.camera.up[1] << ", "
              << config.camera.up[2] << "]\n";

    gs3d::util::log::info() << "[CONFIG] camera.fov_y = "
              << config.camera.fov_y << '\n';

    gs3d::util::log::info() << "[CONFIG] camera.near = "
              << config.camera.near_plane << '\n';

    gs3d::util::log::info() << "[CONFIG] camera.far = "
              << config.camera.far_plane << '\n';

    gs3d::util::log::info() << "[CONFIG] controller.rotate_speed = "
              << config.controller.rotate_speed << '\n';

    gs3d::util::log::info() << "[CONFIG] controller.pan_speed = "
              << config.controller.pan_speed << '\n';

    gs3d::util::log::info() << "[CONFIG] controller.zoom_speed = "
              << config.controller.zoom_speed << '\n';

    gs3d::util::log::info() << "[CONFIG] controller.invert_rotate_x = "
              << (config.controller.invert_rotate_x ? "true" : "false")
              << '\n';

    gs3d::util::log::info() << "[CONFIG] controller.invert_rotate_y = "
              << (config.controller.invert_rotate_y ? "true" : "false")
              << '\n';

    gs3d::util::log::info() << "[CONFIG] controller.invert_pan_x = "
              << (config.controller.invert_pan_x ? "true" : "false")
              << '\n';

    gs3d::util::log::info() << "[CONFIG] controller.invert_pan_y = "
              << (config.controller.invert_pan_y ? "true" : "false")
              << '\n';
    gs3d::util::log::info() << "[CONFIG] lod.enabled = "
              << (config.viewer.lod.enabled ? "true" : "false")
              << '\n';
    gs3d::util::log::info() << "[CONFIG] lod.keep_full_buffer = "
          << (config.viewer.lod.keep_full_buffer ? "true" : "false")
          << '\n';
    gs3d::util::log::info() << "[CONFIG] lod.sidecar_path = "
          << config.viewer.lod.sidecar_path.string()
          << '\n';
    gs3d::util::log::info() << "[CONFIG] lod.auto_load_sidecar = "
            << (config.viewer.lod.auto_load_sidecar ? "true" : "false")
            << '\n';
    gs3d::util::log::info() << "[CONFIG] lod.finest_target_points = "
              << config.viewer.lod.finest_target_points
              << '\n';
    gs3d::util::log::info() << "[CONFIG] lod.growth_factor = "
              << config.viewer.lod.growth_factor
              << '\n';
    gs3d::util::log::info() << "[CONFIG] lod.min_points_per_level = "
              << config.viewer.lod.min_points_per_level
              << '\n';

    gs3d::util::log::info() << "[CONFIG] lod.voxel_mode = "
              << config.viewer.lod.voxel_mode << '\n';

    gs3d::util::log::info() << "[CONFIG] lod.voxel_scale = "
              << config.viewer.lod.voxel_scale << '\n';

    gs3d::util::log::info() << "[CONFIG] lod.medium_delay_seconds = "
              << config.viewer.lod.medium_delay_seconds << '\n';

    gs3d::util::log::info() << "[CONFIG] lod.high_delay_seconds = "
              << config.viewer.lod.high_delay_seconds << '\n';

    gs3d::util::log::info() << "[CONFIG] lod.use_lowest_while_interacting = "
              << (config.viewer.lod.use_lowest_while_interacting
                    ? "true"
                    : "false")
              << '\n';

    gs3d::util::log::info() << "[CONFIG] lod.adaptive_interacting_level = "
              << (config.viewer.lod.adaptive_interacting_level
                    ? "true"
                    : "false")
              << '\n';

    gs3d::util::log::info() << "[CONFIG] lod.frame_time_budget_ms = "
              << config.viewer.lod.frame_time_budget_ms << '\n';

    gs3d::util::log::info() << "[CONFIG] lod.interactive_display_mode = "
              << (config.viewer.lod.interactive_display_mode ==
                        gs3d::app::InteractiveDisplayMode::AllowCoarseLOD
                    ? "coarse"
                    : config.viewer.lod.interactive_display_mode ==
                            gs3d::app::InteractiveDisplayMode::
                                FreezeLastFrameTexture
                        ? "freeze_texture"
                        : "keep_stable")
              << '\n';

    gs3d::util::log::info() << "[CONFIG] lod.verbose = "
              << (config.viewer.lod.verbose ? "true" : "false")
              << '\n';
        gs3d::util::log::info() << "[CONFIG] tile.enabled = "
              << (config.viewer.tile.enabled ? "true" : "false")
              << '\n';

    gs3d::util::log::info() << "[CONFIG] tile.index_path = "
              << config.viewer.tile.index_path.string()
              << '\n';

    gs3d::util::log::info() << "[CONFIG] tile.data_path = "
              << config.viewer.tile.data_path.string()
              << '\n';

    gs3d::util::log::info() << "[CONFIG] tile.min_tile_pixel_size = "
              << config.viewer.tile.min_pixel_size
              << '\n';

    gs3d::util::log::info() << "[CONFIG] tile.max_visible_tiles (deprecated, ignored) = "
              << config.viewer.tile.max_visible_tiles
              << '\n';

    gs3d::util::log::info() << "[CONFIG] tile.use_full_z_range = "
              << (config.viewer.tile.use_full_z_range ? "true" : "false")
              << '\n';

    gs3d::util::log::info() << "[CONFIG] tile.verbose = "
              << (config.viewer.tile.verbose ? "true" : "false")
              << '\n';

    gs3d::util::log::info() << "[CONFIG] tile.gpu_cache_max_tiles = "
              << config.viewer.tile.gpu_cache_max_tiles
              << '\n';

    gs3d::util::log::info() << "[CONFIG] tile.gpu_upload_budget_bytes = "
              << config.viewer.tile.gpu_upload_budget_bytes
              << '\n';

    gs3d::util::log::info() << "[CONFIG] tile.cpu_cache_max_bytes = "
              << config.viewer.tile.cpu_cache_max_bytes
              << '\n';

    gs3d::util::log::info() << "[CONFIG] tile.preload_all = "
              << (config.viewer.tile.preload_all ? "true" : "false")
              << '\n';

    gs3d::util::log::info() << "[CONFIG] tile.preload_max_bytes = "
              << config.viewer.tile.preload_max_bytes
              << '\n';

    gs3d::util::log::info() << "[CONFIG] tile.preload_upload_budget_bytes = "
              << config.viewer.tile.preload_upload_budget_bytes
              << '\n';

    gs3d::util::log::info() << "[CONFIG] viewport.count = "
              << config.viewer.window.viewport_count
              << '\n';
}

} // namespace gs3d::app
