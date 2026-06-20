#include "app/TilePointCache.hpp"
#include "app/ViewportResizeScheduler.hpp"
#include "camera/Camera.hpp"
#include "camera/CameraController.hpp"
#include "render/FrameUploadBudget.hpp"
#include "render/LodSelector.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <string_view>

namespace {

int failures = 0;

void expect(bool condition, std::string_view name)
{
    if (!condition) {
        std::cerr << "[FAIL] " << name << '\n';
        ++failures;
    }
}

std::shared_ptr<gs3d::app::TilePoints> make_points(std::size_t count)
{
    return std::make_shared<gs3d::app::TilePoints>(count);
}

void test_resize_debounce()
{
    gs3d::app::ViewportResizeScheduler scheduler(0.15);
    scheduler.observe(0, 800, 600, 0.00);
    scheduler.observe(0, 900, 600, 0.05);
    scheduler.observe(0, 1000, 600, 0.10);

    expect(scheduler.take_ready(0.24).empty(), "resize waits for quiet period");

    const auto ready = scheduler.take_ready(0.25);
    expect(ready.size() == 1, "resize emits once after drag");
    expect(
        ready[0].width == 1000 && ready[0].height == 600,
        "resize emits latest size"
    );

    scheduler.observe(0, 1000, 600, 1.00);
    expect(
        scheduler.take_ready(2.00).empty(),
        "applied size is not emitted repeatedly"
    );
}

void test_resize_batch()
{
    gs3d::app::ViewportResizeScheduler scheduler(0.10);
    scheduler.observe(0, 640, 480, 0.0);
    scheduler.observe(1, 800, 600, 0.0);
    const auto ready = scheduler.take_ready(0.10);
    expect(ready.size() == 2, "multiple viewports resize as one batch");
}

void test_tile_cache_budget_and_lru()
{
    constexpr std::uint64_t tile_bytes =
        4 * sizeof(gs3d::data::Gs3dPoint);
    gs3d::app::TilePointCache cache(tile_bytes * 2);

    cache.put(1, make_points(4));
    cache.put(2, make_points(4));
    expect(static_cast<bool>(cache.get(1)), "cache hit returns tile");

    cache.put(3, make_points(4));
    expect(!cache.get(2), "least recently used tile is evicted");
    expect(static_cast<bool>(cache.get(1)), "recent tile remains cached");
    expect(static_cast<bool>(cache.get(3)), "new tile is cached");

    const auto stats = cache.stats();
    expect(stats.resident_bytes <= stats.max_bytes, "cache respects byte budget");
    expect(stats.tile_count == 2, "cache keeps bounded tile count");
    expect(stats.evictions == 1, "cache reports eviction");

    cache.clear();
    const auto cleared = cache.stats();
    expect(cleared.resident_bytes == 0, "cache clear releases bytes");
    expect(cleared.hits == 0 && cleared.misses == 0, "cache clear resets counters");
}

void test_oversized_tile_is_not_cached()
{
    gs3d::app::TilePointCache cache(sizeof(gs3d::data::Gs3dPoint));
    cache.put(9, make_points(2));
    expect(!cache.get(9), "oversized tile bypasses cache");
    expect(cache.stats().resident_bytes == 0, "oversized tile uses no cache bytes");
}

void test_frame_upload_budget()
{
    gs3d::render::FrameUploadBudget budget(8);
    expect(budget.try_reserve(5), "upload budget accepts first tile");
    expect(!budget.try_reserve(4), "upload budget defers overflow tile");
    expect(budget.try_reserve(3), "upload budget fills remaining bytes");
    expect(budget.reserved_bytes() == 8, "upload budget tracks bytes");

    gs3d::render::FrameUploadBudget oversized(8);
    expect(
        oversized.try_reserve(12),
        "upload budget allows one oversized tile to make progress"
    );
    expect(
        !oversized.try_reserve(1),
        "oversized tile consumes the frame budget"
    );
}

void test_camera_uses_view_local_input()
{
    gs3d::camera::Camera first;
    first.set_viewport(640, 480);
    first.set_perspective(45.0f, 0.1f, 1000.0f);
    first.look_at(
        {0.0f, -10.0f, 4.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    );
    gs3d::camera::Camera second = first;

    gs3d::camera::CameraController controller;
    gs3d::camera::CameraInput input;
    input.viewport_width = 900;
    input.viewport_height = 600;
    input.delta_x = 80.0f;
    input.delta_y = -20.0f;
    input.rotate = true;

    const auto second_position = second.position();
    expect(
        controller.update(first, input),
        "view-local camera input reports a change"
    );
    expect(
        first.viewport_width() == 900 &&
        first.viewport_height() == 600,
        "camera adopts source viewport dimensions"
    );
    expect(
        first.position().x != second_position.x ||
        first.position().y != second_position.y ||
        first.position().z != second_position.z,
        "rotation changes only the target camera"
    );
    expect(
        second.position().x == second_position.x &&
        second.position().y == second_position.y &&
        second.position().z == second_position.z,
        "independent camera remains unchanged"
    );

    gs3d::camera::CameraInput idle;
    idle.viewport_width = 900;
    idle.viewport_height = 600;
    expect(
        !controller.update(second, idle),
        "idle view-local input does not move camera"
    );
}

void test_lod_adaptive_level_starts_at_lowest_while_interacting()
{
    gs3d::render::LodSelectorConfig config;
    config.adaptive_interacting_level = true;
    gs3d::render::LodSelector selector(config);

    selector.update(/*interacting=*/true, 0.016);
    expect(
        selector.select_level(4) == 3,
        "adaptive mode starts at the lowest-detail level (index 3 of 4)"
    );
}

void test_lod_adaptive_level_climbs_after_good_frame_streak()
{
    gs3d::render::LodSelectorConfig config;
    config.adaptive_interacting_level = true;
    config.frame_time_budget_ms = 14.0;
    gs3d::render::LodSelector selector(config);

    selector.update(true, 0.016);
    auto level = selector.select_level(4);
    expect(level == 3, "starts at lowest detail");

    // 30 consecutive in-budget frames are needed to climb one level
    // (see kGoodStreakToClimb) — fewer than that must not climb yet.
    for (int i = 0; i < 29; ++i) {
        selector.report_frame_time(level, 5.0);
    }
    selector.update(true, 0.016);
    level = selector.select_level(4);
    expect(level == 3, "29 good frames is not yet enough to climb");

    selector.report_frame_time(level, 5.0); // 30th good frame
    selector.update(true, 0.016);
    level = selector.select_level(4);
    expect(level == 2, "30th good frame climbs one level of detail");
}

void test_lod_adaptive_level_drops_immediately_when_over_budget()
{
    gs3d::render::LodSelectorConfig config;
    config.adaptive_interacting_level = true;
    config.frame_time_budget_ms = 14.0;
    gs3d::render::LodSelector selector(config);

    selector.update(true, 0.016);
    auto level = selector.select_level(4);

    // Climb to level 1 via two good streaks first.
    for (int cycle = 0; cycle < 2; ++cycle) {
        for (int i = 0; i < 30; ++i) {
            selector.report_frame_time(level, 5.0);
        }
        selector.update(true, 0.016);
        level = selector.select_level(4);
    }
    expect(level == 1, "climbed two levels of detail via good streaks");

    // A single over-budget frame must drop one level immediately, not
    // after a streak — back-off is fast, climbing is slow.
    selector.report_frame_time(level, 20.0);
    selector.update(true, 0.016);
    level = selector.select_level(4);
    expect(level == 2, "one over-budget frame drops one level immediately");
}

void test_lod_adaptive_level_ignores_stale_feedback()
{
    gs3d::render::LodSelectorConfig config;
    config.adaptive_interacting_level = true;
    gs3d::render::LodSelector selector(config);

    selector.update(true, 0.016);
    const auto level = selector.select_level(4);

    // Feedback for a level that isn't the one currently tracked must be
    // ignored rather than corrupting the streak/level state.
    selector.report_frame_time(level + 1, 5.0);
    selector.update(true, 0.016);
    expect(
        selector.select_level(4) == level,
        "feedback for a stale/mismatched level is ignored"
    );
}

void test_lod_non_adaptive_mode_still_pins_to_lowest()
{
    gs3d::render::LodSelectorConfig config;
    config.adaptive_interacting_level = false;
    config.use_lowest_while_interacting = true;
    gs3d::render::LodSelector selector(config);

    selector.update(true, 0.016);
    expect(
        selector.select_level(4) == 3,
        "non-adaptive mode keeps the original pin-to-lowest behavior"
    );

    // report_frame_time() must be a no-op outside adaptive mode.
    selector.report_frame_time(3, 5.0);
    selector.report_frame_time(3, 5.0);
    expect(
        selector.select_level(4) == 3,
        "report_frame_time is a no-op when adaptive_interacting_level is off"
    );
}

} // namespace

int main()
{
    test_resize_debounce();
    test_resize_batch();
    test_tile_cache_budget_and_lru();
    test_oversized_tile_is_not_cached();
    test_frame_upload_budget();
    test_camera_uses_view_local_input();
    test_lod_adaptive_level_starts_at_lowest_while_interacting();
    test_lod_adaptive_level_climbs_after_good_frame_streak();
    test_lod_adaptive_level_drops_immediately_when_over_budget();
    test_lod_adaptive_level_ignores_stale_feedback();
    test_lod_non_adaptive_mode_still_pins_to_lowest();

    if (failures == 0) {
        std::cout << "[PASS] runtime performance tests\n";
    }
    return failures == 0 ? 0 : 1;
}
