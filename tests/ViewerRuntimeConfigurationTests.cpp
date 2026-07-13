#include "app/ViewerRuntimeConfiguration.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Viewer runtime configuration narrows each domain", "[runtime_config]")
{
    gs3d::app::ViewerWindowConfig window;
    window.width = 1600;
    window.height = 900;
    window.title = "runtime test";
    window.resizable = false;
    const auto platform_window = gs3d::app::make_window_config(window);
    CHECK(platform_window.width == 1600);
    CHECK(platform_window.height == 900);
    CHECK(platform_window.title == "runtime test");
    CHECK_FALSE(platform_window.resizable);

    gs3d::app::ViewerGraphicsConfig graphics;
    graphics.enable_validation_layers = false;
    graphics.preferred_gpu = "uuid:portable-test";
    graphics.vertex_shader_path = "vertex.spv";
    graphics.fragment_shader_path = "fragment.spv";
    graphics.clear_color = {0.1f, 0.2f, 0.3f, 0.4f};
    const auto vulkan = gs3d::app::make_vulkan_context_config(graphics);
    const auto clear = gs3d::app::make_clear_color(graphics);
    const auto pipeline = gs3d::app::make_point_pipeline_config(graphics);
    CHECK_FALSE(vulkan.enable_validation_layers);
    CHECK(vulkan.application_name == "GeoScatter3D");
    CHECK(vulkan.preferred_gpu == "uuid:portable-test");
    CHECK(clear.r == Catch::Approx(0.1f));
    CHECK(clear.a == Catch::Approx(0.4f));
    CHECK(pipeline.vertex_shader_path == "vertex.spv");
    CHECK(pipeline.fragment_shader_path == "fragment.spv");
}

TEST_CASE("Viewer runtime configuration preserves controller LOD and tile policy", "[runtime_config]")
{
    gs3d::app::ViewerControllerConfig controller;
    controller.rotate_speed = 1.5f;
    controller.pan_speed = 0.5f;
    controller.zoom_speed = 2.0f;
    controller.invert_pan_y = false;
    const auto camera = gs3d::app::make_camera_controller_config(controller);
    CHECK(camera.rotate_speed == Catch::Approx(1.5f));
    CHECK(camera.pan_speed == Catch::Approx(0.5f));
    CHECK(camera.zoom_speed == Catch::Approx(2.0f));
    CHECK_FALSE(camera.invert_pan_y);

    gs3d::app::ViewerLodConfig lod;
    lod.medium_delay_seconds = 0.3;
    lod.high_delay_seconds = 1.2;
    lod.use_lowest_while_interacting = false;
    lod.adaptive_interacting_level = true;
    lod.frame_time_budget_ms = 11.0;
    const auto selector = gs3d::app::make_lod_selector_config(lod);
    CHECK(selector.medium_delay_seconds == Catch::Approx(0.3));
    CHECK(selector.high_delay_seconds == Catch::Approx(1.2));
    CHECK_FALSE(selector.use_lowest_while_interacting);
    CHECK(selector.adaptive_interacting_level);
    CHECK(selector.frame_time_budget_ms == Catch::Approx(11.0));

    gs3d::app::ViewerTileConfig tile;
    tile.min_pixel_size = 88.0f;
    tile.max_visible_tiles = 42;
    tile.use_full_z_range = false;
    const auto selection = gs3d::app::make_tile_selection_config(tile);
    CHECK(selection.min_tile_pixel_size == Catch::Approx(88.0f));
    CHECK(selection.max_visible_tiles == 42);
    CHECK_FALSE(selection.use_full_z_range);
}
