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

void resolve_viewer_resource_paths(
    AppConfig& config,
    const std::filesystem::path& config_path,
    const std::filesystem::path& executable_path
) {
    ResourcePathContext context;
    context.config_path = config_path;
    context.executable_path = executable_path;

    config.viewer.gs3d_path =
        ResourcePath::resolve_existing_file(
            config.viewer.gs3d_path,
            context
        );

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
        config.viewer.gs3d_path = path_or_default(
            *input,
            "gs3d_path",
            config.viewer.gs3d_path
        );
    }

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

    resolve_viewer_resource_paths(
        config,
        path,
        {}
    );

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

            config.viewer.gs3d_path =
                argument_at(argc, argv, i + 1);
        }
    }
}

void AppConfigPrinter::print(const AppConfig& config) {
    std::cout << "[CONFIG] input.gs3d_path = "
              << config.viewer.gs3d_path.string() << '\n';

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
}

} // namespace gs3d::app