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

void test_zoom_converges_toward_cursor_pivot_not_target()
{
    // Top-down camera looking at the origin from (0,0,100).
    gs3d::camera::Camera camera;
    camera.set_viewport(800, 600);
    camera.set_perspective(60.0f, 1.0f, 1000.0f);
    camera.look_at(
        {0.0f, 0.0f, 100.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f}
    );

    gs3d::camera::CameraController controller;

    // Mouse parked off-center (near the right edge, not screen center),
    // so its world pivot differs from the camera's current target (origin).
    gs3d::camera::CameraInput input;
    input.viewport_width = 800;
    input.viewport_height = 600;
    input.scroll_y = 1.0f;
    input.mouse_x = 700.0f;
    input.mouse_y = 300.0f;

    const auto target_before = camera.target();
    static_cast<void>(controller.update(camera, input));
    const auto target_after = camera.target();

    expect(
        target_after.x != target_before.x || target_after.y != target_before.y,
        "zooming toward an off-center cursor moves the orbit target "
        "(old behavior kept it pinned at the original target)"
    );

    // The target must move toward the cursor's world pivot (positive X,
    // since mouse_x=700 is right of center on a top-down view looking
    // along -Z with up=+Y), not in some unrelated direction.
    expect(
        target_after.x > target_before.x,
        "target re-centers toward the cursor side of the view, not away from it"
    );
}

void test_zoom_respects_max_distance_from_bounds()
{
    gs3d::camera::Camera camera;
    camera.set_viewport(800, 600);
    camera.set_perspective(60.0f, 1.0f, 1.0e6f);
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

    // Scroll out aggressively many times — distance must not run away
    // to an unbounded value once it's many multiples of the scene size.
    input.scroll_y = -1.0f;
    for (int i = 0; i < 200; ++i) {
        static_cast<void>(controller.update(camera, input));
    }

    const float scene_diagonal = std::sqrt(20.0f * 20.0f * 3.0f);
    expect(
        camera.distance() <= scene_diagonal * 50.0f + 1.0f,
        "zooming out repeatedly is clamped to a bounded multiple of the "
        "scene diagonal, not left to grow without limit"
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

} // namespace

int main()
{
    test_resize_debounce();
    test_resize_batch();
    test_tile_cache_budget_and_lru();
    test_oversized_tile_is_not_cached();
    test_frame_upload_budget();
    test_camera_uses_view_local_input();
    test_zoom_converges_toward_cursor_pivot_not_target();
    test_zoom_respects_max_distance_from_bounds();
    test_fit_bounds_distance_is_orientation_independent();
    test_box_select_falls_back_at_grazing_pitch();
    test_box_select_stays_within_scene_bounds_under_camera_tilt();
    test_axis_ticks_returns_empty_for_degenerate_range();
    test_axis_ticks_are_evenly_spaced_and_within_range();
    test_axis_ticks_step_is_a_nice_round_number();
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
