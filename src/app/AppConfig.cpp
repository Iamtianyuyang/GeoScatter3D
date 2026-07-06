#include "app/AppConfig.hpp"
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
        config.viewer.gs3d_path =
            ResourcePath::resolve_existing_file(
                config.viewer.gs3d_path,
                context
            );
    } else {
        config.csv_input_path =
            ResourcePath::resolve_existing_file(
                config.csv_input_path,
                context
            );
    }

    config.viewer.vertex_shader_path =
        ResourcePath::resolve_existing_file(
            config.viewer.vertex_shader_path,
            context
        );

    config.viewer.fragment_shader_path =
        ResourcePath::resolve_existing_file(
            config.viewer.fragment_shader_path,
            context
        );

    /*
     * .gs3dlod 可能还不存在，因为 auto_save_sidecar 会在运行时生成。
     * 所以不能用 resolve_existing_file。
     */
    if (!input_mode_is_bundle(config)) {
        config.viewer.lod_sidecar_path = resolve_non_existing_path(
            config.viewer.lod_sidecar_path,
            config_path
        );
    }

    if (!input_mode_is_csv(config) &&
        !input_mode_is_bundle(config) &&
        config.viewer.tile_enabled) {
            config.viewer.tile_index_path =
                ResourcePath::resolve_existing_file(
                    config.viewer.tile_index_path,
                    context
                );

            config.viewer.tile_data_path =
                ResourcePath::resolve_existing_file(
                    config.viewer.tile_data_path,
                    context
                );
    }
}

[[nodiscard]]
std::filesystem::path path_or_default(
    const toml::table& table,
    std::string_view key,
    const std::filesystem::path& default_value
) {
    if (const auto value = table[key].value<std::string>()) {
        return std::filesystem::path(*value);
    }

    return default_value;
}

[[nodiscard]]
std::string string_or_default(
    const toml::table& table,
    std::string_view key,
    const std::string& default_value
) {
    if (const auto value = table[key].value<std::string>()) {
        return *value;
    }

    return default_value;
}

[[nodiscard]]
bool bool_or_default(
    const toml::table& table,
    std::string_view key,
    bool default_value
) {
    if (const auto value = table[key].value<bool>()) {
        return *value;
    }

    return default_value;
}

[[nodiscard]]
unsigned int uint_or_default(
    const toml::table& table,
    std::string_view key,
    unsigned int default_value
) {
    if (const auto value = table[key].value<std::int64_t>()) {
        if (*value < 0) {
            throw std::runtime_error(
                "AppConfig: unsigned integer field is negative"
            );
        }

        return static_cast<unsigned int>(*value);
    }

    return default_value;
}

[[nodiscard]]
std::uint64_t uint64_or_default(
    const toml::table& table,
    std::string_view key,
    std::uint64_t default_value
) {
    if (const auto value = table[key].value<std::int64_t>()) {
        if (*value < 0) {
            throw std::runtime_error(
                "AppConfig: uint64 field is negative"
            );
        }

        return static_cast<std::uint64_t>(*value);
    }

    return default_value;
}

[[nodiscard]]
float float_or_default(
    const toml::table& table,
    std::string_view key,
    float default_value
) {
    if (const auto value = table[key].value<double>()) {
        return static_cast<float>(*value);
    }

    if (const auto value = table[key].value<std::int64_t>()) {
        return static_cast<float>(*value);
    }

    return default_value;
}

[[nodiscard]]
std::array<float, 3> float3_or_default(
    const toml::table& table,
    std::string_view key,
    const std::array<float, 3>& default_value
) {
    const auto* array = table[key].as_array();

    if (!array) {
        return default_value;
    }

    if (array->size() != 3) {
        throw std::runtime_error(
            "AppConfig: expected array with 3 values"
        );
    }

    std::array<float, 3> result{};

    for (std::size_t i = 0; i < 3; ++i) {
        const auto node = array->get(i);

        if (!node) {
            throw std::runtime_error(
                "AppConfig: invalid float3 array element"
            );
        }

        if (const auto value = node->value<double>()) {
            result[i] = static_cast<float>(*value);
        } else if (const auto value = node->value<std::int64_t>()) {
            result[i] = static_cast<float>(*value);
        } else {
            throw std::runtime_error(
                "AppConfig: float3 array contains non-numeric value"
            );
        }
    }

    return result;
}

[[nodiscard]]
std::array<float, 4> float4_or_default(
    const toml::table& table,
    std::string_view key,
    const std::array<float, 4>& default_value
) {
    const auto* array = table[key].as_array();

    if (!array) {
        return default_value;
    }

    if (array->size() != 4) {
        throw std::runtime_error(
            "AppConfig: expected array with 4 values"
        );
    }

    std::array<float, 4> result{};

    for (std::size_t i = 0; i < 4; ++i) {
        const auto node = array->get(i);

        if (!node) {
            throw std::runtime_error(
                "AppConfig: invalid float4 array element"
            );
        }

        if (const auto value = node->value<double>()) {
            result[i] = static_cast<float>(*value);
        } else if (const auto value = node->value<std::int64_t>()) {
            result[i] = static_cast<float>(*value);
        } else {
            throw std::runtime_error(
                "AppConfig: float4 array contains non-numeric value"
            );
        }
    }

    return result;
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
        config.input_mode = string_or_default(
            *input,
            "mode",
            config.input_mode
        );

        config.viewer.gs3d_path = path_or_default(
            *input,
            "gs3d_path",
            config.viewer.gs3d_path
        );

        config.csv_input_path = path_or_default(
            *input,
            "csv_path",
            config.csv_input_path
        );

        config.bundle_dir = path_or_default(
            *input,
            "bundle_dir",
            config.bundle_dir
        );
    }

    if (const auto* csv_convert = root["csv_convert"].as_table()) {
        config.csv_convert.num_threads = uint_or_default(
            *csv_convert,
            "num_threads",
            config.csv_convert.num_threads
        );

        config.csv_convert.chunk_bytes = uint64_or_default(
            *csv_convert,
            "chunk_bytes",
            config.csv_convert.chunk_bytes
        );

        config.csv_convert.min_parallel_file_bytes = uint64_or_default(
            *csv_convert,
            "min_parallel_file_bytes",
            config.csv_convert.min_parallel_file_bytes
        );

        config.csv_convert.x_field = string_or_default(
            *csv_convert,
            "x_field",
            config.csv_convert.x_field
        );

        config.csv_convert.y_field = string_or_default(
            *csv_convert,
            "y_field",
            config.csv_convert.y_field
        );

        config.csv_convert.z_field = string_or_default(
            *csv_convert,
            "z_field",
            config.csv_convert.z_field
        );

        config.csv_convert.primary_value_field = string_or_default(
            *csv_convert,
            "primary_value_field",
            config.csv_convert.primary_value_field
        );
    }

    config.tile_build.num_threads =
        config.csv_convert.num_threads;

    validate_input_mode(config.input_mode);

    if (const auto* shader = root["shader"].as_table()) {
        config.viewer.vertex_shader_path = path_or_default(
            *shader,
            "vertex_shader_path",
            config.viewer.vertex_shader_path
        );

        config.viewer.fragment_shader_path = path_or_default(
            *shader,
            "fragment_shader_path",
            config.viewer.fragment_shader_path
        );
    }

    if (const auto* window = root["window"].as_table()) {
        config.viewer.window_width = uint_or_default(
            *window,
            "width",
            config.viewer.window_width
        );

        config.viewer.window_height = uint_or_default(
            *window,
            "height",
            config.viewer.window_height
        );

        config.viewer.window_title = string_or_default(
            *window,
            "title",
            config.viewer.window_title
        );

        config.viewer.window_resizable = bool_or_default(
            *window,
            "resizable",
            config.viewer.window_resizable
        );

        /*
         * ImGui docking 布局持久化文件路径；留空字符串表示不持久化
         * （每次启动都用默认布局，不写不读 .ini）。
         */
        config.viewer.ui_layout_ini_path = path_or_default(
            *window,
            "ui_layout_ini_path",
            config.viewer.ui_layout_ini_path
        );

        config.viewer.ui_scale_multiplier = float_or_default(
            *window,
            "ui_scale_multiplier",
            config.viewer.ui_scale_multiplier
        );
    }

    if (const auto* vulkan = root["vulkan"].as_table()) {
        config.viewer.enable_validation_layers = bool_or_default(
            *vulkan,
            "validation_layers",
            config.viewer.enable_validation_layers
        );
    }

    if (const auto* render = root["render"].as_table()) {
        config.render.clear_color = float4_or_default(
            *render,
            "clear_color",
            config.render.clear_color
        );

        config.render.initial_point_size = float_or_default(
            *render,
            "initial_point_size",
            config.render.initial_point_size
        );
    }

    if (const auto* camera = root["camera"].as_table()) {
        config.camera.mode = string_or_default(
            *camera,
            "mode",
            config.camera.mode
        );

        config.camera.position = float3_or_default(
            *camera,
            "position",
            config.camera.position
        );

        config.camera.target = float3_or_default(
            *camera,
            "target",
            config.camera.target
        );

        config.camera.up = float3_or_default(
            *camera,
            "up",
            config.camera.up
        );

        config.camera.fov_y = float_or_default(
            *camera,
            "fov_y",
            config.camera.fov_y
        );

        config.camera.near_plane = float_or_default(
            *camera,
            "near",
            config.camera.near_plane
        );

        config.camera.far_plane = float_or_default(
            *camera,
            "far",
            config.camera.far_plane
        );
    }

    if (const auto* controller = root["controller"].as_table()) {
        config.controller.rotate_speed = float_or_default(
            *controller,
            "rotate_speed",
            config.controller.rotate_speed
        );

        config.controller.pan_speed = float_or_default(
            *controller,
            "pan_speed",
            config.controller.pan_speed
        );

        config.controller.zoom_speed = float_or_default(
            *controller,
            "zoom_speed",
            config.controller.zoom_speed
        );

        config.controller.invert_rotate_x = bool_or_default(
            *controller,
            "invert_rotate_x",
            config.controller.invert_rotate_x
        );

        config.controller.invert_rotate_y = bool_or_default(
            *controller,
            "invert_rotate_y",
            config.controller.invert_rotate_y
        );

        config.controller.invert_pan_x = bool_or_default(
            *controller,
            "invert_pan_x",
            config.controller.invert_pan_x
        );

        config.controller.invert_pan_y = bool_or_default(
            *controller,
            "invert_pan_y",
            config.controller.invert_pan_y
        );
    }

        if (const auto* lod = root["lod"].as_table()) {
        config.viewer.lod_enabled = bool_or_default(
            *lod,
            "enabled",
            config.viewer.lod_enabled
        );

        config.viewer.lod_keep_full_buffer = bool_or_default(
            *lod,
            "keep_full_buffer",
            config.viewer.lod_keep_full_buffer
        );
        config.viewer.lod_sidecar_path = path_or_default(
            *lod,
            "sidecar_path",
            config.viewer.lod_sidecar_path
        );

        config.viewer.lod_auto_load_sidecar = bool_or_default(
            *lod,
            "auto_load_sidecar",
            config.viewer.lod_auto_load_sidecar
        );

        config.viewer.lod_auto_save_sidecar = bool_or_default(
            *lod,
            "auto_save_sidecar",
            config.viewer.lod_auto_save_sidecar
        );
        
        /*
         * Potree 式自动分层参数（替代旧的 target_point_counts / target_point_ratios）。
         * 最精细层由 finest_target_points 锚定，然后每层 voxel_size ×= growth_factor，
         * 层数由数据自然决定。
         */
        config.viewer.lod_finest_target_points = uint64_or_default(
            *lod,
            "finest_target_points",
            config.viewer.lod_finest_target_points
        );

        config.viewer.lod_growth_factor = float_or_default(
            *lod,
            "growth_factor",
            config.viewer.lod_growth_factor
        );

        config.viewer.lod_min_points_per_level = uint64_or_default(
            *lod,
            "min_points_per_level",
            config.viewer.lod_min_points_per_level
        );

        config.viewer.lod_voxel_mode = string_or_default(
            *lod,
            "voxel_mode",
            config.viewer.lod_voxel_mode
        );

        config.viewer.lod_voxel_scale = float_or_default(
            *lod,
            "voxel_scale",
            config.viewer.lod_voxel_scale
        );

        config.viewer.lod_medium_delay_seconds =
            static_cast<double>(
                float_or_default(
                    *lod,
                    "medium_delay_seconds",
                    static_cast<float>(
                        config.viewer.lod_medium_delay_seconds
                    )
                )
            );

        config.viewer.lod_high_delay_seconds =
            static_cast<double>(
                float_or_default(
                    *lod,
                    "high_delay_seconds",
                    static_cast<float>(
                        config.viewer.lod_high_delay_seconds
                    )
                )
            );

        config.viewer.lod_use_lowest_while_interacting = bool_or_default(
            *lod,
            "use_lowest_while_interacting",
            config.viewer.lod_use_lowest_while_interacting
        );

        config.viewer.lod_adaptive_interacting_level = bool_or_default(
            *lod,
            "adaptive_interacting_level",
            config.viewer.lod_adaptive_interacting_level
        );

        config.viewer.lod_frame_time_budget_ms =
            static_cast<double>(
                float_or_default(
                    *lod,
                    "frame_time_budget_ms",
                    static_cast<float>(
                        config.viewer.lod_frame_time_budget_ms
                    )
                )
            );

        // 交互期间显示策略:keep_stable(默认) / coarse / freeze_texture。
        // 默认保持高质量,不在交互途中切到稀疏粗 LOD。
        {
            const std::string mode_str = string_or_default(
                *lod,
                "interactive_display_mode",
                "keep_stable"
            );
            if (mode_str == "coarse" ||
                mode_str == "allow_coarse" ||
                mode_str == "AllowCoarseLOD") {
                config.viewer.interactive_display_mode =
                    gs3d::app::InteractiveDisplayMode::AllowCoarseLOD;
            } else if (mode_str == "freeze_texture" ||
                       mode_str == "FreezeLastFrameTexture") {
                config.viewer.interactive_display_mode =
                    gs3d::app::InteractiveDisplayMode::FreezeLastFrameTexture;
            } else {
                config.viewer.interactive_display_mode =
                    gs3d::app::InteractiveDisplayMode::KeepStableHighQuality;
            }
        }

        config.viewer.lod_verbose = bool_or_default(
            *lod,
            "verbose",
            config.viewer.lod_verbose
        );
    }
    if (const auto* viewport = root["viewport"].as_table()) {
        config.viewer.viewport_count = static_cast<int>(
            uint_or_default(*viewport, "count",
                static_cast<unsigned int>(config.viewer.viewport_count))
        );
    }

    if (const auto* tile = root["tile"].as_table()) {
        config.viewer.tile_enabled = bool_or_default(
            *tile,
            "enabled",
            config.viewer.tile_enabled
        );

        config.viewer.tile_index_path = path_or_default(
            *tile,
            "index_path",
            config.viewer.tile_index_path
        );

        config.viewer.tile_data_path = path_or_default(
            *tile,
            "data_path",
            config.viewer.tile_data_path
        );

        config.viewer.tile_min_pixel_size = float_or_default(
            *tile,
            "min_tile_pixel_size",
            config.viewer.tile_min_pixel_size
        );

        config.viewer.tile_max_visible_tiles = uint_or_default(
            *tile,
            "max_visible_tiles",
            config.viewer.tile_max_visible_tiles
        );

        config.viewer.tile_use_full_z_range = bool_or_default(
            *tile,
            "use_full_z_range",
            config.viewer.tile_use_full_z_range
        );

        config.viewer.tile_verbose = bool_or_default(
            *tile,
            "verbose",
            config.viewer.tile_verbose
        );

        config.viewer.tile_gpu_cache_max_tiles = uint_or_default(
            *tile,
            "gpu_cache_max_tiles",
            config.viewer.tile_gpu_cache_max_tiles
        );

        config.viewer.tile_gpu_upload_budget_bytes = uint64_or_default(
            *tile,
            "gpu_upload_budget_bytes",
            config.viewer.tile_gpu_upload_budget_bytes
        );

        config.viewer.tile_cpu_cache_max_bytes = uint64_or_default(
            *tile,
            "cpu_cache_max_bytes",
            config.viewer.tile_cpu_cache_max_bytes
        );

        config.viewer.tile_preload_all = bool_or_default(
            *tile,
            "preload_all",
            config.viewer.tile_preload_all
        );

        config.viewer.tile_preload_max_bytes = uint64_or_default(
            *tile,
            "preload_max_bytes",
            config.viewer.tile_preload_max_bytes
        );

        config.viewer.tile_preload_upload_budget_bytes = uint64_or_default(
            *tile,
            "preload_upload_budget_bytes",
            config.viewer.tile_preload_upload_budget_bytes
        );

    }

    if (const auto* debug = root["debug"].as_table()) {
        config.viewer.pick_debug_dump_enabled = bool_or_default(
            *debug,
            "pick_debug_dump_enabled",
            config.viewer.pick_debug_dump_enabled
        );
        config.viewer.pick_debug_dump_dir = path_or_default(
            *debug,
            "pick_debug_dump_dir",
            config.viewer.pick_debug_dump_dir
        );
        config.viewer.pick_debug_dump_once_on_hover = bool_or_default(
            *debug,
            "pick_debug_dump_once_on_hover",
            config.viewer.pick_debug_dump_once_on_hover
        );
    }
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
        config.viewer.enable_validation_layers = false;
    }

    if (has_flag(argc, argv, "--validation")) {
        config.viewer.enable_validation_layers = true;
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
            config.viewer.gs3d_path =
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
                throw std::runtime_error(
                    "AppConfig: --bundle requires a directory path"
                );
            }

            config.input_mode = "bundle";
            config.bundle_dir =
                argument_at(argc, argv, i + 1);
        }
    }
}

void AppConfigPrinter::print(const AppConfig& config) {
    std::cout << "[CONFIG] input.gs3d_path = "
              << config.viewer.gs3d_path.string() << '\n';

    std::cout << "[CONFIG] input.mode = "
              << config.input_mode << '\n';

    std::cout << "[CONFIG] input.csv_path = "
              << config.csv_input_path.string() << '\n';

    std::cout << "[CONFIG] input.bundle_dir = "
              << config.bundle_dir.string() << '\n';

    std::cout << "[CONFIG] csv_convert.num_threads = "
              << config.csv_convert.num_threads << '\n';

    std::cout << "[CONFIG] preprocess.num_threads = "
              << config.tile_build.num_threads << '\n';

    std::cout << "[CONFIG] csv_convert.chunk_bytes = "
              << config.csv_convert.chunk_bytes << '\n';

    std::cout << "[CONFIG] csv_convert.min_parallel_file_bytes = "
              << config.csv_convert.min_parallel_file_bytes << '\n';

    std::cout << "[CONFIG] shader.vertex_shader_path = "
              << config.viewer.vertex_shader_path.string() << '\n';

    std::cout << "[CONFIG] shader.fragment_shader_path = "
              << config.viewer.fragment_shader_path.string() << '\n';

    std::cout << "[CONFIG] window.width = "
              << config.viewer.window_width << '\n';

    std::cout << "[CONFIG] window.height = "
              << config.viewer.window_height << '\n';

    std::cout << "[CONFIG] window.title = "
              << config.viewer.window_title << '\n';

    std::cout << "[CONFIG] window.resizable = "
              << (config.viewer.window_resizable ? "true" : "false") << '\n';

    std::cout << "[CONFIG] window.ui_layout_ini_path = "
              << config.viewer.ui_layout_ini_path.string() << '\n';

    std::cout << "[CONFIG] window.ui_scale_multiplier = "
              << config.viewer.ui_scale_multiplier << '\n';

    std::cout << "[CONFIG] debug.pick_debug_dump_enabled = "
              << (config.viewer.pick_debug_dump_enabled ? "true" : "false")
              << '\n';

    std::cout << "[CONFIG] debug.pick_debug_dump_dir = "
              << config.viewer.pick_debug_dump_dir.string() << '\n';

    std::cout << "[CONFIG] debug.pick_debug_dump_once_on_hover = "
              << (config.viewer.pick_debug_dump_once_on_hover
                      ? "true"
                      : "false")
              << '\n';

    std::cout << "[CONFIG] vulkan.validation_layers = "
              << (config.viewer.enable_validation_layers ? "true" : "false")
              << '\n';

    std::cout << "[CONFIG] render.clear_color = ["
              << config.render.clear_color[0] << ", "
              << config.render.clear_color[1] << ", "
              << config.render.clear_color[2] << ", "
              << config.render.clear_color[3] << "]\n";

    std::cout << "[CONFIG] render.initial_point_size = "
              << config.render.initial_point_size << '\n';

    std::cout << "[CONFIG] camera.mode = "
              << config.camera.mode << '\n';

    std::cout << "[CONFIG] camera.position = ["
              << config.camera.position[0] << ", "
              << config.camera.position[1] << ", "
              << config.camera.position[2] << "]\n";

    std::cout << "[CONFIG] camera.target = ["
              << config.camera.target[0] << ", "
              << config.camera.target[1] << ", "
              << config.camera.target[2] << "]\n";

    std::cout << "[CONFIG] camera.up = ["
              << config.camera.up[0] << ", "
              << config.camera.up[1] << ", "
              << config.camera.up[2] << "]\n";

    std::cout << "[CONFIG] camera.fov_y = "
              << config.camera.fov_y << '\n';

    std::cout << "[CONFIG] camera.near = "
              << config.camera.near_plane << '\n';

    std::cout << "[CONFIG] camera.far = "
              << config.camera.far_plane << '\n';

    std::cout << "[CONFIG] controller.rotate_speed = "
              << config.controller.rotate_speed << '\n';

    std::cout << "[CONFIG] controller.pan_speed = "
              << config.controller.pan_speed << '\n';

    std::cout << "[CONFIG] controller.zoom_speed = "
              << config.controller.zoom_speed << '\n';

    std::cout << "[CONFIG] controller.invert_rotate_x = "
              << (config.controller.invert_rotate_x ? "true" : "false")
              << '\n';

    std::cout << "[CONFIG] controller.invert_rotate_y = "
              << (config.controller.invert_rotate_y ? "true" : "false")
              << '\n';

    std::cout << "[CONFIG] controller.invert_pan_x = "
              << (config.controller.invert_pan_x ? "true" : "false")
              << '\n';

    std::cout << "[CONFIG] controller.invert_pan_y = "
              << (config.controller.invert_pan_y ? "true" : "false")
              << '\n';
    std::cout << "[CONFIG] lod.enabled = "
              << (config.viewer.lod_enabled ? "true" : "false")
              << '\n';
    std::cout << "[CONFIG] lod.keep_full_buffer = "
          << (config.viewer.lod_keep_full_buffer ? "true" : "false")
          << '\n';
    std::cout << "[CONFIG] lod.sidecar_path = "
          << config.viewer.lod_sidecar_path.string()
          << '\n';
    std::cout << "[CONFIG] lod.auto_load_sidecar = "
            << (config.viewer.lod_auto_load_sidecar ? "true" : "false")
            << '\n';

    std::cout << "[CONFIG] lod.auto_save_sidecar = "
            << (config.viewer.lod_auto_save_sidecar ? "true" : "false")
            << '\n';
    std::cout << "[CONFIG] lod.finest_target_points = "
              << config.viewer.lod_finest_target_points
              << '\n';
    std::cout << "[CONFIG] lod.growth_factor = "
              << config.viewer.lod_growth_factor
              << '\n';
    std::cout << "[CONFIG] lod.min_points_per_level = "
              << config.viewer.lod_min_points_per_level
              << '\n';

    std::cout << "[CONFIG] lod.voxel_mode = "
              << config.viewer.lod_voxel_mode << '\n';

    std::cout << "[CONFIG] lod.voxel_scale = "
              << config.viewer.lod_voxel_scale << '\n';

    std::cout << "[CONFIG] lod.medium_delay_seconds = "
              << config.viewer.lod_medium_delay_seconds << '\n';

    std::cout << "[CONFIG] lod.high_delay_seconds = "
              << config.viewer.lod_high_delay_seconds << '\n';

    std::cout << "[CONFIG] lod.use_lowest_while_interacting = "
              << (config.viewer.lod_use_lowest_while_interacting
                    ? "true"
                    : "false")
              << '\n';

    std::cout << "[CONFIG] lod.adaptive_interacting_level = "
              << (config.viewer.lod_adaptive_interacting_level
                    ? "true"
                    : "false")
              << '\n';

    std::cout << "[CONFIG] lod.frame_time_budget_ms = "
              << config.viewer.lod_frame_time_budget_ms << '\n';

    std::cout << "[CONFIG] lod.interactive_display_mode = "
              << (config.viewer.interactive_display_mode ==
                        gs3d::app::InteractiveDisplayMode::AllowCoarseLOD
                    ? "coarse"
                    : config.viewer.interactive_display_mode ==
                            gs3d::app::InteractiveDisplayMode::
                                FreezeLastFrameTexture
                        ? "freeze_texture"
                        : "keep_stable")
              << '\n';

    std::cout << "[CONFIG] lod.verbose = "
              << (config.viewer.lod_verbose ? "true" : "false")
              << '\n';
        std::cout << "[CONFIG] tile.enabled = "
              << (config.viewer.tile_enabled ? "true" : "false")
              << '\n';

    std::cout << "[CONFIG] tile.index_path = "
              << config.viewer.tile_index_path.string()
              << '\n';

    std::cout << "[CONFIG] tile.data_path = "
              << config.viewer.tile_data_path.string()
              << '\n';

    std::cout << "[CONFIG] tile.min_tile_pixel_size = "
              << config.viewer.tile_min_pixel_size
              << '\n';

    std::cout << "[CONFIG] tile.max_visible_tiles = "
              << config.viewer.tile_max_visible_tiles
              << '\n';

    std::cout << "[CONFIG] tile.use_full_z_range = "
              << (config.viewer.tile_use_full_z_range ? "true" : "false")
              << '\n';

    std::cout << "[CONFIG] tile.verbose = "
              << (config.viewer.tile_verbose ? "true" : "false")
              << '\n';

    std::cout << "[CONFIG] tile.gpu_cache_max_tiles = "
              << config.viewer.tile_gpu_cache_max_tiles
              << '\n';

    std::cout << "[CONFIG] tile.gpu_upload_budget_bytes = "
              << config.viewer.tile_gpu_upload_budget_bytes
              << '\n';

    std::cout << "[CONFIG] tile.cpu_cache_max_bytes = "
              << config.viewer.tile_cpu_cache_max_bytes
              << '\n';

    std::cout << "[CONFIG] tile.preload_all = "
              << (config.viewer.tile_preload_all ? "true" : "false")
              << '\n';

    std::cout << "[CONFIG] tile.preload_max_bytes = "
              << config.viewer.tile_preload_max_bytes
              << '\n';

    std::cout << "[CONFIG] tile.preload_upload_budget_bytes = "
              << config.viewer.tile_preload_upload_budget_bytes
              << '\n';

    std::cout << "[CONFIG] viewport.count = "
              << config.viewer.viewport_count
              << '\n';
}

} // namespace gs3d::app
