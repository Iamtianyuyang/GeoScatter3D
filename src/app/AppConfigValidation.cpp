#include "app/AppConfigValidation.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace gs3d::app {

namespace {

[[nodiscard]] bool is_positive_finite(const float value) noexcept
{
    return std::isfinite(value) && value > 0.0f;
}

[[nodiscard]] bool is_nonnegative_finite(const double value) noexcept
{
    return std::isfinite(value) && value >= 0.0;
}

void require(const bool condition, const char* const message)
{
    if (!condition) {
        throw std::runtime_error("AppConfig: " + std::string(message));
    }
}

} // namespace

void validate_app_config(const AppConfig& config)
{
    require(config.viewer.window.width > 0, "window.width must be positive");
    require(config.viewer.window.height > 0, "window.height must be positive");
    require(config.viewer.window.viewport_count > 0,
            "viewport.count must be positive");
    require(is_positive_finite(config.viewer.window.ui_scale_multiplier),
            "window.ui_scale_multiplier must be finite and positive");

    for (const float channel : config.render.clear_color) {
        require(std::isfinite(channel) && channel >= 0.0f && channel <= 1.0f,
                "render.clear_color channels must be finite values in [0, 1]");
    }
    require(is_positive_finite(config.render.initial_point_size),
            "render.initial_point_size must be finite and positive");

    require(std::isfinite(config.camera.fov_y) &&
                config.camera.fov_y >= 1.0f && config.camera.fov_y <= 120.0f,
            "camera.fov_y must be finite and within [1, 120]");
    require(is_positive_finite(config.camera.near_plane),
            "camera.near must be finite and positive");
    require(std::isfinite(config.camera.far_plane) &&
                config.camera.far_plane > config.camera.near_plane,
            "camera.far must be finite and greater than camera.near");

    require(is_nonnegative_finite(config.viewer.lod.medium_delay_seconds),
            "lod.medium_delay_seconds must be finite and non-negative");
    require(is_nonnegative_finite(config.viewer.lod.high_delay_seconds),
            "lod.high_delay_seconds must be finite and non-negative");
    require(config.viewer.lod.finest_target_points > 0,
            "lod.finest_target_points must be positive");
    require(config.viewer.lod.min_points_per_level > 0,
            "lod.min_points_per_level must be positive");
    require(std::isfinite(config.viewer.lod.growth_factor) &&
                config.viewer.lod.growth_factor > 1.0f,
            "lod.growth_factor must be finite and greater than 1");
    require(is_positive_finite(config.viewer.lod.voxel_scale),
            "lod.voxel_scale must be finite and positive");
    require(std::isfinite(config.viewer.lod.frame_time_budget_ms) &&
                config.viewer.lod.frame_time_budget_ms > 0.0,
            "lod.frame_time_budget_ms must be finite and positive");

    require(is_positive_finite(config.viewer.tile.min_pixel_size),
            "tile.min_tile_pixel_size must be finite and positive");
    require(!config.viewer.tile.enabled ||
                config.viewer.tile.max_visible_tiles == 0 ||
                config.viewer.tile.gpu_cache_max_tiles == 0 ||
                config.viewer.tile.gpu_cache_max_tiles >=
                    config.viewer.tile.max_visible_tiles,
            "tile.gpu_cache_max_tiles must be zero (unlimited) or at least tile.max_visible_tiles");
    require(config.viewer.tile.gpu_upload_budget_bytes > 0,
            "tile.gpu_upload_budget_bytes must be positive");
    require(config.viewer.tile.cpu_cache_max_bytes > 0,
            "tile.cpu_cache_max_bytes must be positive");
    require(config.viewer.tile.preload_max_bytes > 0,
            "tile.preload_max_bytes must be positive");
    require(config.viewer.tile.preload_upload_budget_bytes > 0,
            "tile.preload_upload_budget_bytes must be positive");
}

} // namespace gs3d::app
