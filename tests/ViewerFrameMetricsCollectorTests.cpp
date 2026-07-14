#include "app/ViewerAppTileStreaming.hpp"
#include "app/ViewerFrameMetricsCollector.hpp"
#include "render/TileSelection.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <future>

TEST_CASE(
    "ViewerFrameMetricsCollector reports clock and pending async work",
    "[frame_metrics]"
)
{
    gs3d::app::ViewerAppTileStreamState tile_stream(
        32ull * 1024ull * 1024ull
    );
    std::promise<gs3d::app::TileLoadResult> pending_load;
    tile_stream.load_future = pending_load.get_future();
    tile_stream.loading_ids = {1, 2, 3};
    const gs3d::render::TileSelectionResult tile_result{};
    const gs3d::app::ViewerFrameMetricsContext context{
        .tile_stream = tile_stream,
        .tile_result = tile_result,
        .dataset_point_count = 42,
        .fps = 60.0f,
        .delta_seconds = 1.0 / 120.0,
        .camera_position = "1.0, 2.0, 3.0"
    };

    const gs3d::app::ViewerFrameMetricsCollector collector;
    const auto metrics = collector.collect(context);

    CHECK(metrics.frame_state.dataset_point_count == 42);
    CHECK(metrics.frame_state.pending_tiles == 3);
    CHECK(metrics.frame_state.fps == Catch::Approx(60.0f));
    CHECK(metrics.frame_state.frame_time_ms == Catch::Approx(1000.0f / 120.0f));
    CHECK(metrics.frame_state.camera_position == "1.0, 2.0, 3.0");
    CHECK(metrics.tile_cache.max_bytes == 32ull * 1024ull * 1024ull);
}
