#include "app/ViewerFrameStateSynchronizer.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

TEST_CASE("ViewerFrameStateSynchronizer mirrors per-viewport render state")
{
    gs3d::app::AppState state;
    state.active_viewport_index = 1;
    state.render_settings_by_view.resize(2);
    state.render_views.resize(2);
    state.render_views[0].viewport_index = 0;
    state.render_views[1].viewport_index = 1;

    std::vector<gs3d::render::PointPushConstants> pushes(2);
    pushes[0].point_size = 1.0f;
    pushes[1].point_size = 3.0f;
    pushes[1].color_source = static_cast<std::uint32_t>(
        gs3d::app::AttrPhysicalSource::Z
    );
    pushes[1].color_min = 7.0f;
    pushes[1].color_range = 3.0f;
    pushes[1].flags = 8u << 1;

    std::vector<gs3d::scene::SceneState> scenes(2);
    scenes[1].active_attribute_index = 1;
    scenes[1].active_height_index = 0;
    const std::vector<float> height_exaggerations{1.0f, 2.5f};
    const gs3d::app::ViewerFrameTileCacheMetrics cache{
        .resident_bytes = 12ull * 1024ull * 1024ull,
        .max_bytes = 64ull * 1024ull * 1024ull,
        .hits = 3,
        .misses = 1
    };
    const gs3d::app::ViewerFrameStateMetrics metrics{
        .dataset_point_count = 100,
        .gpu_resident_points = 80,
        .visible_points = 50,
        .gpu_buffer_bytes = 4096,
        .loaded_tiles = 4,
        .pending_tiles = 2,
        .fps = 60.0f,
        .frame_time_ms = 16.0f,
        .camera_position = "1.0, 2.0, 3.0"
    };

    const gs3d::app::ViewerFrameStateSynchronizer synchronizer(
        100.0,
        288,
        true
    );
    synchronizer.synchronize(
        state,
        pushes,
        scenes,
        height_exaggerations,
        cache,
        metrics
    );

    const auto& active = state.render_settings_by_view[1];
    CHECK(active.point_size == Catch::Approx(3.0f));
    CHECK(active.color_attr_index == 1);
    CHECK(active.height_attr_index == 0);
    CHECK(active.height_exaggeration == Catch::Approx(2.5f));
    CHECK(active.data_value_min == Catch::Approx(107.0f));
    CHECK(active.data_value_max == Catch::Approx(110.0f));
    CHECK(active.cache_usage == "4 / 288");
    CHECK(active.cpu_cache_usage == "12 / 64 MB");
    CHECK(active.cache_hit_rate == Catch::Approx(75.0f));
    CHECK(state.render_settings.point_size == Catch::Approx(3.0f));
    CHECK(state.dataset.loaded_points == 80);
    CHECK(state.performance.lod_mode == "已启用细节层级");
    CHECK(state.status_bar.camera_position == "1.0, 2.0, 3.0");
}
