#include "app/ViewerRuntimeConfiguration.hpp"

namespace gs3d::app {

gs3d::platform::WindowConfig make_window_config(
    const ViewerWindowConfig& config
) {
    return {
        .width = config.width,
        .height = config.height,
        .title = config.title,
        .resizable = config.resizable,
        .visible = config.visible,
    };
}

gs3d::render::VulkanContextConfig make_vulkan_context_config(
    const ViewerGraphicsConfig& config
) {
    return {
        .enable_validation_layers = config.enable_validation_layers,
        .application_name = "GeoScatter3D",
        .preferred_gpu = config.preferred_gpu,
    };
}

gs3d::render::ClearColor make_clear_color(
    const ViewerGraphicsConfig& config
) noexcept {
    return {
        .r = config.clear_color[0],
        .g = config.clear_color[1],
        .b = config.clear_color[2],
        .a = config.clear_color[3],
    };
}

gs3d::render::PointPipelineConfig make_point_pipeline_config(
    const ViewerGraphicsConfig& config
) {
    return {
        .vertex_shader_path = config.vertex_shader_path,
        .fragment_shader_path = config.fragment_shader_path,
    };
}

gs3d::camera::CameraControllerConfig make_camera_controller_config(
    const ViewerControllerConfig& config
) noexcept {
    return {
        .rotate_speed = config.rotate_speed,
        .pan_speed = config.pan_speed,
        .zoom_speed = config.zoom_speed,
        .invert_rotate_x = config.invert_rotate_x,
        .invert_rotate_y = config.invert_rotate_y,
        .invert_pan_x = config.invert_pan_x,
        .invert_pan_y = config.invert_pan_y,
    };
}

gs3d::render::LodSelectorConfig make_lod_selector_config(
    const ViewerLodConfig& config
) {
    return {
        .medium_delay_seconds = config.medium_delay_seconds,
        .high_delay_seconds = config.high_delay_seconds,
        .use_lowest_while_interacting = config.use_lowest_while_interacting,
        .adaptive_interacting_level = config.adaptive_interacting_level,
        .frame_time_budget_ms = config.frame_time_budget_ms,
    };
}

gs3d::render::TileSelectionConfig make_tile_selection_config(
    const ViewerTileConfig& config
) {
    return {
        .min_tile_pixel_size = config.min_pixel_size,
        .max_visible_tiles = config.max_visible_tiles,
        .use_full_z_range = config.use_full_z_range,
    };
}

} // namespace gs3d::app
