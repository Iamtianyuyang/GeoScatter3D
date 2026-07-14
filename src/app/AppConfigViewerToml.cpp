#include "app/AppConfigViewerToml.hpp"

#include "app/AppConfigTomlValue.hpp"

namespace gs3d::app::detail {

namespace {

void apply_shader(const toml::table& root, AppConfig& config)
{
    if (const auto* shader = root["shader"].as_table()) {
        config.viewer.graphics.vertex_shader_path = path_or_default(
            *shader, "vertex_shader_path",
            config.viewer.graphics.vertex_shader_path);
        config.viewer.graphics.fragment_shader_path = path_or_default(
            *shader, "fragment_shader_path",
            config.viewer.graphics.fragment_shader_path);
    }
}

void apply_window(const toml::table& root, AppConfig& config)
{
    if (const auto* window = root["window"].as_table()) {
        config.viewer.window.width = uint_or_default(
            *window, "width", config.viewer.window.width);
        config.viewer.window.height = uint_or_default(
            *window, "height", config.viewer.window.height);
        config.viewer.window.title = string_or_default(
            *window, "title", config.viewer.window.title);
        config.viewer.window.resizable = bool_or_default(
            *window, "resizable", config.viewer.window.resizable);
        config.viewer.window.ui_layout_ini_path = path_or_default(
            *window, "ui_layout_ini_path",
            config.viewer.window.ui_layout_ini_path);
        config.viewer.window.ui_scale_multiplier = float_or_default(
            *window, "ui_scale_multiplier",
            config.viewer.window.ui_scale_multiplier);
        config.viewer.window.theme = string_or_default(
            *window, "theme", config.viewer.window.theme);
        config.viewer.window.enable_multi_viewports = bool_or_default(
            *window, "multi_viewports",
            config.viewer.window.enable_multi_viewports);
    }
}

void apply_graphics(const toml::table& root, AppConfig& config)
{
    if (const auto* vulkan = root["vulkan"].as_table()) {
        config.viewer.graphics.enable_validation_layers = bool_or_default(
            *vulkan, "validation_layers",
            config.viewer.graphics.enable_validation_layers);
    }
    if (const auto* graphics = root["graphics"].as_table()) {
        config.viewer.graphics.preferred_gpu = string_or_default(
            *graphics, "preferred_gpu", config.viewer.graphics.preferred_gpu);
    }
    if (const auto* render = root["render"].as_table()) {
        config.render.clear_color = float4_or_default(
            *render, "clear_color", config.render.clear_color);
        config.render.initial_point_size = float_or_default(
            *render, "initial_point_size", config.render.initial_point_size);
    }
}

void apply_camera(const toml::table& root, AppConfig& config)
{
    if (const auto* camera = root["camera"].as_table()) {
        config.camera.mode = string_or_default(
            *camera, "mode", config.camera.mode);
        config.camera.position = float3_or_default(
            *camera, "position", config.camera.position);
        config.camera.target = float3_or_default(
            *camera, "target", config.camera.target);
        config.camera.up = float3_or_default(
            *camera, "up", config.camera.up);
        config.camera.fov_y = float_or_default(
            *camera, "fov_y", config.camera.fov_y);
        config.camera.near_plane = float_or_default(
            *camera, "near", config.camera.near_plane);
        config.camera.far_plane = float_or_default(
            *camera, "far", config.camera.far_plane);
    }
}

void apply_controller(const toml::table& root, AppConfig& config)
{
    if (const auto* controller = root["controller"].as_table()) {
        config.controller.rotate_speed = float_or_default(
            *controller, "rotate_speed", config.controller.rotate_speed);
        config.controller.pan_speed = float_or_default(
            *controller, "pan_speed", config.controller.pan_speed);
        config.controller.zoom_speed = float_or_default(
            *controller, "zoom_speed", config.controller.zoom_speed);
        config.controller.invert_rotate_x = bool_or_default(
            *controller, "invert_rotate_x", config.controller.invert_rotate_x);
        config.controller.invert_rotate_y = bool_or_default(
            *controller, "invert_rotate_y", config.controller.invert_rotate_y);
        config.controller.invert_pan_x = bool_or_default(
            *controller, "invert_pan_x", config.controller.invert_pan_x);
        config.controller.invert_pan_y = bool_or_default(
            *controller, "invert_pan_y", config.controller.invert_pan_y);
    }
}

void apply_lod(const toml::table& root, AppConfig& config)
{
    const auto* lod = root["lod"].as_table();
    if (lod == nullptr) {
        return;
    }

    auto& options = config.viewer.lod;
    options.enabled = bool_or_default(*lod, "enabled", options.enabled);
    options.keep_full_buffer = bool_or_default(
        *lod, "keep_full_buffer", options.keep_full_buffer);
    options.sidecar_path = path_or_default(
        *lod, "sidecar_path", options.sidecar_path);
    options.auto_load_sidecar = bool_or_default(
        *lod, "auto_load_sidecar", options.auto_load_sidecar);
    options.auto_save_sidecar = bool_or_default(
        *lod, "auto_save_sidecar", options.auto_save_sidecar);
    options.finest_target_points = uint64_or_default(
        *lod, "finest_target_points", options.finest_target_points);
    options.growth_factor = float_or_default(
        *lod, "growth_factor", options.growth_factor);
    options.min_points_per_level = uint64_or_default(
        *lod, "min_points_per_level", options.min_points_per_level);
    options.voxel_mode = string_or_default(
        *lod, "voxel_mode", options.voxel_mode);
    options.voxel_scale = float_or_default(
        *lod, "voxel_scale", options.voxel_scale);
    options.medium_delay_seconds = static_cast<double>(float_or_default(
        *lod, "medium_delay_seconds",
        static_cast<float>(options.medium_delay_seconds)));
    options.high_delay_seconds = static_cast<double>(float_or_default(
        *lod, "high_delay_seconds",
        static_cast<float>(options.high_delay_seconds)));
    options.use_lowest_while_interacting = bool_or_default(
        *lod, "use_lowest_while_interacting",
        options.use_lowest_while_interacting);
    options.adaptive_interacting_level = bool_or_default(
        *lod, "adaptive_interacting_level", options.adaptive_interacting_level);
    options.frame_time_budget_ms = static_cast<double>(float_or_default(
        *lod, "frame_time_budget_ms",
        static_cast<float>(options.frame_time_budget_ms)));

    const std::string display_mode = string_or_default(
        *lod, "interactive_display_mode", "keep_stable");
    if (display_mode == "coarse" ||
        display_mode == "allow_coarse" ||
        display_mode == "AllowCoarseLOD") {
        options.interactive_display_mode =
            InteractiveDisplayMode::AllowCoarseLOD;
    } else if (display_mode == "freeze_texture" ||
               display_mode == "FreezeLastFrameTexture") {
        options.interactive_display_mode =
            InteractiveDisplayMode::FreezeLastFrameTexture;
    } else {
        options.interactive_display_mode =
            InteractiveDisplayMode::KeepStableHighQuality;
    }
    options.verbose = bool_or_default(*lod, "verbose", options.verbose);
}

void apply_viewport(const toml::table& root, AppConfig& config)
{
    if (const auto* viewport = root["viewport"].as_table()) {
        config.viewer.window.viewport_count = static_cast<int>(
            uint_or_default(
                *viewport,
                "count",
                static_cast<unsigned int>(config.viewer.window.viewport_count))
        );
    }
}

void apply_tile(const toml::table& root, AppConfig& config)
{
    const auto* tile = root["tile"].as_table();
    if (tile == nullptr) {
        return;
    }

    auto& options = config.viewer.tile;
    options.enabled = bool_or_default(*tile, "enabled", options.enabled);
    options.index_path = path_or_default(*tile, "index_path", options.index_path);
    options.data_path = path_or_default(*tile, "data_path", options.data_path);
    options.min_pixel_size = float_or_default(
        *tile, "min_tile_pixel_size", options.min_pixel_size);
    options.max_visible_tiles = uint_or_default(
        *tile, "max_visible_tiles", options.max_visible_tiles);
    options.use_full_z_range = bool_or_default(
        *tile, "use_full_z_range", options.use_full_z_range);
    options.verbose = bool_or_default(*tile, "verbose", options.verbose);
    options.gpu_cache_max_tiles = uint_or_default(
        *tile, "gpu_cache_max_tiles", options.gpu_cache_max_tiles);
    options.gpu_upload_budget_bytes = uint64_or_default(
        *tile, "gpu_upload_budget_bytes", options.gpu_upload_budget_bytes);
    options.cpu_cache_max_bytes = uint64_or_default(
        *tile, "cpu_cache_max_bytes", options.cpu_cache_max_bytes);
    options.preload_all = bool_or_default(
        *tile, "preload_all", options.preload_all);
    options.preload_max_bytes = uint64_or_default(
        *tile, "preload_max_bytes", options.preload_max_bytes);
    options.preload_upload_budget_bytes = uint64_or_default(
        *tile, "preload_upload_budget_bytes",
        options.preload_upload_budget_bytes);
}

void apply_debug(const toml::table& root, AppConfig& config)
{
    if (const auto* debug = root["debug"].as_table()) {
        config.viewer.pick_debug.dump_enabled = bool_or_default(
            *debug, "pick_debug_dump_enabled",
            config.viewer.pick_debug.dump_enabled);
        config.viewer.pick_debug.dump_dir = path_or_default(
            *debug, "pick_debug_dump_dir", config.viewer.pick_debug.dump_dir);
        config.viewer.pick_debug.dump_once_on_hover = bool_or_default(
            *debug, "pick_debug_dump_once_on_hover",
            config.viewer.pick_debug.dump_once_on_hover);
    }
}

} // namespace

void apply_viewer_toml_sections(const toml::table& root, AppConfig& config)
{
    apply_shader(root, config);
    apply_window(root, config);
    apply_graphics(root, config);
    apply_camera(root, config);
    apply_controller(root, config);
    apply_lod(root, config);
    apply_viewport(root, config);
    apply_tile(root, config);
    apply_debug(root, config);
}

} // namespace gs3d::app::detail
