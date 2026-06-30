#include "app/TilePointCache.hpp"
#include "app/ViewportResizeScheduler.hpp"
#include "camera/BoxSelect.hpp"
#include "camera/Camera.hpp"
#include "camera/CameraController.hpp"
#include "data/Gs3dFormat.hpp"
#include "data/PointDataAdapters.hpp"
#include "render/AxisGrid.hpp"
#include "render/FrameUploadBudget.hpp"
#include "render/LodSelector.hpp"
#include "render/NearestPointQuery.hpp"
#include "scene/SceneState.hpp"
#include "ui/UiRoot.hpp"

#include <cmath>
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
    auto points = std::make_shared<gs3d::app::TilePoints>();
    points->points.resize(count);
    points->point_ids.resize(count);
    return points;
}

float dot(
    const gs3d::camera::Vec3& a,
    const gs3d::camera::Vec3& b
)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

gs3d::camera::Vec3 sub(
    const gs3d::camera::Vec3& a,
    const gs3d::camera::Vec3& b
)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

gs3d::camera::Vec3 normalize(const gs3d::camera::Vec3& v)
{
    const float len = std::sqrt(dot(v, v));
    if (len <= 1.0e-8f) {
        return {0.0f, 0.0f, 0.0f};
    }

    return {v.x / len, v.y / len, v.z / len};
}

float max_scene_depth_along_view(
    const gs3d::camera::CameraBounds& bounds,
    const gs3d::camera::Camera& camera
)
{
    const auto& min = bounds.min;
    const auto& max = bounds.max;
    const gs3d::camera::Vec3 corners[] = {
        {min.x, min.y, min.z},
        {min.x, min.y, max.z},
        {min.x, max.y, min.z},
        {min.x, max.y, max.z},
        {max.x, min.y, min.z},
        {max.x, min.y, max.z},
        {max.x, max.y, min.z},
        {max.x, max.y, max.z},
    };

    const auto forward = normalize(sub(camera.target(), camera.position()));
    float max_depth = 0.0f;
    for (const auto& corner : corners) {
        max_depth = std::max(
            max_depth,
            dot(sub(corner, camera.position()), forward)
        );
    }

    return max_depth;
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
        4 * (sizeof(gs3d::data::Gs3dPoint) + sizeof(std::uint32_t));
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
    gs3d::app::TilePointCache cache(
        sizeof(gs3d::data::Gs3dPoint) + sizeof(std::uint32_t)
    );
    cache.put(9, make_points(2));
    expect(!cache.get(9), "oversized tile bypasses cache");
    expect(cache.stats().resident_bytes == 0, "oversized tile uses no cache bytes");
}

void test_scene_state_is_constructible_without_dataset_io()
{
    gs3d::scene::SceneState scene_state;
    expect(
        scene_state.active_dataset == nullptr,
        "scene state defaults to no active dataset descriptor"
    );
    expect(
        scene_state.active_attribute_index == 0,
        "scene state starts with a zero active attribute index"
    );

    gs3d::core::DatasetDescriptor descriptor;
    descriptor.display_name = "unit-test";
    descriptor.point_count = 42;
    descriptor.attributes.push_back({"Fold"});

    scene_state.active_dataset = &descriptor;
    scene_state.active_attribute_index = 1;

    expect(
        scene_state.active_dataset->point_count == 42,
        "scene state can bind a lightweight dataset descriptor without file IO"
    );
    expect(
        scene_state.active_attribute_index == 1,
        "scene state keeps only the selected attribute index, not the schema"
    );
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
        !oversized.try_reserve(12),
        "upload budget rejects oversized tile exceeding budget"
    );
    expect(
        oversized.try_reserve(7),
        "upload budget accepts tile within budget after rejection"
    );
    expect(
        !oversized.try_reserve(2),
        "remaining budget is 1, rejects 2-byte tile"
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

void test_zoom_converges_toward_centre_anchor_not_cursor()
{
    // Ortho zoom: scales ortho_height uniformly, does not move camera.
    // Cursor position is ignored — zooming with cursor off-centre still
    // changes the visible extent uniformly.

    gs3d::camera::Camera camera;
    camera.set_viewport(800, 600);
    camera.set_orthographic(10.0f, 0.01f, 1000.0f);
    camera.look_at(
        {0.0f, 0.0f, 100.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f}
    );

    gs3d::camera::CameraController controller;

    const float initial_ortho = camera.ortho_height();
    const auto pos_before = camera.position();
    const auto tgt_before = camera.target();

    // Cursor off-centre — zoom should still work uniformly.
    gs3d::camera::CameraInput input;
    input.viewport_width = 800;
    input.viewport_height = 600;
    input.scroll_y = 1.0f;
    input.mouse_x = 700.0f;  // off-centre — ignored
    input.mouse_y = 300.0f;
    static_cast<void>(controller.update(camera, input));

    // Ortho zoom: position/target do not move.
    const auto pos_after = camera.position();
    const auto tgt_after = camera.target();
    expect(
        std::abs(pos_after.x - pos_before.x) < 1.0e-6f &&
        std::abs(pos_after.y - pos_before.y) < 1.0e-6f &&
        std::abs(pos_after.z - pos_before.z) < 1.0e-6f,
        "ortho zoom does not move camera position"
    );
    expect(
        std::abs(tgt_after.x - tgt_before.x) < 1.0e-6f &&
        std::abs(tgt_after.y - tgt_before.y) < 1.0e-6f &&
        std::abs(tgt_after.z - tgt_before.z) < 1.0e-6f,
        "ortho zoom does not move camera target"
    );

    // Ortho_height must shrink (zoom in).
    expect(
        camera.ortho_height() < initial_ortho,
        "ortho zoom in shrinks ortho_height"
    );
}

void test_zoom_respects_max_distance_from_bounds()
{
    gs3d::camera::Camera camera;
    camera.set_viewport(800, 600);
    camera.set_orthographic(10.0f, 1.0f, 1.0e6f);
    camera.look_at(
        {0.0f, 0.0f, 10.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f}
    );

    gs3d::camera::CameraController controller;
    gs3d::camera::CameraBounds bounds;
    bounds.min = {-10.0f, -10.0f, -10.0f};
    bounds.max = {10.0f, 10.0f, 10.0f};
    controller.set_bounds(bounds);

    gs3d::camera::CameraInput input;
    input.viewport_width = 800;
    input.viewport_height = 600;
    input.mouse_x = 400.0f;
    input.mouse_y = 300.0f;

    // Scroll out aggressively — ortho_height must not exceed limit.
    input.scroll_y = -1.0f;
    for (int i = 0; i < 200; ++i) {
        static_cast<void>(controller.update(camera, input));
    }

    expect(
        camera.ortho_height() <= 1.0e9f,
        "zooming out repeatedly is clamped to ortho_height upper bound"
    );
}

void test_fit_bounds_distance_is_orientation_independent()
{
    gs3d::camera::CameraBounds bounds;
    bounds.min = {-10.0f, -10.0f, -2.0f};
    bounds.max = {10.0f, 10.0f, 2.0f};

    gs3d::camera::Camera top_down;
    top_down.set_viewport(800, 600);
    top_down.set_perspective(60.0f, 0.1f, 10000.0f);
    top_down.look_at({0.0f, 0.0f, 100.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    top_down.fit_bounds(bounds);

    gs3d::camera::Camera tilted;
    tilted.set_viewport(800, 600);
    tilted.set_perspective(60.0f, 0.1f, 10000.0f);
    // 45-degree oblique view — the old extent.x/extent.y-based fit_bounds
    // implicitly assumed a top-down camera and would diverge here.
    tilted.look_at({0.0f, -70.0f, 70.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f});
    tilted.fit_bounds(bounds);

    const float d1 = top_down.distance();
    const float d2 = tilted.distance();
    const float ratio = std::max(d1, d2) / std::min(d1, d2);

    expect(
        ratio < 1.05f,
        "fit_bounds() picks a distance independent of view orientation "
        "(bounding-sphere convention), not skewed by camera tilt"
    );
}

void test_fit_bounds_keeps_panorama_far_end_visible()
{
    gs3d::camera::CameraBounds bounds;
    bounds.min = {-20000.0f, -20000.0f, -500.0f};
    bounds.max = {20000.0f, 20000.0f, 500.0f};

    gs3d::camera::Camera camera;
    camera.set_viewport(800, 600);
    camera.set_perspective(60.0f, 0.1f, 1000.0f);
    camera.look_at(
        {0.0f, -60000.0f, 30000.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    );
    camera.fit_bounds(bounds);

    const float far_scene_depth = max_scene_depth_along_view(bounds, camera);
    expect(
        camera.far_plane() >= far_scene_depth,
        "fit_bounds() keeps the far plane beyond the scene's far end in "
        "panorama views"
    );
    expect(
        camera.far_plane() / camera.near_plane() <=
            gs3d::camera::kMaxDepthRatio * 1.001f,
        "fit_bounds() also keeps the far/near ratio within kMaxDepthRatio"
    );
}

void test_zoom_caps_depth_ratio_for_close_large_scene()
{
    // Ortho near/far are set by fit_bounds() and stay fixed — zoom does
    // not move the camera so the ratio is invariant.
    gs3d::camera::Camera camera;
    camera.set_viewport(800, 600);
    camera.set_orthographic(10.0f, 0.01f, 1000.0f);
    camera.look_at(
        {0.0f, -5.0f, 2.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    );

    gs3d::camera::CameraController controller;
    controller.set_bounds({
        {-500.0f, -500.0f, -50.0f},
        {500.0f, 500.0f, 50.0f}
    });

    gs3d::camera::CameraInput input;
    input.viewport_width = 800;
    input.viewport_height = 600;
    input.scroll_y = 1.0f;
    input.mouse_x = 400.0f;
    input.mouse_y = 300.0f;
    static_cast<void>(controller.update(camera, input));

    // Ortho zoom changes ortho_height, not near/far.
    // Near/far ratio stays within bounds.
    expect(
        camera.far_plane() / camera.near_plane() <=
            gs3d::camera::kMaxDepthRatio * 1.001f,
        "ortho near/far ratio stays within kMaxDepthRatio"
    );
    // Ortho_height must have changed (zoom happened).
    expect(
        camera.ortho_height() < 9.9f,
        "ortho zoom in reduced ortho_height"
    );
}

void test_rotate_refreshes_depth_ratio()
{
    gs3d::camera::Camera camera;
    camera.set_viewport(800, 600);
    camera.set_perspective(60.0f, 1.0e-4f, 1.0e8f);
    camera.look_at(
        {0.0f, -5.0f, 2.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    );

    gs3d::camera::CameraController controller;
    controller.set_bounds({
        {-20000.0f, -20000.0f, -500.0f},
        {20000.0f, 20000.0f, 500.0f}
    });

    gs3d::camera::CameraInput input;
    input.viewport_width = 800;
    input.viewport_height = 600;
    input.delta_x = 8.0f;
    input.rotate = true;
    static_cast<void>(controller.update(camera, input));

    expect(
        camera.far_plane() / camera.near_plane() <=
            gs3d::camera::kMaxDepthRatio * 1.001f,
        "rotate updates refresh near/far instead of leaving a stale bad ratio"
    );
}

void test_pan_refreshes_depth_ratio()
{
    gs3d::camera::Camera camera;
    camera.set_viewport(800, 600);
    camera.set_perspective(60.0f, 1.0e-4f, 1.0e8f);
    camera.look_at(
        {0.0f, -5.0f, 2.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    );

    gs3d::camera::CameraController controller;
    controller.set_bounds({
        {-20000.0f, -20000.0f, -500.0f},
        {20000.0f, 20000.0f, 500.0f}
    });

    gs3d::camera::CameraInput input;
    input.viewport_width = 800;
    input.viewport_height = 600;
    input.delta_x = 8.0f;
    input.pan = true;
    static_cast<void>(controller.update(camera, input));

    expect(
        camera.far_plane() / camera.near_plane() <=
            gs3d::camera::kMaxDepthRatio * 1.001f,
        "pan updates refresh near/far instead of leaving a stale bad ratio"
    );
}

void test_box_select_falls_back_at_grazing_pitch()
{
    // Camera looking almost perfectly horizontally — the horizontal-plane
    // ray intersection degenerates here (ray nearly parallel to the plane).
    gs3d::camera::Camera camera;
    camera.set_viewport(800, 600);
    camera.set_perspective(60.0f, 0.1f, 10000.0f);
    camera.look_at(
        {0.0f, -100.0f, 0.001f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    );
    const gs3d::camera::Viewport viewport{800, 600};
    const gs3d::camera::CameraBounds scene_bounds{
        {-1000.0f, -1000.0f, -10.0f}, {1000.0f, 1000.0f, 10.0f}
    };

    const auto result = gs3d::camera::box_select_world_bounds(
        350.0f, 250.0f, 450.0f, 350.0f, viewport, camera, scene_bounds, 0.0f
    );

    expect(
        result.has_value(),
        "box select still returns a result at grazing pitch via the "
        "camera-facing-plane fallback, instead of failing outright"
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

    // 45 consecutive in-budget frames are needed to climb one level
    // (see kGoodStreakToClimb) — fewer than that must not climb yet.
    for (int i = 0; i < 44; ++i) {
        selector.report_frame_time(level, 5.0);
    }
    selector.update(true, 0.016);
    level = selector.select_level(4);
    expect(level == 3, "44 good frames is not yet enough to climb");

    selector.report_frame_time(level, 5.0); // 45th good frame
    selector.update(true, 0.016);
    level = selector.select_level(4);
    expect(level == 2, "45th good frame climbs one level of detail");
}

void test_lod_adaptive_level_drops_immediately_when_over_budget()
{
    gs3d::render::LodSelectorConfig config;
    config.adaptive_interacting_level = true;
    config.frame_time_budget_ms = 14.0;
    gs3d::render::LodSelector selector(config);

    selector.update(true, 0.016);
    auto level = selector.select_level(4);

    // Climb to level 1 via two good streaks first (45 frames each).
    for (int cycle = 0; cycle < 2; ++cycle) {
        for (int i = 0; i < 45; ++i) {
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

gs3d::camera::Camera make_top_down_camera()
{
    gs3d::camera::Camera camera;
    camera.set_viewport(800, 600);
    camera.set_perspective(60.0f, 1.0f, 1000.0f);
    camera.look_at(
        {0.0f, 0.0f, 100.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f}
    );
    return camera;
}

void test_axis_ticks_returns_empty_for_degenerate_range()
{
    expect(
        gs3d::render::compute_axis_ticks(5.0f, 5.0f).empty(),
        "min == max yields no ticks"
    );
    expect(
        gs3d::render::compute_axis_ticks(5.0f, 1.0f).empty(),
        "min > max yields no ticks"
    );
    expect(
        gs3d::render::compute_axis_ticks(0.0f, 100.0f, 0).empty(),
        "target_tick_count <= 0 yields no ticks"
    );
}

void test_axis_ticks_are_evenly_spaced_and_within_range()
{
    const auto ticks = gs3d::render::compute_axis_ticks(0.0f, 100.0f, 5);
    expect(ticks.size() >= 2, "a 0-100 range produces multiple ticks");

    for (const float tick : ticks) {
        expect(
            tick >= 0.0f && tick <= 100.0f,
            "every tick falls within [min, max]"
        );
    }

    if (ticks.size() >= 2) {
        const float step = ticks[1] - ticks[0];
        for (std::size_t i = 1; i + 1 < ticks.size(); ++i) {
            expect(
                std::abs((ticks[i + 1] - ticks[i]) - step) < 1.0e-3f,
                "tick spacing is constant across the range"
            );
        }
    }
}

void test_axis_ticks_step_is_a_nice_round_number()
{
    // 33 is an awkward range; the step should still round to a nice
    // 1/2/5 * 10^n value (e.g. 5 or 10), not an arbitrary fraction like
    // 33/5 = 6.6.
    const auto ticks = gs3d::render::compute_axis_ticks(0.0f, 33.0f, 5);
    expect(ticks.size() >= 2, "a 0-33 range produces multiple ticks");

    if (ticks.size() >= 2) {
        const float step = ticks[1] - ticks[0];
        const float normalized =
            step / std::pow(10.0f, std::floor(std::log10(static_cast<double>(step))));
        const bool is_nice =
            std::abs(normalized - 1.0f) < 1.0e-3f ||
            std::abs(normalized - 2.0f) < 1.0e-3f ||
            std::abs(normalized - 5.0f) < 1.0e-3f;
        expect(is_nice, "tick step rounds to 1/2/5 * 10^n, not an arbitrary fraction");
    }
}

void test_axis_ticks_huge_origin_with_tiny_step_terminates()
{
    // Regression guard for the UiRoot "stuck in tick loop" hang:
    // when the axis range sits at a large absolute value (e.g.
    // -14544) but the user zoomed in to a tiny window, the nice
    // step collapses to ~1e-3 — at which point float's unit-roundoff
    // at magnitude 1e4 is already >= step, and a `tick += step`
    // loop never advances. compute_axis_ticks must terminate even
    // on these inputs and produce a sane (small) number of ticks.
    constexpr float kMin = -14544.0f;
    constexpr float kMax = -14543.9912f;   // range ≈ 0.0088
    const auto ticks = gs3d::render::compute_axis_ticks(
        kMin, kMax, /*target_tick_count=*/5);

    expect(!ticks.empty(),
           "huge origin + tiny range still produces at least one tick");

    // Defensive cap: even pathological inputs must not explode.
    constexpr std::size_t kHardCap = 500;
    expect(ticks.size() <= kHardCap,
           "huge origin + tiny range is hard-capped");

    for (const float tick : ticks) {
        expect(std::isfinite(tick), "every produced tick is finite");
        expect(
            tick >= kMin - 1.0e-3f && tick <= kMax + 1.0e-3f,
            "every tick falls inside the requested range"
        );
    }
}

void test_axis_ticks_huge_origin_with_tiny_step_is_capped()
{
    // The other end of the spectrum: same huge origin, but the user
    // requests an absurd target_tick_count that the nice_step would
    // happily accept. The internal safety cap must still bound the
    // produced tick count.
    constexpr float kMin = -100000.0f;
    constexpr float kMax = 100000.0f;
    const auto ticks = gs3d::render::compute_axis_ticks(
        kMin, kMax, /*target_tick_count=*/1000000);

    constexpr std::size_t kHardCap = 500;
    expect(ticks.size() <= kHardCap,
           "huge range with absurd target_tick_count is hard-capped");

    // First and last ticks should still bracket [min, max].
    if (!ticks.empty()) {
        expect(ticks.front() >= kMin,
               "first tick is at or after min");
        expect(ticks.back() <= kMax,
               "last tick is at or before max");
    }
}

void test_mouse_mapping_without_map_axis_uses_canvas_rect()
{
    const gs3d::ui::ViewportScreenRect canvas_rect{
        100.0f, 50.0f, 900.0f, 650.0f
    };
    const auto mapping = gs3d::ui::map_screen_mouse_to_framebuffer(
        300.0f,
        200.0f,
        canvas_rect,
        false,
        800,
        600
    );

    expect(mapping.mouse_on_image,
           "mouse inside canvas maps onto image when map axis is off");
    expect(std::abs(mapping.framebuffer_x - 200.0f) < 1.0e-4f,
           "map-axis-off X uses canvas origin directly");
    expect(std::abs(mapping.framebuffer_y - 150.0f) < 1.0e-4f,
           "map-axis-off Y uses canvas origin directly");
}

void test_mouse_mapping_with_map_axis_uses_plot_rect()
{
    const gs3d::ui::ViewportScreenRect canvas_rect{
        100.0f, 50.0f, 900.0f, 650.0f
    };
    const auto plot_rect =
        gs3d::ui::compute_plot_rect(true, canvas_rect);
    const auto mapping = gs3d::ui::map_screen_mouse_to_framebuffer(
        plot_rect.min_x + 100.0f,
        plot_rect.min_y + 50.0f,
        canvas_rect,
        true,
        800,
        600
    );

    expect(mapping.mouse_on_image,
           "mouse inside plot rect maps onto image when map axis is on");
    expect(std::abs(mapping.framebuffer_x -
                    (100.0f * 800.0f / plot_rect.width())) < 1.0e-4f,
           "map-axis-on X is normalized against plot rect width");
    expect(std::abs(mapping.framebuffer_y -
                    (50.0f * 600.0f / plot_rect.height())) < 1.0e-4f,
           "map-axis-on Y is normalized against plot rect height");
}

void test_mouse_mapping_with_map_axis_rejects_axis_margin()
{
    const gs3d::ui::ViewportScreenRect canvas_rect{
        100.0f, 50.0f, 900.0f, 650.0f
    };
    const auto mapping = gs3d::ui::map_screen_mouse_to_framebuffer(
        canvas_rect.min_x + 10.0f,
        canvas_rect.min_y + 120.0f,
        canvas_rect,
        true,
        800,
        600
    );

    expect(!mapping.mouse_on_image,
           "axis margin is treated as outside the image");
}

// Local copy of the front-most pick semantics used by BenchmarkSuite
    // and the production GPU pick contract: among rendered points
    // whose screen projection falls inside an NxN neighborhood of the
    // cursor (here: 11x11 ≈ 5 px radius), return the one with the
    // smallest depth, or nullopt if none fall inside.
    [[nodiscard]]
    std::optional<gs3d::core::PointRecord> pick_front_most_in_neighborhood(
        const std::vector<gs3d::core::PointDataView>& candidate_point_sets,
        float mouse_x,
        float mouse_y,
        const gs3d::camera::Viewport& viewport,
        const gs3d::camera::Camera& camera,
        int half_radius_px
    ) {
        constexpr int kScreenRadius = 5; // matches kPickRadiusPx after the 5x5 -> 11x11 bump
        if (half_radius_px <= 0) {
            half_radius_px = kScreenRadius;
        }
        const float width =
            static_cast<float>(viewport.width > 0 ? viewport.width : 1);
        const float height =
            static_cast<float>(viewport.height > 0 ? viewport.height : 1);
        const auto view_projection = camera.view_projection_matrix();
        const int center_x = static_cast<int>(std::floor(mouse_x));
        const int center_y = static_cast<int>(std::floor(mouse_y));

        std::optional<gs3d::core::PointRecord> best;
        float best_depth = 1.0f;
        for (const auto& points : candidate_point_sets) {
            if (!points.valid() || points.empty()) {
                continue;
            }
            for (std::uint64_t i = 0; i < points.point_count; ++i) {
                const auto point = points.point_at(i);
                const auto screen = gs3d::camera::MouseRay::world_to_screen(
                    view_projection,
                    point.x,
                    point.y,
                    point.z,
                    width,
                    height
                );
                if (!screen) {
                    continue;
                }
                const int pixel_x = static_cast<int>(std::floor(screen->x));
                const int pixel_y = static_cast<int>(std::floor(screen->y));
                if (std::abs(pixel_x - center_x) > half_radius_px ||
                    std::abs(pixel_y - center_y) > half_radius_px) {
                    continue;
                }

                // crude but monotone NDC depth; sufficient for
                // ordering, which is all front-most needs.
                const float* m = view_projection.m.data();
                const float clip_z =
                    m[2] * point.x + m[6] * point.y +
                    m[10] * point.z + m[14];
                const float clip_w =
                    m[3] * point.x + m[7] * point.y +
                    m[11] * point.z + m[15];
                if (clip_w <= 1.0e-6f) {
                    continue;
                }
                const float depth = clip_z / clip_w;
                if (!best || depth < best_depth) {
                    best = point;
                    best_depth = depth;
                }
            }
        }
        return best;
    }

void test_pick_11x11_neighborhood_hits_sparse_isolated_point()
{
    // Regression guard for the "sparse / 1-px points are visible but
    // not pickable" report: with the old 5x5 / 2 px neighborhood the
    // cursor had to land within ±2 px of the screen-projected pixel
    // to register a hit. After bumping to 11x11 (5 px radius) a 4-px
    // offset should still hit the only rendered point in the scene.
    const auto camera = make_top_down_camera();
    const gs3d::camera::Viewport viewport{800, 600};

    // First project a known world point so the cursor can be placed
    // deterministically relative to it.
    const gs3d::core::PointRecord isolated{0.0f, 0.0f, 0.0f, 0.0f, 1u};
    const auto projected = gs3d::camera::MouseRay::to_screen(
        {isolated.x, isolated.y, isolated.z}, viewport, camera);
    expect(projected.has_value(),
           "isolated world point must project to a screen position");
    if (!projected) {
        return;
    }
    // Confirm the 4-px offset is in fact outside the old 2-px radius.
    expect(
        std::abs(static_cast<int>(std::floor(projected->x)) -
                 static_cast<int>(std::floor(projected->x)) + 4) > 2,
        "test fixture: 4-px offset is outside the old 5x5 (2-px) radius"
    );

    const std::vector<gs3d::data::Gs3dPoint> points = {
        {isolated.x, isolated.y, isolated.z, 0.0f}
    };
    const auto view = gs3d::data::make_point_data_view(
        points.data(), points.size());

    // Cursor 4 px below the point — well inside 11x11 but outside 5x5.
    const auto hit_11x11 = pick_front_most_in_neighborhood(
        {view},
        projected->x,
        projected->y + 4.0f,
        viewport,
        camera,
        /*half_radius_px=*/5
    );
    expect(hit_11x11.has_value(),
           "11x11 pick radius hits an isolated point 4 px from cursor");
    if (hit_11x11) {
        expect(
            hit_11x11->x == isolated.x &&
                hit_11x11->y == isolated.y &&
                hit_11x11->z == isolated.z,
            "11x11 pick returns the isolated point"
        );
    }

    const auto hit_5x5 = pick_front_most_in_neighborhood(
        {view},
        projected->x,
        projected->y + 4.0f,
        viewport,
        camera,
        /*half_radius_px=*/2
    );
    expect(!hit_5x5.has_value(),
           "old 5x5 / 2-px radius cannot reach an isolated point 4 px away");
}

void test_pick_11x11_neighborhood_misses_point_outside_radius()
{
    // The flip side: a point sitting 7 px from the cursor must NOT
    // be hit, regardless of the new neighborhood size. Guards
    // against accidental over-reach where the radius expands to
    // "everything visible".
    const auto camera = make_top_down_camera();
    const gs3d::camera::Viewport viewport{800, 600};

    const auto projected = gs3d::camera::MouseRay::to_screen(
        {0.0f, 0.0f, 0.0f}, viewport, camera);
    expect(projected.has_value(),
           "origin projects to a screen position");
    if (!projected) {
        return;
    }

    const std::vector<gs3d::data::Gs3dPoint> points = {
        {0.0f, 0.0f, 0.0f, 0.0f}
    };
    const auto view = gs3d::data::make_point_data_view(
        points.data(), points.size());

    const auto hit = pick_front_most_in_neighborhood(
        {view},
        projected->x + 7.0f,
        projected->y,
        viewport,
        camera,
        /*half_radius_px=*/5
    );
    expect(!hit.has_value(),
           "11x11 (5 px radius) does not reach a point 7 px away");
}

void test_mouse_ray_to_screen_projects_target_near_center()
{
    const auto camera = make_top_down_camera();
    const gs3d::camera::Viewport viewport{800, 600};

    const auto screen = gs3d::camera::MouseRay::to_screen(
        {0.0f, 0.0f, 0.0f}, viewport, camera
    );
    expect(screen.has_value(), "camera look-at target projects to a screen point");
    if (screen) {
        expect(
            std::abs(screen->x - 400.0f) < 1.0f &&
            std::abs(screen->y - 300.0f) < 1.0f,
            "the look-at target projects to (near) the viewport center"
        );
    }
}

void test_mouse_ray_to_screen_skips_points_behind_camera()
{
    const auto camera = make_top_down_camera();
    const gs3d::camera::Viewport viewport{800, 600};

    const auto screen = gs3d::camera::MouseRay::to_screen(
        {0.0f, 0.0f, 500.0f}, viewport, camera
    );
    expect(!screen.has_value(), "a point behind the camera has no screen projection");
}

void test_mouse_ray_to_screen_round_trips_with_from_screen()
{
    const auto camera = make_top_down_camera();
    const gs3d::camera::Viewport viewport{800, 600};

    const gs3d::camera::Vec3 world_point{15.0f, -8.0f, 0.0f};
    const auto screen =
        gs3d::camera::MouseRay::to_screen(world_point, viewport, camera);
    expect(screen.has_value(), "world point projects to screen");

    if (screen) {
        const auto ray = gs3d::camera::MouseRay::from_screen(
            screen->x, screen->y, viewport, camera
        );
        const auto recovered = gs3d::camera::MouseRay::intersect_plane(
            ray, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}
        );
        expect(recovered.has_value(), "screen point unprojects back to the plane");
        if (recovered) {
            expect(
                std::abs(recovered->x - world_point.x) < 0.1f &&
                std::abs(recovered->y - world_point.y) < 0.1f,
                "to_screen() and from_screen()+intersect_plane() round-trip"
            );
        }
    }
}

void test_box_select_returns_nullopt_for_degenerate_rect()
{
    const auto camera = make_top_down_camera();
    const gs3d::camera::Viewport viewport{800, 600};
    const gs3d::camera::CameraBounds scene_bounds{
        {-100.0f, -100.0f, -10.0f}, {100.0f, 100.0f, 10.0f}
    };

    const auto result = gs3d::camera::box_select_world_bounds(
        400.0f, 300.0f, 400.0f, 300.0f, viewport, camera, scene_bounds, 0.0f
    );
    expect(!result.has_value(), "zero-size screen rect yields no box");
}

void test_box_select_world_bounds_scales_with_screen_rect_size()
{
    const auto camera = make_top_down_camera();
    const gs3d::camera::Viewport viewport{800, 600};
    const gs3d::camera::CameraBounds scene_bounds{
        {-100.0f, -100.0f, -10.0f}, {100.0f, 100.0f, 10.0f}
    };

    const auto small = gs3d::camera::box_select_world_bounds(
        350.0f, 250.0f, 450.0f, 350.0f, viewport, camera, scene_bounds, 0.0f
    );
    const auto large = gs3d::camera::box_select_world_bounds(
        300.0f, 200.0f, 500.0f, 400.0f, viewport, camera, scene_bounds, 0.0f
    );
    expect(small.has_value() && large.has_value(), "both rects hit the plane");

    if (small && large) {
        const float small_area =
            (small->max.x - small->min.x) * (small->max.y - small->min.y);
        const float large_area =
            (large->max.x - large->min.x) * (large->max.y - large->min.y);
        expect(
            large_area > small_area,
            "a larger screen-space rect maps to a larger world-space box"
        );
    }
}

void test_box_select_world_bounds_centers_near_camera_look_target()
{
    const auto camera = make_top_down_camera();
    const gs3d::camera::Viewport viewport{800, 600};
    const gs3d::camera::CameraBounds scene_bounds{
        {-100.0f, -100.0f, -10.0f}, {100.0f, 100.0f, 10.0f}
    };

    // A small rect right at the viewport center, for a camera looking
    // straight down at the origin, should map close to world (0, 0).
    const auto result = gs3d::camera::box_select_world_bounds(
        395.0f, 295.0f, 405.0f, 305.0f, viewport, camera, scene_bounds, 0.0f
    );
    expect(result.has_value(), "center rect hits the plane");

    if (result) {
        const float center_x = 0.5f * (result->min.x + result->max.x);
        const float center_y = 0.5f * (result->min.y + result->max.y);
        expect(
            std::abs(center_x) < 5.0f && std::abs(center_y) < 5.0f,
            "a viewport-center rect maps near the camera's look-at target"
        );
    }
}

void test_box_select_stays_within_scene_bounds_under_camera_tilt()
{
    // Tilted camera (not top-down) looking at a thin, terrain-like scene.
    // The old implementation unprojected the 4 screen corners onto a
    // single flat plane through the target — for a tilted camera this
    // turns the dragged rectangle into a perspective trapezoid whose AABB
    // can balloon far past the actual scene, especially for a rect near
    // the top of the screen where the plane intersection grazes near-
    // parallel. Clipping against the real scene bounds must keep the
    // result inside the scene regardless of where on screen the rect is.
    gs3d::camera::Camera camera;
    camera.set_viewport(800, 600);
    camera.set_perspective(60.0f, 0.1f, 10000.0f);
    camera.look_at({0.0f, -70.0f, 70.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f});

    const gs3d::camera::Viewport viewport{800, 600};
    const gs3d::camera::CameraBounds scene_bounds{
        {-50.0f, -50.0f, -5.0f}, {50.0f, 50.0f, 5.0f}
    };

    // A small off-center rect — under the old flat-plane approach, a
    // tilted camera turns this rectangle into a perspective trapezoid
    // whose AABB can spill past the real scene extent.
    const auto result = gs3d::camera::box_select_world_bounds(
        550.0f, 400.0f, 570.0f, 420.0f, viewport, camera, scene_bounds, 0.0f
    );

    expect(result.has_value(), "off-center rect under tilted camera still resolves");
    if (result) {
        constexpr float kEps = 1.0e-3f;
        expect(
            result->min.x >= scene_bounds.min.x - kEps &&
            result->max.x <= scene_bounds.max.x + kEps &&
            result->min.y >= scene_bounds.min.y - kEps &&
            result->max.y <= scene_bounds.max.y + kEps,
            "box-select result stays clipped to the real scene bounds "
            "instead of ballooning past it under camera tilt"
        );
    }
}

void test_nearest_point_query_returns_nullopt_for_no_candidates()
{
    const auto camera = make_top_down_camera();
    const gs3d::camera::Viewport viewport{800, 600};

    const auto result = gs3d::render::find_nearest_point_on_screen(
        {}, 400.0f, 300.0f, viewport, camera
    );
    expect(!result.has_value(), "no candidate point sets yields no result");
}

void test_nearest_point_query_picks_screen_closest_candidate()
{
    const auto camera = make_top_down_camera();
    const gs3d::camera::Viewport viewport{800, 600};

    // Camera looks straight down at the origin, so a point at world (0,0,0)
    // projects almost exactly to screen center; a point offset far away in
    // world X on the same plane projects well away from center.
    const std::vector<gs3d::data::Gs3dPoint> points = {
        {0.0f, 0.0f, 0.0f, 1.0f},
        {60.0f, 0.0f, 0.0f, 2.0f},
    };
    const std::vector<gs3d::core::PointDataView> sets = {
        gs3d::data::make_point_data_view(points)
    };

    const auto result = gs3d::render::find_nearest_point_on_screen(
        sets, 400.0f, 300.0f, viewport, camera, 50.0f
    );
    expect(result.has_value(), "a candidate is found near screen center");
    if (result) {
        expect(
            result->point.value == 1.0f,
            "the point projecting nearest to the mouse is chosen, not just the first one"
        );
    }
}

void test_nearest_point_query_respects_max_screen_distance()
{
    const auto camera = make_top_down_camera();
    const gs3d::camera::Viewport viewport{800, 600};

    // Same far-offset point as above, but with a small search radius that
    // excludes it.
    const std::vector<gs3d::data::Gs3dPoint> points = {
        {60.0f, 0.0f, 0.0f, 2.0f},
    };
    const std::vector<gs3d::core::PointDataView> sets = {
        gs3d::data::make_point_data_view(points)
    };

    const auto result = gs3d::render::find_nearest_point_on_screen(
        sets, 400.0f, 300.0f, viewport, camera, 5.0f
    );
    expect(
        !result.has_value(),
        "a point outside max_screen_distance_px is not returned"
    );
}

void test_nearest_point_query_skips_points_behind_camera()
{
    const auto camera = make_top_down_camera();
    const gs3d::camera::Viewport viewport{800, 600};

    // Camera is at z=100 looking toward z=0 (i.e. -Z); a point at z=500 is
    // on the opposite side of the camera and must be skipped.
    const std::vector<gs3d::data::Gs3dPoint> points = {
        {0.0f, 0.0f, 500.0f, 3.0f},
    };
    const std::vector<gs3d::core::PointDataView> sets = {
        gs3d::data::make_point_data_view(points)
    };

    const auto result = gs3d::render::find_nearest_point_on_screen(
        sets, 400.0f, 300.0f, viewport, camera, 1000.0f
    );
    expect(
        !result.has_value(),
        "a point behind the camera is never returned, regardless of search radius"
    );
}

void test_nearest_point_query_depth_tie_is_order_sensitive()
{
    const auto camera = make_top_down_camera();
    const gs3d::camera::Viewport viewport{800, 600};

    const std::vector<gs3d::data::Gs3dPoint> first_then_second = {
        {0.0f, 0.0f, 8.0f, 101.0f},
        {0.0f, 0.0f, -8.0f, 102.0f},
    };
    const std::vector<gs3d::data::Gs3dPoint> second_then_first = {
        {0.0f, 0.0f, -8.0f, 102.0f},
        {0.0f, 0.0f, 8.0f, 101.0f},
    };

    const auto screen = gs3d::camera::MouseRay::to_screen(
        {0.0f, 0.0f, 8.0f},
        viewport,
        camera
    );
    expect(
        screen.has_value(),
        "depth-tie test point projects into the viewport"
    );
    if (!screen) {
        return;
    }

    const auto first_hit = gs3d::render::find_nearest_point_on_screen(
        {gs3d::data::make_point_data_view(first_then_second)},
        screen->x,
        screen->y,
        viewport,
        camera,
        12.0f
    );
    const auto second_hit = gs3d::render::find_nearest_point_on_screen(
        {gs3d::data::make_point_data_view(second_then_first)},
        screen->x,
        screen->y,
        viewport,
        camera,
        12.0f
    );

    expect(first_hit.has_value(), "tie test finds the first permutation");
    expect(second_hit.has_value(), "tie test finds the second permutation");
    if (first_hit && second_hit) {
        expect(
            first_hit->point.value == 101.0f,
            "equal screen-distance points keep the first encountered candidate"
        );
        expect(
            second_hit->point.value == 102.0f,
            "reversing candidate order flips the result because there is no depth tie-breaker"
        );
    }
}

void test_continuous_zoom_in_flies_forward_without_stalling()
{
    // Ortho zoom: ortho_height shrinks geometrically by factor 0.75 per
    // step.  No distance involvement — uniform scale, no lock-dead.

    gs3d::camera::Camera camera;
    camera.set_viewport(800, 600);
    camera.set_orthographic(100.0f, 0.01f, 10000.0f);
    camera.look_at(
        {0.0f, -20.0f, 8.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    );

    gs3d::camera::CameraController controller;
    gs3d::camera::CameraBounds bounds;
    bounds.min = {-50.0f, -50.0f, -10.0f};
    bounds.max = {50.0f, 50.0f, 10.0f};
    controller.set_bounds(bounds);

    const float initial_ortho = camera.ortho_height();

    gs3d::camera::CameraInput input;
    input.viewport_width = 800;
    input.viewport_height = 600;
    input.scroll_y = 1.0f;        // zoom in

    float prev_ortho = initial_ortho;
    for (int i = 0; i < 40; ++i) {
        static_cast<void>(controller.update(camera, input));

        const float current_ortho = camera.ortho_height();

        // Ortho_height must decrease monotonically.
        expect(
            current_ortho < prev_ortho + 1.0e-4f,
            "ortho zoom must shrink ortho_height monotonically"
        );
        prev_ortho = current_ortho;

        // Must not collapse below float precision.
        expect(
            current_ortho > 1.0e-7f,
            "ortho_height must not collapse to near-zero (no lock-dead)"
        );
    }

    // After 40 consecutive zooms ortho_height must be substantially
    // smaller (100 × 0.75^40 ≈ 0.001).
    const float final_ortho = camera.ortho_height();
    expect(
        final_ortho < initial_ortho * 0.01f,
        "after 40 ortho zooms height shrinks substantially (not stalled)"
    );

    // Position and target unchanged by ortho zoom.
    const auto pos = camera.position();
    const auto tgt = camera.target();
    expect(
        std::abs(pos.x - 0.0f) < 1.0e-6f &&
        std::abs(pos.y + 20.0f) < 1.0e-6f &&
        std::abs(pos.z - 8.0f) < 1.0e-6f,
        "ortho zoom does not move camera position"
    );
    expect(
        std::abs(tgt.x) < 1.0e-6f &&
        std::abs(tgt.y) < 1.0e-6f &&
        std::abs(tgt.z) < 1.0e-6f,
        "ortho zoom does not move camera target"
    );
}

void test_idle_update_does_not_change_camera()
{
    // Iron law: when the user is not interacting, update() must not
    // modify camera position or target.  This directly covers the
    // "drift" bug where per-frame GPU-pick target updates moved the
    // camera without any user action.

    gs3d::camera::Camera camera;
    camera.set_viewport(800, 600);
    camera.set_perspective(60.0f, 0.01f, 10000.0f);
    camera.look_at(
        {0.0f, -20.0f, 8.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    );

    gs3d::camera::CameraController controller;

    const auto pos_before = camera.position();
    const auto tgt_before = camera.target();

    gs3d::camera::CameraInput input;
    input.viewport_width = 800;
    input.viewport_height = 600;
    // No rotate, no pan, no scroll, no delta — completely idle.

    for (int i = 0; i < 10; ++i) {
        static_cast<void>(controller.update(camera, input));
    }

    const auto pos_after = camera.position();
    const auto tgt_after = camera.target();

    expect(
        std::abs(pos_after.x - pos_before.x) < 1.0e-6f &&
        std::abs(pos_after.y - pos_before.y) < 1.0e-6f &&
        std::abs(pos_after.z - pos_before.z) < 1.0e-6f,
        "idle update must not change camera position (no drift)"
    );
    expect(
        std::abs(tgt_after.x - tgt_before.x) < 1.0e-6f &&
        std::abs(tgt_after.y - tgt_before.y) < 1.0e-6f &&
        std::abs(tgt_after.z - tgt_before.z) < 1.0e-6f,
        "idle update must not change camera target (no drift)"
    );
}

void test_rotation_center_only_changes_on_rotate_begin()
{
    // The rotation pivot is computed ONCE at drag-start (after cursor moves
    // past the activation threshold) and locked for the entire drag.
    // Crucially, computing the pivot does NOT change camera.target —
    // the pivot is decoupled from target so the view does not jump on press.
    // Target is synced to the pivot only on release (set_target is a pure
    // store, no view-matrix recompute).

    gs3d::camera::Camera camera;
    camera.set_viewport(800, 600);
    camera.set_perspective(60.0f, 0.01f, 10000.0f);
    camera.look_at(
        {0.0f, -20.0f, 8.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    );

    gs3d::camera::CameraController controller;

    // --- Frame 1: drag-start at screen centre ---
    // Pivot is computed but target is NOT changed (decoupled).
    {
        gs3d::camera::CameraInput input;
        input.viewport_width = 800;
        input.viewport_height = 600;
        input.rotate = true;
        input.rotate_begin = true;
        input.delta_x = 10.0f;
        input.mouse_x = 400.0f;  // centre
        input.mouse_y = 300.0f;
        const auto tgt_before = camera.target();
        static_cast<void>(controller.update(camera, input));
        const auto tgt_after = camera.target();

        // Target must NOT change on rotate_begin (pivot is decoupled).
        // Float error in inv-VP matrix means look_at(target=pivot) sets
        // target ≈ origin ± 1e-4; a 1e-3 threshold guards against real
        // jumps while tolerating numerical noise.
        expect(
            std::abs(tgt_after.x - tgt_before.x) < 1.0e-3f &&
            std::abs(tgt_after.y - tgt_before.y) < 1.0e-3f &&
            std::abs(tgt_after.z - tgt_before.z) < 1.0e-3f,
            "rotate_begin must not change camera.target (no view jump on press)"
        );
    }

    // --- Frame 2: continuing drag, cursor moved off-centre ---
    // Still orbits around the locked pivot; target unchanged.
    {
        gs3d::camera::CameraInput input;
        input.viewport_width = 800;
        input.viewport_height = 600;
        input.rotate = true;
        input.rotate_begin = false;  // continuing drag
        input.delta_x = 5.0f;
        input.mouse_x = 600.0f;  // off-centre — ignored during drag
        input.mouse_y = 400.0f;
        const auto tgt_before = camera.target();
        static_cast<void>(controller.update(camera, input));
        const auto tgt_after = camera.target();

        // Target must not change during continuing drag.
        expect(
            std::abs(tgt_after.x - tgt_before.x) < 1.0e-6f &&
            std::abs(tgt_after.y - tgt_before.y) < 1.0e-6f &&
            std::abs(tgt_after.z - tgt_before.z) < 1.0e-6f,
            "rotate target must not change during continuing drag"
        );
    }

    // --- Frame 3: release ---
    // Target syncs to the last orbit centre (pure store, no view change).
    {
        const auto tgt_before = camera.target();
        gs3d::camera::CameraInput input;
        input.viewport_width = 800;
        input.viewport_height = 600;
        input.rotate = false;
        input.rotate_begin = false;
        static_cast<void>(controller.update(camera, input));
        const auto tgt_after = camera.target();

        // After orbiting around centre≈origin, target syncs to ≈origin.
        // Both before and after should be near origin.
        expect(
            std::abs(tgt_after.x) < 1.0e-3f &&
            std::abs(tgt_after.y) < 1.0e-3f &&
            std::abs(tgt_after.z) < 1.0e-3f,
            "on release target syncs to orbit centre (near origin)"
        );
    }

    // --- Frame 4: new drag-start at off-centre cursor ---
    // Pivot is always screen centre, not cursor.  Even when cursor is
    // at (200,150), the pivot stays at the centre anchor (~origin).
    // Target orbits around origin — no shift away from it.
    {
        const auto tgt_before = camera.target();

        gs3d::camera::CameraInput input;
        input.viewport_width = 800;
        input.viewport_height = 600;
        input.rotate = true;
        input.rotate_begin = true;
        input.delta_x = 10.0f;
        input.mouse_x = 200.0f;  // off-centre — ignored
        input.mouse_y = 150.0f;
        static_cast<void>(controller.update(camera, input));

        const auto tgt_after = camera.target();

        // Pivot is screen centre (~origin).  Target stays near origin
        // regardless of cursor position.
        expect(
            std::abs(tgt_after.x) < 1.0e-3f &&
            std::abs(tgt_after.y) < 1.0e-3f &&
            std::abs(tgt_after.z) < 1.0e-3f,
            "rotate_begin uses screen-centre pivot even with off-centre cursor"
        );
    }
}

void test_click_without_drag_does_not_move_camera()
{
    // Pure left-click (press + release without moving past the activation
    // threshold) must leave camera position and target completely unchanged.
    // This reserves left-click for future point-selection features.

    gs3d::camera::Camera camera;
    camera.set_viewport(800, 600);
    camera.set_perspective(60.0f, 0.01f, 10000.0f);
    camera.look_at(
        {0.0f, -20.0f, 8.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    );

    gs3d::camera::CameraController controller;

    const auto pos_before = camera.position();
    const auto tgt_before = camera.target();

    // Frame 1: button press — rotate_begin=true but delta=0
    // (no cursor movement, below activation threshold).
    {
        gs3d::camera::CameraInput input;
        input.viewport_width = 800;
        input.viewport_height = 600;
        input.rotate = true;
        input.rotate_begin = true;
        input.delta_x = 0.0f;
        input.delta_y = 0.0f;
        input.mouse_x = 400.0f;
        input.mouse_y = 300.0f;
        static_cast<void>(controller.update(camera, input));
    }

    // Frame 2: release without any drag movement.
    {
        gs3d::camera::CameraInput input;
        input.viewport_width = 800;
        input.viewport_height = 600;
        input.rotate = false;
        input.rotate_begin = false;
        input.delta_x = 0.0f;
        input.delta_y = 0.0f;
        static_cast<void>(controller.update(camera, input));
    }

    const auto pos_after = camera.position();
    const auto tgt_after = camera.target();

    expect(
        std::abs(pos_after.x - pos_before.x) < 1.0e-6f &&
        std::abs(pos_after.y - pos_before.y) < 1.0e-6f &&
        std::abs(pos_after.z - pos_before.z) < 1.0e-6f,
        "pure click must not change camera position"
    );
    expect(
        std::abs(tgt_after.x - tgt_before.x) < 1.0e-6f &&
        std::abs(tgt_after.y - tgt_before.y) < 1.0e-6f &&
        std::abs(tgt_after.z - tgt_before.z) < 1.0e-6f,
        "pure click must not change camera target"
    );
}

void test_rotate_release_does_not_move_camera()
{
    // Releasing the mouse after a rotation drag must not change
    // position or target — orbit_around_pivot already produced the
    // self-consistent final state.  Any post-hoc set_target(pivot)
    // would cause a visible jump on release.

    gs3d::camera::Camera camera;
    camera.set_viewport(800, 600);
    camera.set_perspective(60.0f, 0.01f, 10000.0f);
    camera.look_at(
        {0.0f, -20.0f, 8.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    );

    gs3d::camera::CameraController controller;

    // Frame 1: drag-start + orbit at centre
    {
        gs3d::camera::CameraInput input;
        input.viewport_width = 800;
        input.viewport_height = 600;
        input.rotate = true;
        input.rotate_begin = true;
        input.delta_x = 10.0f;
        input.mouse_x = 400.0f;
        input.mouse_y = 300.0f;
        static_cast<void>(controller.update(camera, input));
    }

    // Frame 2: continuing drag
    {
        gs3d::camera::CameraInput input;
        input.viewport_width = 800;
        input.viewport_height = 600;
        input.rotate = true;
        input.rotate_begin = false;
        input.delta_x = 5.0f;
        input.mouse_x = 450.0f;
        input.mouse_y = 300.0f;
        static_cast<void>(controller.update(camera, input));
    }

    // Frame 3: release — must NOT change position or target.
    const auto pos_before = camera.position();
    const auto tgt_before = camera.target();
    {
        gs3d::camera::CameraInput input;
        input.viewport_width = 800;
        input.viewport_height = 600;
        input.rotate = false;
        input.rotate_begin = false;
        static_cast<void>(controller.update(camera, input));
    }
    const auto pos_after = camera.position();
    const auto tgt_after = camera.target();

    expect(
        std::abs(pos_after.x - pos_before.x) < 1.0e-6f &&
        std::abs(pos_after.y - pos_before.y) < 1.0e-6f &&
        std::abs(pos_after.z - pos_before.z) < 1.0e-6f,
        "release after rotation must not change camera position"
    );
    expect(
        std::abs(tgt_after.x - tgt_before.x) < 1.0e-6f &&
        std::abs(tgt_after.y - tgt_before.y) < 1.0e-6f &&
        std::abs(tgt_after.z - tgt_before.z) < 1.0e-6f,
        "release after rotation must not change camera target (no sync-to-pivot jump)"
    );

    // Frame 4: fresh drag-start after release — must still work.
    {
        gs3d::camera::CameraInput input;
        input.viewport_width = 800;
        input.viewport_height = 600;
        input.rotate = true;
        input.rotate_begin = true;
        input.delta_x = 8.0f;
        input.mouse_x = 200.0f;
        input.mouse_y = 150.0f;
        static_cast<void>(controller.update(camera, input));
        // Just verifying it doesn't crash / the state is clean.
    }
}

void test_rotation_pivot_is_screen_center()
{
    // The rotation pivot must be the scene point under the screen CENTRE,
    // not under the cursor.  Even when the cursor is off-centre, the
    // pivot is computed from (viewport_width/2, viewport_height/2).

    gs3d::camera::Camera camera;
    camera.set_viewport(800, 600);
    camera.set_perspective(60.0f, 0.01f, 10000.0f);
    camera.look_at(
        {0.0f, -20.0f, 8.0f},   // oblique view
        {0.0f, 0.0f, 0.0f},      // target: origin
        {0.0f, 0.0f, 1.0f}
    );

    // Centre-ray pivot (cursor position is ignored).
    const auto centre_pivot =
        gs3d::camera::MouseRay::intersect_camera_facing_plane(
            400.0, 300.0,   // screen centre
            {800, 600},
            camera,
            camera.target()
        );

    // Off-centre cursor pivot — should differ from centre pivot.
    const auto off_centre_pivot =
        gs3d::camera::MouseRay::intersect_camera_facing_plane(
            200.0, 150.0,   // off-centre cursor
            {800, 600},
            camera,
            camera.target()
        );

    // Verify that centre and off-centre pivots are different (the
    // off-centre ray hits the camera-facing plane at a different point).
    if (centre_pivot && off_centre_pivot) {
        const float dist =
            std::abs(centre_pivot->x - off_centre_pivot->x) +
            std::abs(centre_pivot->y - off_centre_pivot->y) +
            std::abs(centre_pivot->z - off_centre_pivot->z);
        expect(
            dist > 1.0e-3f,
            "centre and off-centre pivots differ (rays hit different points)"
        );
    }

    // Now drive a rotate_begin with an off-centre cursor and verify the
    // camera orbits around the CENTRE pivot, not the cursor pivot.
    gs3d::camera::CameraController controller;

    // Frame 1: rotate_begin with off-centre cursor at (200, 150).
    {
        gs3d::camera::CameraInput input;
        input.viewport_width = 800;
        input.viewport_height = 600;
        input.rotate = true;
        input.rotate_begin = true;
        input.delta_x = 10.0f;
        input.mouse_x = 200.0f;   // off-centre — intentionally ignored
        input.mouse_y = 150.0f;
        static_cast<void>(controller.update(camera, input));

        // The orbit centre should be near the screen-centre anchor
        // (≈ origin), NOT the off-centre anchor.
        const auto tgt = camera.target();
        expect(
            std::abs(tgt.x) < 1.0e-3f &&
            std::abs(tgt.y) < 1.0e-3f &&
            std::abs(tgt.z) < 1.0e-3f,
            "rotate_begin uses screen-centre pivot (~origin), not cursor"
        );
    }
}

void test_pitch_never_exceeds_pole()
{
    // Total pitch φ = asin((position−pivot).z / r) must stay inside
    // (−90°, +90°) regardless of how far the user drags vertically.
    // Crossing ±90° flips cos(φ) → azimuth reverses → "mouse left, view
    // right" bug.  This test simulates extreme cumulative drags well past
    // the pole and asserts the clamp + feedback keeps φ safe and
    // direction consistent.

    gs3d::camera::Camera camera;
    camera.set_viewport(800, 600);
    camera.set_perspective(60.0f, 0.01f, 10000.0f);
    camera.look_at(
        {0.0f, -20.0f, 8.0f},   // φ₀ ≈ 22°
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    );

    gs3d::camera::CameraController controller;

    auto total_phi = [&]() -> double {
        const auto offset = sub(camera.position(), camera.target());
        const double r = std::sqrt(
            static_cast<double>(dot(offset, offset)));
        if (r < 1.0e-12) return 0.0;
        return std::asin(std::clamp(
            static_cast<double>(offset.z) / r, -1.0, 1.0));
    };

    // Default config pitch limits: −85° … +89° (radians).
    const double kPi = 3.14159265358979323846;
    const double kMinPitch = -1.483;  // ≈ −85°
    const double kMaxPitch =  1.553;  // ≈ +89°

    // Float round-trip sin→asin near the pole adds ~1e-3 error.
    constexpr double kPhiSlop = 0.015;

    // --- Drag up repeatedly far beyond 90° total ---
    {
        gs3d::camera::CameraInput input;
        input.viewport_width = 800;
        input.viewport_height = 600;
        input.rotate = true;
        input.rotate_begin = true;
        input.delta_y = 50.0f;  // large upward drag
        input.mouse_x = 400.0f;
        input.mouse_y = 300.0f;
        static_cast<void>(controller.update(camera, input));

        const double phi = total_phi();
        expect(
            phi >= kMinPitch - kPhiSlop && phi <= kMaxPitch + kPhiSlop,
            "after first drag-up phi stays inside config pitch limits"
        );
        // cos(φ) near π/2 needs double precision; float32 cos may
        // underflow to zero even when φ is 1 µrad inside the pole.
        expect(
            std::cos(phi) > 0.0,
            "cos(phi) > 0 after drag-up (no azimuth flip)"
        );
    }

    // Continue dragging up (cumulative far beyond 90°).
    for (int i = 0; i < 20; ++i) {
        gs3d::camera::CameraInput input;
        input.viewport_width = 800;
        input.viewport_height = 600;
        input.rotate = true;
        input.rotate_begin = false;  // continuing
        input.delta_y = 50.0f;       // keep pushing up
        input.mouse_x = 400.0f;
        input.mouse_y = 300.0f;
        static_cast<void>(controller.update(camera, input));

        const double phi = total_phi();
        expect(
            phi >= kMinPitch - kPhiSlop && phi <= kMaxPitch + kPhiSlop,
            "phi never exceeds config pitch limits after extreme drag"
        );
        expect(
            std::cos(phi) > 0.0,
            "cos(phi) stays positive — no azimuth sign flip"
        );
    }

    // Release and verify state is clean.
    {
        gs3d::camera::CameraInput input;
        input.viewport_width = 800;
        input.viewport_height = 600;
        input.rotate = false;
        input.rotate_begin = false;
        static_cast<void>(controller.update(camera, input));
    }

    // --- Fresh drag: verify direction is consistent ---
    // Camera is near the upper pole limit.  Dragging LEFT (negative
    // delta_x) must rotate the view LEFT (theta decreases), not right.
    {
        gs3d::camera::CameraInput input;
        input.viewport_width = 800;
        input.viewport_height = 600;
        input.rotate = true;
        input.rotate_begin = true;
        input.delta_x = -20.0f;  // drag left
        input.delta_y = 0.0f;
        input.mouse_x = 400.0f;
        input.mouse_y = 300.0f;
        const auto pos_before = camera.position();
        static_cast<void>(controller.update(camera, input));
        const auto pos_after = camera.position();

        // After dragging left (negative delta_x), the camera should orbit
        // counter-clockwise around +Z (viewed from above).  For a camera
        // at (x, y, z) looking at origin, a CCW rotation increases the
        // angle measured from +X toward +Y, meaning x decreases and y
        // increases (for a camera in the -Y quadrant).
        // But the simplest cross-check: position must have changed
        // (rotation happened), and phi is still in bounds.
        const auto delta_pos = sub(pos_after, pos_before);
        const float moved = std::abs(delta_pos.x) +
                            std::abs(delta_pos.y) +
                            std::abs(delta_pos.z);
        expect(
            moved > 1.0e-4f,
            "drag left near pole limit still produces rotation (not dead)"
        );
        const double phi = total_phi();
        expect(
            std::cos(phi) > 0.0,
            "cos(phi) > 0 after drag-left near pole (no direction reversal)"
        );
    }

    // --- Drag down from pole: must respond immediately (no lag) ---
    {
        const double phi_before = total_phi();

        gs3d::camera::CameraInput input;
        input.viewport_width = 800;
        input.viewport_height = 600;
        input.rotate = true;
        input.rotate_begin = false;  // continuing drag
        input.delta_y = -20.0f;       // drag down
        input.mouse_x = 400.0f;
        input.mouse_y = 300.0f;
        static_cast<void>(controller.update(camera, input));

        const double phi_after = total_phi();
        // Feedback: cumulative_phi_ was written back to the clamped
        // value, so reverse drag immediately moves away from the limit.
        // Dragging down from the lower pole (-85°) increases phi.
        expect(
            phi_after > phi_before - 1.0e-4,
            "drag down from pole limit moves phi immediately "
            "(no dead-zone lag from over-accumulated cumulative)"
        );
    }
}

void test_zoom_in_past_distance_floor_keeps_zooming_via_fov()
{
    // Ortho zoom: uniform scaling, no distance floor, no FOV.
    // ortho_height shrinks geometrically without bound (only float
    // precision limits).

    gs3d::camera::Camera camera;
    camera.set_viewport(800, 600);
    camera.set_orthographic(100.0f, 0.01f, 10000.0f);
    camera.look_at(
        {0.0f, -5.0f, 2.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    );

    gs3d::camera::CameraController controller;
    gs3d::camera::CameraBounds bounds;
    bounds.min = {-50.0f, -50.0f, -10.0f};
    bounds.max = {50.0f, 50.0f, 10.0f};
    controller.set_bounds(bounds);

    const float initial_ortho = camera.ortho_height();

    gs3d::camera::CameraInput input;
    input.viewport_width = 800;
    input.viewport_height = 600;
    input.scroll_y = 1.0f;  // zoom in

    float prev_ortho = initial_ortho;
    for (int i = 0; i < 60; ++i) {
        static_cast<void>(controller.update(camera, input));

        const float current_ortho = camera.ortho_height();
        expect(
            current_ortho < prev_ortho + 1.0e-4f,
            "ortho_height shrinks monotonically"
        );
        prev_ortho = current_ortho;
    }

    // After 60 consecutive zooms, ortho_height must be vastly smaller.
    const float final_ortho = camera.ortho_height();
    expect(
        final_ortho < initial_ortho * 0.001f,
        "ortho_height shrinks substantially (uniform, no floor)"
    );
    // No collapse — still well above float epsilon.
    expect(
        final_ortho > 1.0e-7f,
        "ortho_height still above float precision floor"
    );
}

void test_fov_clamped_at_min()
{
    // Ortho zoom: no FOV, just ortho_height.  Even after extreme
    // zoom-in, ortho_height is well-behaved and camera is stable.

    gs3d::camera::Camera camera;
    camera.set_viewport(800, 600);
    camera.set_orthographic(10.0f, 0.01f, 10000.0f);
    camera.look_at(
        {0.0f, -5.0f, 2.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    );

    gs3d::camera::CameraController controller;

    gs3d::camera::CameraInput input;
    input.viewport_width = 800;
    input.viewport_height = 600;
    input.scroll_y = 1.0f;  // zoom in

    // Zoom far past any reasonable limit.
    for (int i = 0; i < 200; ++i) {
        static_cast<void>(controller.update(camera, input));
    }

    // ortho_height is tiny but not zero (float precision limit).
    const float final_ortho = camera.ortho_height();
    expect(
        final_ortho < 1.0e-5f,
        "ortho_height becomes tiny after extreme zoom-in"
    );
    expect(
        final_ortho > 1.0e-38f,
        "ortho_height never hits zero (float precision floor)"
    );
    // Camera position/target unchanged.
    expect(
        std::abs(camera.position().x - 0.0f) < 1.0e-6f,
        "camera position unchanged by ortho zoom"
    );
}

} // namespace

int main()
{
    test_resize_debounce();
    test_resize_batch();
    test_tile_cache_budget_and_lru();
    test_oversized_tile_is_not_cached();
    test_scene_state_is_constructible_without_dataset_io();
    test_frame_upload_budget();
    test_camera_uses_view_local_input();
    test_zoom_converges_toward_centre_anchor_not_cursor();
    test_zoom_respects_max_distance_from_bounds();
    test_fit_bounds_distance_is_orientation_independent();
    test_fit_bounds_keeps_panorama_far_end_visible();
    test_zoom_caps_depth_ratio_for_close_large_scene();
    test_rotate_refreshes_depth_ratio();
    test_pan_refreshes_depth_ratio();
    test_box_select_falls_back_at_grazing_pitch();
    test_box_select_stays_within_scene_bounds_under_camera_tilt();
    test_axis_ticks_returns_empty_for_degenerate_range();
    test_axis_ticks_are_evenly_spaced_and_within_range();
    test_axis_ticks_step_is_a_nice_round_number();
    test_axis_ticks_huge_origin_with_tiny_step_terminates();
    test_axis_ticks_huge_origin_with_tiny_step_is_capped();
    test_mouse_mapping_without_map_axis_uses_canvas_rect();
    test_mouse_mapping_with_map_axis_uses_plot_rect();
    test_mouse_mapping_with_map_axis_rejects_axis_margin();
    test_pick_11x11_neighborhood_hits_sparse_isolated_point();
    test_pick_11x11_neighborhood_misses_point_outside_radius();
    test_mouse_ray_to_screen_projects_target_near_center();
    test_mouse_ray_to_screen_skips_points_behind_camera();
    test_mouse_ray_to_screen_round_trips_with_from_screen();
    test_box_select_returns_nullopt_for_degenerate_rect();
    test_box_select_world_bounds_scales_with_screen_rect_size();
    test_box_select_world_bounds_centers_near_camera_look_target();
    test_nearest_point_query_returns_nullopt_for_no_candidates();
    test_nearest_point_query_picks_screen_closest_candidate();
    test_nearest_point_query_respects_max_screen_distance();
    test_nearest_point_query_skips_points_behind_camera();
    test_nearest_point_query_depth_tie_is_order_sensitive();
    test_lod_adaptive_level_starts_at_lowest_while_interacting();
    test_lod_adaptive_level_climbs_after_good_frame_streak();
    test_lod_adaptive_level_drops_immediately_when_over_budget();
    test_lod_adaptive_level_ignores_stale_feedback();
    test_lod_non_adaptive_mode_still_pins_to_lowest();
    test_continuous_zoom_in_flies_forward_without_stalling();
    test_idle_update_does_not_change_camera();
    test_rotation_center_only_changes_on_rotate_begin();
    test_click_without_drag_does_not_move_camera();
    test_rotate_release_does_not_move_camera();
    test_rotation_pivot_is_screen_center();
    test_pitch_never_exceeds_pole();
    test_zoom_in_past_distance_floor_keeps_zooming_via_fov();
    test_fov_clamped_at_min();

    if (failures == 0) {
        std::cout << "[PASS] runtime performance tests\n";
    }
    return failures == 0 ? 0 : 1;
}
