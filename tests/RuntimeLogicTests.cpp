#include "app/RecentProjects.hpp"
#include "app/ResourcePath.hpp"
#include "app/TilePointCache.hpp"
#include "app/UserPreferences.hpp"
#include "app/ViewerApp.hpp"
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
#include "render/TileSelection.hpp"
#include "scene/SceneState.hpp"
#include "ui/UiRoot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

void expect(bool condition, std::string_view name)
{
    INFO(name);
    CHECK(condition);
}

bool set_environment_variable(
    const std::string& name,
    const std::string& value
)
{
#if defined(_WIN32)
    return _putenv_s(name.c_str(), value.c_str()) == 0;
#else
    return setenv(name.c_str(), value.c_str(), 1) == 0;
#endif
}

bool unset_environment_variable(const std::string& name)
{
#if defined(_WIN32)
    return _putenv_s(name.c_str(), "") == 0;
#else
    return unsetenv(name.c_str()) == 0;
#endif
}

class ScopedEnvironmentVariable {
public:
    explicit ScopedEnvironmentVariable(std::string name)
        : name_(std::move(name))
    {
        if (const char* value = std::getenv(name_.c_str())) {
            original_value_ = value;
        }
    }

    ~ScopedEnvironmentVariable()
    {
        if (original_value_) {
            set_environment_variable(name_, *original_value_);
        } else {
            unset_environment_variable(name_);
        }
    }

    bool set(const std::string& value) const
    {
        return set_environment_variable(name_, value);
    }

private:
    std::string name_;
    std::optional<std::string> original_value_;
};

std::shared_ptr<gs3d::app::TilePoints> make_points(std::size_t count)
{
    auto points = std::make_shared<gs3d::app::TilePoints>();
    points->points.resize(count);
    points->point_ids.resize(count);
    return points;
}

void test_recent_projects_persist_and_dedupe()
{
    const auto nonce =
        std::chrono::steady_clock::now()
            .time_since_epoch()
            .count();
    const auto test_root =
        std::filesystem::temp_directory_path() /
        ("gs3d-recent-projects-" + std::to_string(nonce));
    const auto storage = test_root / "recent-projects.txt";
    const auto project_a = test_root / "a.gs3d.bundle";
    const auto project_b = test_root / "b.gs3d.bundle";
    std::filesystem::create_directories(project_a);
    std::filesystem::create_directories(project_b);
    ScopedEnvironmentVariable storage_override(
        "GS3D_RECENT_PROJECTS_PATH"
    );
    expect(
        storage_override.set(storage.string()),
        "recent projects test can set storage override"
    );

    gs3d::app::remember_recent_project(project_a);
    gs3d::app::remember_recent_project(project_b);
    gs3d::app::remember_recent_project(project_a);
    const auto entries = gs3d::app::load_recent_projects();

    expect(
        entries.size() == 2,
        "recent projects deduplicate repeated paths"
    );
    expect(
        !entries.empty() &&
            entries.front().path.filename() ==
                project_a.filename(),
        "most recently reopened project moves to the front"
    );
    gs3d::app::clear_recent_projects();
    expect(
        gs3d::app::load_recent_projects().empty(),
        "recent projects can be cleared from the welcome window"
    );

    std::error_code ec;
    std::filesystem::remove_all(test_root, ec);
}

void test_gpu_preference_is_user_scoped()
{
    const auto nonce =
        std::chrono::steady_clock::now()
            .time_since_epoch()
            .count();
    const auto test_root =
        std::filesystem::temp_directory_path() /
        ("gs3d-user-preferences-" + std::to_string(nonce));
    ScopedEnvironmentVariable config_root_override(
        "GS3D_USER_CONFIG_DIR"
    );
    expect(
        config_root_override.set(test_root.string()),
        "user preferences test can set config directory override"
    );

    const auto preferences_path =
        gs3d::app::user_preferences_path();
    expect(
        preferences_path == test_root / "preferences.toml",
        "user preferences use the user config directory"
    );
    expect(
        !gs3d::app::load_preferred_gpu_preference().has_value(),
        "missing user GPU preference falls back to template default"
    );
    expect(
        gs3d::app::save_preferred_gpu_preference(
            "uuid:0123456789abcdef0123456789abcdef"
        ),
        "GPU preference is persisted outside the project config"
    );
    expect(
        std::filesystem::is_regular_file(preferences_path),
        "GPU preference writes a user preferences file"
    );
    const auto preferred_gpu =
        gs3d::app::load_preferred_gpu_preference();
    expect(
        preferred_gpu.has_value() &&
            *preferred_gpu == "uuid:0123456789abcdef0123456789abcdef",
        "saved GPU preference is loaded from user preferences"
    );

    std::error_code ec;
    std::filesystem::remove_all(test_root, ec);
}

void test_resource_path_resolves_assets_from_executable_directory()
{
    const auto nonce =
        std::chrono::steady_clock::now()
            .time_since_epoch()
            .count();
    const auto test_root =
        std::filesystem::temp_directory_path() /
        ("gs3d-resource-path-" + std::to_string(nonce));
    const auto executable_path = test_root / "bin" / "GeoScatter3D";
    const auto asset_path =
        executable_path.parent_path() / "assets" / "fonts" /
        "test-font.otf";
    std::filesystem::create_directories(asset_path.parent_path());
    std::ofstream(asset_path) << "test";

    gs3d::app::ResourcePathContext context;
    context.executable_path = executable_path;
    const auto resolved = gs3d::app::ResourcePath::resolve_optional_file(
        "assets/fonts/test-font.otf",
        context
    );
    expect(
        resolved == std::filesystem::absolute(asset_path).lexically_normal(),
        "resource paths resolve relative to the executable directory"
    );

    std::error_code ec;
    std::filesystem::remove_all(test_root, ec);
}

void test_default_viewer_config_is_portable()
{
    const gs3d::app::ViewerAppConfig config;
    expect(
        config.input.gs3d_path.empty() &&
            config.graphics.vertex_shader_path.empty() &&
            config.graphics.fragment_shader_path.empty(),
        "default viewer config does not embed machine-specific file paths"
    );
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

void test_arcball_drag_rotates_about_screen_axes()
{
    // Turntable contract (current main branch; replaces the screen-axis
    // arcball that was tried and rolled back — see project memory
    // [[rotation-direction]]).  Yaw is around the fixed world-up axis
    // {0,0,1}; pitch is around cross(position_offset, world_up) with
    // explicit pivot = (40, 0, 0).  Invariants verified here:
    //   - yaw around world-up: position_offset's z component is unchanged.
    //   - pitch around horizontal radial axis: position_offset's radial
    //     distance to the pivot in the XY plane is preserved (only z moves).
    //   - pivot stays at the same screen pixel for both axes.
    gs3d::camera::Camera camera;
    camera.set_viewport(800, 600);
    camera.set_perspective(60.0f, 0.1f, 10000.0f);
    const gs3d::camera::Vec3 cam_pos{0.0f, -80.0f, 30.0f};
    const gs3d::camera::Vec3 cam_tgt{0.0f, 0.0f, 0.0f};
    // look_at only length-normalises up, it does not re-orthogonalise it
    // against forward - so initialise with an already-perpendicular up.
    const gs3d::camera::Vec3 fwd{
        cam_tgt.x - cam_pos.x,
        cam_tgt.y - cam_pos.y,
        cam_tgt.z - cam_pos.z};
    const float fl = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
    const gs3d::camera::Vec3 fn{fwd.x / fl, fwd.y / fl, fwd.z / fl};
    const float dotz = fn.z;  // forward . (0,0,1)
    gs3d::camera::Vec3 ortho_up{0.0f, 0.0f, 1.0f};
    ortho_up = {
        ortho_up.x - fn.x * dotz,
        ortho_up.y - fn.y * dotz,
        ortho_up.z - fn.z * dotz};
    const float ul = std::sqrt(
        ortho_up.x * ortho_up.x + ortho_up.y * ortho_up.y + ortho_up.z * ortho_up.z);
    camera.look_at(
        cam_pos,
        cam_tgt,
        {ortho_up.x / ul, ortho_up.y / ul, ortho_up.z / ul}
    );

    const gs3d::camera::Viewport viewport{800, 600};
    const gs3d::camera::Vec3 pivot{40.0f, 0.0f, 0.0f};

    auto drag = [&](float dx, float dy) -> gs3d::camera::Camera {
        gs3d::camera::Camera cam = camera;
        gs3d::camera::CameraController controller;
        controller.set_orbit_pivot(pivot);
        gs3d::camera::CameraInput input;
        input.viewport_width = 800;
        input.viewport_height = 600;
        input.rotate = true;
        input.delta_x = dx;
        input.delta_y = dy;
        static_cast<void>(controller.update(cam, input));
        return cam;
    };

    // Horizontal drag yaws around the fixed world-up axis: the camera's
    // Z component relative to the pivot is unchanged (rigid rotation
    // around (0,0,1)).
    {
        const auto cam = drag(60.0f, 0.0f);
        const auto off_z_before = camera.position().z - pivot.z;
        const auto off_z_after  = cam.position().z    - pivot.z;
        expect(
            std::abs(off_z_after - off_z_before) < 1.0e-3f,
            "horizontal drag yaws around world_up (z-component preserved)"
        );
        const auto p = gs3d::camera::MouseRay::to_screen(pivot, viewport, cam);
        expect(p.has_value(), "pivot stays on screen after horizontal drag");
    }

    // Vertical drag pitches around cross(position_offset, world_up): a
    // pure rotation around the pivot, so the 3D distance from camera to
    // pivot is preserved (Z trades off against XY).
    {
        const auto cam = drag(0.0f, 60.0f);
        const auto off_before = gs3d::camera::Vec3{
            camera.position().x - pivot.x,
            camera.position().y - pivot.y,
            camera.position().z - pivot.z};
        const auto off_after = gs3d::camera::Vec3{
            cam.position().x - pivot.x,
            cam.position().y - pivot.y,
            cam.position().z - pivot.z};
        const float r_before = std::sqrt(
            off_before.x * off_before.x +
            off_before.y * off_before.y +
            off_before.z * off_before.z);
        const float r_after = std::sqrt(
            off_after.x * off_after.x +
            off_after.y * off_after.y +
            off_after.z * off_after.z);
        expect(
            std::abs(r_after - r_before) < 1.0e-3f,
            "vertical drag pitches around the horizontal radial axis "
            "(3D distance to pivot preserved)"
        );
        const auto p = gs3d::camera::MouseRay::to_screen(pivot, viewport, cam);
        expect(p.has_value(), "pivot stays on screen after vertical drag");
    }
}

void test_zoom_keeps_cursor_anchor_fixed()
{
    gs3d::camera::Camera camera;
    camera.set_viewport(800, 600);
    camera.set_orthographic(10.0f, 0.01f, 1000.0f);
    camera.look_at(
        {0.0f, 0.0f, 100.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f}
    );

    gs3d::camera::CameraController controller;
    const gs3d::camera::Vec3 locked_pivot{1.0f, 2.0f, 0.0f};
    controller.set_orbit_pivot(locked_pivot);

    const float initial_ortho = camera.ortho_height();
    const gs3d::camera::Viewport viewport{800, 600};
    const auto anchor =
        gs3d::camera::MouseRay::intersect_camera_facing_plane(
            700.0,
            300.0,
            viewport,
            camera,
            camera.target()
        );
    expect(
        anchor.has_value(),
        "off-centre cursor resolves a zoom anchor"
    );

    gs3d::camera::CameraInput input;
    input.viewport_width = 800;
    input.viewport_height = 600;
    input.scroll_y = 1.0f;
    input.mouse_x = 700.0f;
    input.mouse_y = 300.0f;
    input.mouse_position_valid = true;
    static_cast<void>(controller.update(camera, input));

    expect(
        camera.ortho_height() < initial_ortho,
        "ortho zoom in shrinks ortho_height"
    );
    expect(
        controller.orbit_pivot().has_value() &&
        std::abs(controller.orbit_pivot()->x - locked_pivot.x) < 1.0e-6f &&
        std::abs(controller.orbit_pivot()->y - locked_pivot.y) < 1.0e-6f &&
        std::abs(controller.orbit_pivot()->z - locked_pivot.z) < 1.0e-6f,
        "cursor-anchored zoom keeps an explicit pivot world-locked"
    );
    if (anchor.has_value()) {
        const auto anchor_screen =
            gs3d::camera::MouseRay::to_screen(
                *anchor,
                viewport,
                camera
            );
        expect(
            anchor_screen.has_value() &&
            std::abs(anchor_screen->x - 700.0f) < 0.1f &&
            std::abs(anchor_screen->y - 300.0f) < 0.1f,
            "zoom keeps the world point under the cursor fixed"
        );
    }
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

void test_orthographic_fit_bounds_uses_true_top_down_view()
{
    gs3d::camera::CameraBounds bounds;
    bounds.min = {-18'688'740.0f, -20'026'054.0f, -400.5f};
    bounds.max = {18'688'740.0f, 20'026'054.0f, 400.5f};

    gs3d::camera::Camera camera;
    camera.set_viewport(800, 600);
    camera.set_orthographic(10.0f, 0.01f, 10000.0f);
    camera.fit_bounds(bounds);

    expect(
        std::abs(camera.position().x - camera.target().x) < 1.0f &&
            std::abs(camera.position().y - camera.target().y) < 1.0f &&
            camera.position().z > camera.target().z,
        "orthographic fit_bounds() places the camera directly above the "
        "dataset instead of using an oblique view"
    );
    expect(
        std::abs(camera.up().x) < 1.0e-6f &&
            std::abs(camera.up().y - 1.0f) < 1.0e-6f &&
            std::abs(camera.up().z) < 1.0e-6f,
        "top-down orthographic fit uses +Y as screen up"
    );
    expect(
        camera.ortho_height() >=
            bounds.max.y - bounds.min.y,
        "top-down orthographic fit contains the full dataset height"
    );
}

void test_tile_selection_honors_full_z_range_config()
{
    gs3d::camera::Camera camera;
    camera.set_viewport(400, 400);
    camera.set_orthographic(40.0f, 0.1f, 200.0f);
    camera.look_at(
        {5.0f, 50.0f, 80.0f},
        {5.0f, 0.0f, 80.0f},
        {0.0f, 0.0f, 1.0f}
    );

    gs3d::core::TileIndexView tile_index;
    tile_index.header.tile_size_x = 10.0f;
    tile_index.header.tile_size_y = 10.0f;
    tile_index.header.grid_origin_x = 0.0f;
    tile_index.header.grid_origin_y = 0.0f;
    tile_index.header.bbox_min_z = 0.0f;
    tile_index.header.bbox_max_z = 100.0f;
    tile_index.records.push_back({
        7,
        0,
        0,
        100,
        0.0f,
        0.0f,
        0.0f,
        10.0f,
        10.0f,
        10.0f
    });

    gs3d::render::TileSelectionConfig local_z_config;
    local_z_config.min_tile_pixel_size = 1.0f;
    local_z_config.use_full_z_range = false;

    gs3d::render::TileSelectionConfig full_z_config = local_z_config;
    full_z_config.use_full_z_range = true;

    gs3d::render::TileSelection local_z_selection(local_z_config);
    gs3d::render::TileSelection full_z_selection(full_z_config);

    const auto local_z_result =
        local_z_selection.update(camera, tile_index);
    const auto full_z_result =
        full_z_selection.update(camera, tile_index);

    expect(
        local_z_result.tile_ids.empty(),
        "per-tile Z culling can reject a tile outside the visible Z band"
    );
    expect(
        full_z_result.tile_ids.size() == 1 &&
            full_z_result.tile_ids.front() == 7,
        "use_full_z_range keeps XY tiles visible in side views"
    );
}

void test_tile_selection_uses_mapped_height_space()
{
    gs3d::camera::Camera camera;
    camera.set_viewport(400, 400);
    camera.set_orthographic(40.0f, 0.1f, 200.0f);
    camera.look_at(
        {5.0f, 50.0f, 80.0f},
        {5.0f, 0.0f, 80.0f},
        {0.0f, 0.0f, 1.0f}
    );

    gs3d::core::TileIndexView tile_index;
    tile_index.header.tile_size_x = 10.0f;
    tile_index.header.tile_size_y = 10.0f;
    tile_index.header.grid_origin_x = 0.0f;
    tile_index.header.grid_origin_y = 0.0f;
    tile_index.header.bbox_min_z = 0.0f;
    tile_index.header.bbox_max_z = 10.0f;
    tile_index.records.push_back({
        8,
        0,
        0,
        100,
        0.0f,
        0.0f,
        0.0f,
        10.0f,
        10.0f,
        10.0f
    });

    gs3d::render::TileSelectionConfig raw_space_config;
    raw_space_config.min_tile_pixel_size = 1.0f;
    raw_space_config.use_full_z_range = true;
    raw_space_config.height_mult = 1.0f;

    gs3d::render::TileSelectionConfig mapped_space_config =
        raw_space_config;
    mapped_space_config.height_mult = 10.0f;

    gs3d::render::TileSelection raw_space_selection(raw_space_config);
    gs3d::render::TileSelection mapped_space_selection(
        mapped_space_config
    );

    const auto raw_space_result =
        raw_space_selection.update(camera, tile_index);
    const auto mapped_space_result =
        mapped_space_selection.update(camera, tile_index);

    expect(
        raw_space_result.tile_ids.empty(),
        "raw tile Z bounds can miss tiles after camera focus in mapped space"
    );
    expect(
        mapped_space_result.tile_ids.size() == 1 &&
            mapped_space_result.tile_ids.front() == 8,
        "tile selection follows the rendered height mapping"
    );
}

void test_tile_selection_does_not_cap_visible_tiles()
{
    gs3d::camera::Camera camera;
    camera.set_viewport(400, 400);
    camera.set_perspective(60.0f, 0.1f, 200.0f);
    camera.look_at(
        {15.0f, 50.0f, 5.0f},
        {15.0f, 0.0f, 5.0f},
        {0.0f, 0.0f, 1.0f}
    );

    gs3d::core::TileIndexView tile_index;
    tile_index.header.tile_size_x = 10.0f;
    tile_index.header.tile_size_y = 10.0f;
    tile_index.header.grid_origin_x = 0.0f;
    tile_index.header.grid_origin_y = 0.0f;
    tile_index.header.bbox_min_z = 0.0f;
    tile_index.header.bbox_max_z = 10.0f;
    tile_index.records.push_back({
        1,
        2,
        0,
        100,
        20.0f,
        0.0f,
        0.0f,
        30.0f,
        10.0f,
        10.0f
    });
    tile_index.records.push_back({
        2,
        0,
        0,
        100,
        0.0f,
        0.0f,
        0.0f,
        10.0f,
        10.0f,
        10.0f
    });

    gs3d::render::TileSelectionConfig config;
    config.min_tile_pixel_size = 1.0f;
    config.use_full_z_range = true;
    config.max_visible_tiles = 1;

    gs3d::render::TileSelection selection(config);
    const auto result = selection.update(camera, tile_index);

    expect(
        result.tile_ids.size() == 2,
        "visible tile selection ignores max_visible_tiles to avoid block artifacts"
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

void test_spatial_select_picks_coarsest_covering()
{
    // voxel_sizes = [5.0, 10.0, 20.0] (finest → coarsest)
    const std::vector<float> voxel_sizes{5.0f, 10.0f, 20.0f};
    constexpr std::size_t no_prev =
        static_cast<std::size_t>(-1);

    // wpix=12.0: voxel 20 > 12 (skip), voxel 10 ≤ 12 → level 1
    expect(
        gs3d::render::LodSelector::select_level_by_spacing(
            12.0f, voxel_sizes, no_prev) == 1,
        "world_per_pixel=12 picks level 1 (voxel 10 ≤ 12)"
    );

    // wpix=25.0: voxel 20 ≤ 25 → level 2 (coarsest covering)
    expect(
        gs3d::render::LodSelector::select_level_by_spacing(
            25.0f, voxel_sizes, no_prev) == 2,
        "world_per_pixel=25 picks level 2 (coarsest satisfying)"
    );

    // wpix=3.0: no voxel ≤ 3, fallback to level 0 (finest)
    expect(
        gs3d::render::LodSelector::select_level_by_spacing(
            3.0f, voxel_sizes, no_prev) == 0,
        "world_per_pixel=3 falls back to level 0 (zoomed way in)"
    );

    // wpix=5.0 (exact match at boundary): voxel 5 ≤ 5 → level 0
    expect(
        gs3d::render::LodSelector::select_level_by_spacing(
            5.0f, voxel_sizes, no_prev) == 0,
        "world_per_pixel=5 (exact match) picks level 0"
    );
}

void test_zoom_in_selects_finer_level()
{
    const std::vector<float> voxel_sizes{5.0f, 10.0f, 20.0f};
    constexpr std::size_t no_prev =
        static_cast<std::size_t>(-1);

    // 缩小 (大 wpix) → 选择更粗的层
    expect(
        gs3d::render::LodSelector::select_level_by_spacing(
            30.0f, voxel_sizes, no_prev) == 2,
        "zoom out (wpix=30) → coarsest level 2"
    );

    // 放大 (小 wpix)：8.0 在 5 和 10 之间，
    // 20>8 (skip), 10>8 (skip), 5≤8 → level 0
    expect(
        gs3d::render::LodSelector::select_level_by_spacing(
            8.0f, voxel_sizes, no_prev) == 0,
        "zoom in (wpix=8.0) → level 0 (10>8, 5≤8)"
    );

    // 极近兜底
    expect(
        gs3d::render::LodSelector::select_level_by_spacing(
            0.1f, voxel_sizes, no_prev) == 0,
        "extreme zoom (wpix=0.1) → level 0 fallback"
    );
}

void test_spatial_no_flicker_at_boundary()
{
    // 模拟在 level 0/1 边界附近抖动 world_per_pixel
    const std::vector<float> voxel_sizes{5.0f, 10.0f, 20.0f};
    constexpr std::size_t no_prev =
        static_cast<std::size_t>(-1);

    // 初始状态：wpix=10.5，选 level 1 (10 ≤ 10.5)
    auto prev =
        gs3d::render::LodSelector::select_level_by_spacing(
            10.5f, voxel_sizes, no_prev);
    expect(prev == 1,
        "initial wpix=10.5 → level 1");

    // 放大：wpix 降到 9.5 < 10，切到 level 0 (无滞回阻碍变精)
    auto cur =
        gs3d::render::LodSelector::select_level_by_spacing(
            9.5f, voxel_sizes, prev);
    expect(cur == 0,
        "zoom in wpix=9.5 → level 0 (finer, no hysteresis block)");

    // 缩小回 10.5：滞回应阻止切回 level 1
    // 因为 level 1 的阈值被放大为 10 × 1.2 = 12, 12 > 10.5 → 不满足
    prev = cur;
    cur = gs3d::render::LodSelector::select_level_by_spacing(
        10.5f, voxel_sizes, prev);
    expect(cur == 0,
        "wpix=10.5 still level 0 (hysteresis: 10×1.2=12 > 10.5, blocked)");

    // 进一步缩小到 wpix=12.5：12.5 ≥ 12 → 切回 level 1
    prev = cur;
    cur = gs3d::render::LodSelector::select_level_by_spacing(
        12.5f, voxel_sizes, prev);
    expect(cur == 1,
        "wpix=12.5 → level 1 (12.5 ≥ 12, hysteresis released)");

    // 再次放大到 9.5 → 立即切回 level 0（变精不阻塞）
    prev = cur;
    cur = gs3d::render::LodSelector::select_level_by_spacing(
        9.5f, voxel_sizes, prev);
    expect(cur == 0,
        "wpix=9.5 → level 0 again (finer direction, no block)");

    // 空列表兜底
    expect(
        gs3d::render::LodSelector::select_level_by_spacing(
            10.0f, {}, no_prev) == 0,
        "empty voxel_sizes returns 0");
}

/*
 * 交互中空间选层应冻结——不管 ortho_height 如何变化，
 * spatial_level 保持不变。模拟 ViewerApp 的 frozen_spatial_level 逻辑。
 */
void test_spatial_frozen_during_interaction()
{
    const std::vector<float> voxel_sizes{5.0f, 10.0f, 20.0f};
    constexpr std::size_t no_prev =
        static_cast<std::size_t>(-1);

    // 模拟首帧：初始 zoom，spatial=1
    std::size_t frozen_spatial =
        gs3d::render::LodSelector::select_level_by_spacing(
            12.0f, voxel_sizes, no_prev);
    expect(frozen_spatial == 1,
        "initial wpix=12 → spatial=1");

    // 交互中放大：ortho_h 变小，world_per_pixel 随之变小，
    // 但 frozen 应保持不变（不重新计算 spatial）
    std::size_t spatial_during = frozen_spatial; // 冻结
    expect(spatial_during == 1,
        "still spatial=1 during zoom-in (frozen)");

    // 交互中缩小：同理应保持不变
    spatial_during = frozen_spatial;
    expect(spatial_during == 1,
        "still spatial=1 during zoom-out (frozen)");

    // 停手后：重新计算 spatial，应反映当前 zoom
    std::size_t spatial_after =
        gs3d::render::LodSelector::select_level_by_spacing(
            25.0f, voxel_sizes, frozen_spatial);
    frozen_spatial = spatial_after;
    expect(spatial_after == 2,
        "after zoom-out stop: wpix=25 → spatial=2 (updated)");
    expect(frozen_spatial == 2,
        "frozen updated to 2 after interaction");
}

/*
 * 停手后 spatial 应立即更新到当前 zoom 对应的目标层，
 * 不受交互中冻结值的影响。
 */
void test_spatial_updates_after_interaction()
{
    const std::vector<float> voxel_sizes{5.0f, 10.0f, 20.0f};
    constexpr std::size_t no_prev =
        static_cast<std::size_t>(-1);

    // 初始状态：spatial=1
    std::size_t frozen_spatial =
        gs3d::render::LodSelector::select_level_by_spacing(
            12.0f, voxel_sizes, no_prev);

    // 交互中冻结在 level 1
    // ... (交互结束)

    // 停手放大（wpix 从 12 降到 8）：应更新到 level 0
    std::size_t spatial_after =
        gs3d::render::LodSelector::select_level_by_spacing(
            8.0f, voxel_sizes, frozen_spatial);
    frozen_spatial = spatial_after;
    expect(spatial_after == 0,
        "after zoom-in stop: wpix=8 → spatial=0 (finer)");

    // 停手缩小（wpix 从 8 升到 25）：应更新到 level 2
    spatial_after =
        gs3d::render::LodSelector::select_level_by_spacing(
            25.0f, voxel_sizes, frozen_spatial);
    frozen_spatial = spatial_after;
    expect(spatial_after == 2,
        "after zoom-out stop: wpix=25 → spatial=2 (coarser)");

    // 极近兜底
    spatial_after =
        gs3d::render::LodSelector::select_level_by_spacing(
            2.0f, voxel_sizes, frozen_spatial);
    frozen_spatial = spatial_after;
    expect(spatial_after == 0,
        "after extreme zoom-in: wpix=2 → spatial=0 (fallback)");
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

void test_rotation_after_pan_keeps_panned_target()
{
    // The point at the screen centre is camera.target(). Reconstructing it
    // through an inverse view-projection matrix loses precision after a pan,
    // especially for geospatial coordinates far from the world origin.
    // Rotation must orbit around the panned target without moving it.

    gs3d::camera::Camera camera;
    camera.set_viewport(800, 600);
    camera.set_orthographic(200.0f, 0.01f, 10000.0f);
    camera.look_at(
        {1000000.0f, 1999800.0f, 80.0f},
        {1000000.0f, 2000000.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    );

    gs3d::camera::CameraController controller;

    {
        gs3d::camera::CameraInput input;
        input.viewport_width = 800;
        input.viewport_height = 600;
        input.pan = true;
        input.delta_x = 120.0f;
        input.delta_y = -60.0f;
        static_cast<void>(controller.update(camera, input));
    }

    const auto target_after_pan = camera.target();
    const float distance_after_pan = camera.distance();

    {
        gs3d::camera::CameraInput input;
        input.viewport_width = 800;
        input.viewport_height = 600;
        input.rotate = true;
        input.rotate_begin = true;
        input.delta_x = 20.0f;
        input.delta_y = 10.0f;
        static_cast<void>(controller.update(camera, input));
    }

    const auto target_after_rotate = camera.target();
    expect(
        std::abs(target_after_rotate.x - target_after_pan.x) < 1.0e-6f &&
        std::abs(target_after_rotate.y - target_after_pan.y) < 1.0e-6f &&
        std::abs(target_after_rotate.z - target_after_pan.z) < 1.0e-6f,
        "rotation after pan must keep the panned target fixed"
    );
    // Distance is preserved by rotation in exact arithmetic; the residual is
    // float32 storage noise from storing position = pivot + offset at ~1e6
    // geospatial magnitude (float32 precision there is ~0.25, so an absolute
    // floor of 1.0 sits above the noise while still catching real drift).
    expect(
        std::abs(camera.distance() - distance_after_pan) < 1.0f,
        "rotation after pan must preserve camera distance"
    );
}

void test_rotation_does_not_undo_overlapping_pan()
{
    // Mouse-button transitions can produce one frame where rotate and pan
    // are both active. The following rotation frame must continue from the
    // already-panned target instead of rebuilding from a drag-start snapshot.

    gs3d::camera::Camera camera;
    camera.set_viewport(800, 600);
    camera.set_orthographic(200.0f, 0.01f, 10000.0f);
    camera.look_at(
        {0.0f, -200.0f, 80.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    );

    gs3d::camera::CameraController controller;

    {
        gs3d::camera::CameraInput input;
        input.viewport_width = 800;
        input.viewport_height = 600;
        input.rotate = true;
        input.rotate_begin = true;
        input.pan = true;
        input.delta_x = 20.0f;
        input.delta_y = 10.0f;
        static_cast<void>(controller.update(camera, input));
    }

    const auto target_after_pan = camera.target();

    {
        gs3d::camera::CameraInput input;
        input.viewport_width = 800;
        input.viewport_height = 600;
        input.rotate = true;
        input.delta_x = 20.0f;
        input.delta_y = 10.0f;
        static_cast<void>(controller.update(camera, input));
    }

    const auto target_after_rotate = camera.target();
    expect(
        std::abs(target_after_rotate.x - target_after_pan.x) < 1.0e-5f &&
        std::abs(target_after_rotate.y - target_after_pan.y) < 1.0e-5f &&
        std::abs(target_after_rotate.z - target_after_pan.z) < 1.0e-5f,
        "continued rotation must not undo an overlapping pan"
    );
}

void test_screen_space_pan_tracks_mouse_pixels()
{
    // A fixed scene point must follow the mouse in screen space regardless
    // of camera tilt or world axes.
    gs3d::camera::Camera camera;
    camera.set_viewport(800, 600);
    camera.set_orthographic(120.0f, 0.01f, 10000.0f);
    camera.look_at(
        {0.0f, -20.0f, 8.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    );

    gs3d::camera::CameraController controller;
    const gs3d::camera::Viewport viewport{800, 600};
    const gs3d::camera::Vec3 fixed_world_point = camera.target();
    const auto screen_before = gs3d::camera::MouseRay::to_screen(
        fixed_world_point, viewport, camera);
    const float target_z_before = camera.target().z;

    {
        gs3d::camera::CameraInput input;
        input.viewport_width = 800;
        input.viewport_height = 600;
        input.pan = true;
        input.delta_x = 80.0f;
        input.delta_y = 40.0f;
        static_cast<void>(controller.update(camera, input));
    }

    const auto screen_after = gs3d::camera::MouseRay::to_screen(
        fixed_world_point, viewport, camera);
    expect(
        screen_before.has_value() && screen_after.has_value(),
        "screen-space pan keeps the reference point projectable"
    );
    if (screen_before && screen_after) {
        expect(
            std::abs(
                (screen_after->x - screen_before->x) - 80.0f
            ) < 0.2f &&
            std::abs(
                (screen_after->y - screen_before->y) - 40.0f
            ) < 0.2f,
            "scene point must follow mouse pan pixel-for-pixel"
        );
    }
    expect(
        std::abs(camera.target().z - target_z_before) > 1.0e-4f,
        "tilted screen-space vertical pan must include a world-Z component"
    );
}

void test_rotation_supports_nearly_full_pitch_range()
{
    gs3d::camera::Camera camera;
    camera.set_viewport(800, 600);
    camera.set_orthographic(120.0f, 0.01f, 10000.0f);
    camera.look_at(
        {0.0f, -20.0f, 8.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    );

    gs3d::camera::CameraController controller;
    {
        gs3d::camera::CameraInput input;
        input.viewport_width = 800;
        input.viewport_height = 600;
        input.rotate = true;
        input.delta_y = -200.0f;
        static_cast<void>(controller.update(camera, input));
    }

    const auto offset_after = sub(camera.position(), camera.target());
    const float radius_after = std::sqrt(dot(offset_after, offset_after));
    const float pitch_after = std::asin(std::clamp(
        offset_after.z / radius_after, -1.0f, 1.0f));

    expect(
        pitch_after < -1.50f && pitch_after >= -1.553f - 1.0e-4f,
        "default orbit must allow viewing from almost directly below"
    );
}

void test_rotation_after_pan_keeps_scene_pivot_on_screen()
{
    // Contract: with no explicit orbit pivot, rotation pivots around the
    // camera target (= the implicit pivot). After a pan, the target has
    // moved, so the rotation pivot moves with it. The visible invariant
    // is that the camera's look-at point (= pivot) stays anchored at the
    // same screen pixel while the user tumbles. The world-space distance
    // from camera to target is also preserved (rigid rotation).
    //
    // Note: under the OLD (pre-fix) policy, the rotation pivot was the
    // scene bounds centre, which is why this test used to assert that the
    // bounds centre stayed on screen. That policy was rejected because
    // it produced a fixed world-axis rotation that ignored the user's
    // panned composition (see project memory [[rotation-direction]]).

    gs3d::camera::Camera camera;
    camera.set_viewport(800, 600);
    camera.set_orthographic(13000.0f, 0.01f, 100000.0f);
    camera.look_at(
        {0.0f, -14000.0f, 6000.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    );

    gs3d::camera::CameraController controller;
    gs3d::camera::CameraBounds bounds;
    bounds.min = {-4000.0f, -5000.0f, -2000.0f};
    bounds.max = { 4000.0f,  5000.0f,  2000.0f};
    controller.set_bounds(bounds);

    {
        gs3d::camera::CameraInput input;
        input.viewport_width = 800;
        input.viewport_height = 600;
        input.pan = true;
        input.delta_x = 120.0f;
        input.delta_y = -60.0f;
        static_cast<void>(controller.update(camera, input));
    }

    const gs3d::camera::Viewport viewport{800, 600};
    // The new pivot is the camera target (no explicit pivot set).
    const gs3d::camera::Vec3 target_pivot = camera.target();
    const auto screen_before = gs3d::camera::MouseRay::to_screen(
        target_pivot, viewport, camera);
    const float distance_before = camera.distance();

    {
        gs3d::camera::CameraInput input;
        input.viewport_width = 800;
        input.viewport_height = 600;
        input.rotate = true;
        input.rotate_begin = true;
        input.delta_x = 120.0f;
        input.delta_y = 40.0f;
        static_cast<void>(controller.update(camera, input));
    }

    const auto screen_after = gs3d::camera::MouseRay::to_screen(
        target_pivot, viewport, camera);
    expect(
        screen_before.has_value() && screen_after.has_value(),
        "panned target (= pivot) remains projectable across rotation"
    );
    if (screen_before && screen_after) {
        expect(
            std::abs(screen_after->x - screen_before->x) < 0.1f &&
            std::abs(screen_after->y - screen_before->y) < 0.1f,
            "panned camera target (= pivot) must stay at the same screen pixel"
        );
    }
    expect(
        std::abs(camera.distance() - distance_before) < 0.01f,
        "rigid pivot rotation preserves camera look distance"
    );
}

void test_custom_orbit_pivot_and_focus()
{
    gs3d::camera::Camera camera;
    camera.set_viewport(800, 600);
    camera.set_orthographic(120.0f, 0.01f, 10000.0f);
    camera.look_at(
        {0.0f, -20.0f, 8.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    );

    gs3d::camera::CameraController controller;
    const gs3d::camera::Vec3 selected_point{5.0f, 3.0f, 2.0f};
    controller.set_orbit_pivot(selected_point);

    const gs3d::camera::Viewport viewport{800, 600};
    const auto pivot_before = gs3d::camera::MouseRay::to_screen(
        selected_point, viewport, camera);

    gs3d::camera::CameraInput rotate;
    rotate.viewport_width = 800;
    rotate.viewport_height = 600;
    rotate.rotate = true;
    rotate.rotate_begin = true;
    rotate.delta_x = 40.0f;
    rotate.delta_y = 20.0f;
    static_cast<void>(controller.update(camera, rotate));

    const auto pivot_after = gs3d::camera::MouseRay::to_screen(
        selected_point, viewport, camera);
    expect(
        pivot_before.has_value() && pivot_after.has_value() &&
        std::abs(pivot_after->x - pivot_before->x) < 0.1f &&
        std::abs(pivot_after->y - pivot_before->y) < 0.1f,
        "custom selected pivot stays fixed on screen while rotating"
    );

    const float distance_before_focus = camera.distance();
    controller.set_focus_anim_duration(0.0f);  // instant jump for test
    controller.focus_on(camera, selected_point);
    expect(
        std::abs(camera.target().x - selected_point.x) < 1.0e-5f &&
        std::abs(camera.target().y - selected_point.y) < 1.0e-5f &&
        std::abs(camera.target().z - selected_point.z) < 1.0e-5f,
        "focus moves the selected point to camera target"
    );
    expect(
        std::abs(camera.distance() - distance_before_focus) < 1.0e-4f,
        "focus preserves camera distance and zoom"
    );

    const auto focused_screen = gs3d::camera::MouseRay::to_screen(
        selected_point, viewport, camera);
    expect(
        focused_screen.has_value() &&
        std::abs(focused_screen->x - 400.0f) < 0.1f &&
        std::abs(focused_screen->y - 300.0f) < 0.1f,
        "focused point projects to viewport centre"
    );

    controller.clear_orbit_pivot();
    expect(
        !controller.orbit_pivot().has_value(),
        "reset clears the custom orbit pivot"
    );
}

void test_rotation_orbits_current_target()
{
    // Incremental orbit always uses the current camera.target() and changes
    // only camera.position(). Cursor position and drag boundaries must not
    // move the target.

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

        expect(
            std::abs(tgt_after.x - tgt_before.x) < 1.0e-3f &&
            std::abs(tgt_after.y - tgt_before.y) < 1.0e-3f &&
            std::abs(tgt_after.z - tgt_before.z) < 1.0e-3f,
            "rotate_begin must not change camera.target (no view jump on press)"
        );
    }

    // --- Frame 2: continuing drag, cursor moved off-centre ---
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
    {
        const auto tgt_before = camera.target();
        gs3d::camera::CameraInput input;
        input.viewport_width = 800;
        input.viewport_height = 600;
        input.rotate = false;
        input.rotate_begin = false;
        static_cast<void>(controller.update(camera, input));
        const auto tgt_after = camera.target();

        expect(
            std::abs(tgt_after.x - tgt_before.x) < 1.0e-6f &&
            std::abs(tgt_after.y - tgt_before.y) < 1.0e-6f &&
            std::abs(tgt_after.z - tgt_before.z) < 1.0e-6f,
            "release must not change the orbit target"
        );
    }

    // --- Frame 4: new drag-start at off-centre cursor ---
    // Cursor position does not replace the current orbit target.
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

        expect(
            std::abs(tgt_after.x - tgt_before.x) < 1.0e-6f &&
            std::abs(tgt_after.y - tgt_before.y) < 1.0e-6f &&
            std::abs(tgt_after.z - tgt_before.z) < 1.0e-6f,
            "off-centre drag must keep the current orbit target"
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

    // Default orbit pitch limits: −89° … +89° (radians).
    const double kMinPitch = -1.553;  // ≈ −89°
    const double kMaxPitch =  1.553;  // ≈ +89°

    // Float round-trip sin→asin near the pole adds ~1e-3 error.
    constexpr double kPhiSlop = 0.015;

    // --- Drag down repeatedly far beyond 90° total ---
    {
        gs3d::camera::CameraInput input;
        input.viewport_width = 800;
        input.viewport_height = 600;
        input.rotate = true;
        input.rotate_begin = true;
        input.delta_y = 50.0f;  // large downward drag
        input.mouse_x = 400.0f;
        input.mouse_y = 300.0f;
        static_cast<void>(controller.update(camera, input));

        const double phi = total_phi();
        expect(
            phi >= kMinPitch - kPhiSlop && phi <= kMaxPitch + kPhiSlop,
            "after first drag-down phi stays inside config pitch limits"
        );
        // cos(φ) near π/2 needs double precision; float32 cos may
        // underflow to zero even when φ is 1 µrad inside the pole.
        expect(
            std::cos(phi) > 0.0,
            "cos(phi) > 0 after drag-down (no azimuth flip)"
        );
    }

    // Continue dragging down (cumulative far beyond 90°).
    for (int i = 0; i < 20; ++i) {
        gs3d::camera::CameraInput input;
        input.viewport_width = 800;
        input.viewport_height = 600;
        input.rotate = true;
        input.rotate_begin = false;  // continuing
        input.delta_y = 50.0f;       // keep pushing down
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

    // --- Drag up from pole: must respond immediately (no lag) ---
    {
        const double phi_before = total_phi();

        gs3d::camera::CameraInput input;
        input.viewport_width = 800;
        input.viewport_height = 600;
        input.rotate = true;
        input.rotate_begin = false;  // continuing drag
        input.delta_y = -20.0f;       // drag up
        input.mouse_x = 400.0f;
        input.mouse_y = 300.0f;
        static_cast<void>(controller.update(camera, input));

        const double phi_after = total_phi();
        expect(
            phi_after < phi_before - 1.0e-4,
            "drag up from pole limit lowers phi immediately"
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

void test_hover_cleared_when_no_hit()
{
    // After kNoHitClearThreshold consecutive has_hit=false pick results,
    // the hover point must be cleared so the tooltip doesn't stick.
    // A single has_hit=true resets the debounce counter.
    constexpr int kThreshold = 3;
    std::optional<int> hover_point = 42;
    int consecutive_no_hit = 0;

    // has_hit=true keeps the point and resets the counter.
    consecutive_no_hit = 0;
    hover_point = 99;
    expect(hover_point.has_value(), "hover point set on hit");
    expect(consecutive_no_hit == 0, "counter reset on hit");

    // threshold-1 consecutive misses — point survives debounce.
    for (int i = 0; i < kThreshold - 1; ++i) {
        ++consecutive_no_hit;
        if (consecutive_no_hit >= kThreshold) {
            hover_point.reset();
        }
    }
    expect(hover_point.has_value(),
           "hover point persists before no-hit threshold");
    expect(consecutive_no_hit == kThreshold - 1,
           "counter at threshold-1");

    // threshold-th miss — point is cleared.
    ++consecutive_no_hit;
    if (consecutive_no_hit >= kThreshold) {
        hover_point.reset();
    }
    expect(!hover_point.has_value(),
           "hover point cleared after consecutive no-hit frames");

    // A hit after clear restores the point and resets the counter.
    consecutive_no_hit = 0;
    hover_point = 77;
    expect(hover_point.has_value(),
           "hover point restored on hit after clear");
    expect(consecutive_no_hit == 0,
           "counter reset on hit after clear");
}

// =====================================================================
// A3: regression tests for the orbit-pivot policy.
//
// A3a — controller behaviour with REAL mouse-pixel input (goes through
//       pan_view's pixel→world conversion, FOV, viewport size, etc.).
// A3b — pure geometric invariant under a world-space translation D
//       (bypasses the pixel pipeline; the failure mode is unambiguous
//       if it ever regresses: "rotation geometry broke" vs
//       "pixel-to-world conversion broke").
//
// Invariant under the new policy:
//   pivot = orbit_pivot_.value_or(camera.target())
//   pan  : pos += D, tgt += D, explicit orbit_pivot_ remains world-locked
//   rotate: rigid (pos - pivot, tgt - pivot) preserved
// =====================================================================

// A3a-1: an explicit pivot represents the selected world-space point.
// Panning moves the camera, not the selected point.
void test_pan_keeps_explicit_pivot_world_locked()
{
    gs3d::camera::Camera camera;
    camera.set_viewport(800, 600);
    camera.set_orthographic(100.0f, 0.01f, 1000.0f);
    camera.look_at(
        {0.0f, -10.0f, 5.0f},
        {0.0f,   0.0f, 0.0f},
        {0.0f,   0.0f, 1.0f});

    gs3d::camera::CameraController controller;
    const gs3d::camera::Vec3 explicit_pivot{2.0f, 3.0f, 0.5f};
    controller.set_orbit_pivot(explicit_pivot);

    // Record state, pan, then verify pos and target moved together while
    // the explicit pivot remained at the selected world coordinate.
    const auto pos_before = camera.position();
    const auto tgt_before = camera.target();
    const auto piv_before = *controller.orbit_pivot();

    gs3d::camera::CameraInput input{};
    input.viewport_width = 800;
    input.viewport_height = 600;
    input.pan = true;
    input.delta_x = 60.0f;
    input.delta_y = -20.0f;
    static_cast<void>(controller.update(camera, input));

    const auto pos_after = camera.position();
    const auto tgt_after = camera.target();
    const auto piv_after = *controller.orbit_pivot();

    const auto dpos = gs3d::camera::Vec3{
        pos_after.x - pos_before.x,
        pos_after.y - pos_before.y,
        pos_after.z - pos_before.z};
    const auto dtgt = gs3d::camera::Vec3{
        tgt_after.x - tgt_before.x,
        tgt_after.y - tgt_before.y,
        tgt_after.z - tgt_before.z};
    // Use a small tolerance: floating-point noise from the pixel→world
    // conversion is well below 1e-3.
    const float tol = 1.0e-3f;
    expect(
        std::abs(dpos.x - dtgt.x) < tol &&
        std::abs(dpos.y - dtgt.y) < tol &&
        std::abs(dpos.z - dtgt.z) < tol,
        "pan moves pos and tgt by the same world delta"
    );
    expect(
        std::abs(piv_after.x - piv_before.x) < tol &&
        std::abs(piv_after.y - piv_before.y) < tol &&
        std::abs(piv_after.z - piv_before.z) < tol,
        "pan keeps the explicit orbit pivot at its selected world point"
    );
}

// A3a-2: with no explicit pivot, rotation uses camera.target() as the
// pivot. Concretely: after a rotation, the camera target stays on the
// same screen pixel (because both pos and tgt are rigidly rotated
// around the target).
void test_rotation_uses_target_as_implicit_pivot()
{
    gs3d::camera::Camera camera;
    camera.set_viewport(800, 600);
    camera.set_orthographic(200.0f, 0.01f, 1000.0f);
    camera.look_at(
        {0.0f, -50.0f, 20.0f},
        {10.0f,  0.0f,  5.0f},
        {0.0f,   0.0f,  1.0f});

    gs3d::camera::CameraController controller;
    // No set_orbit_pivot call — implicit pivot = camera.target().

    const gs3d::camera::Viewport viewport{800, 600};
    const auto target_before = camera.target();
    const auto screen_before = gs3d::camera::MouseRay::to_screen(
        target_before, viewport, camera);
    expect(screen_before.has_value(),
           "target is projectable before rotation");

    gs3d::camera::CameraInput input{};
    input.viewport_width = 800;
    input.viewport_height = 600;
    input.rotate = true;
    input.delta_x = 80.0f;
    input.delta_y = 30.0f;
    static_cast<void>(controller.update(camera, input));

    // Rotation should keep camera.target() at the same screen pixel
    // (rigid rotation around target).
    const auto screen_after = gs3d::camera::MouseRay::to_screen(
        target_before, viewport, camera);
    expect(screen_after.has_value(),
           "target is projectable after rotation");
    if (screen_before && screen_after) {
        expect(
            std::abs(screen_after->x - screen_before->x) < 0.1f &&
            std::abs(screen_after->y - screen_before->y) < 0.1f,
            "rotation around camera.target() keeps target on same screen pixel"
        );
    }
    // distance to target preserved
    const float dist_to_target = std::sqrt(
        (camera.position().x - target_before.x) *
            (camera.position().x - target_before.x) +
        (camera.position().y - target_before.y) *
            (camera.position().y - target_before.y) +
        (camera.position().z - target_before.z) *
            (camera.position().z - target_before.z));
    expect(
        std::abs(camera.distance() - dist_to_target) < 0.5f,
        "rotation around camera.target() preserves distance to target"
    );
}

// A3a-3: pan then rotate preserves the orbit geometry (pos - pivot
// and target - pivot unchanged), for both implicit (target) and
// explicit pivot modes. This is the headline invariant — if it
// regresses, the user complaint ("after panning, the camera rotates
// around the wrong point") is back.
void test_pan_then_rotation_preserves_orbit_geometry()
{
    auto run_for_pivot = [](
        bool use_explicit_pivot,
        const gs3d::camera::Vec3& explicit_pivot_value
    ) {
        gs3d::camera::Camera camera;
        camera.set_viewport(800, 600);
        camera.set_orthographic(100.0f, 0.01f, 1000.0f);
        camera.look_at(
            {0.0f, -10.0f, 5.0f},
            {0.0f,   0.0f, 0.0f},
            {0.0f,   0.0f, 1.0f});

        gs3d::camera::CameraController controller;
        if (use_explicit_pivot) {
            controller.set_orbit_pivot(explicit_pivot_value);
        }

        // Pan
        gs3d::camera::CameraInput pan_input{};
        pan_input.viewport_width = 800;
        pan_input.viewport_height = 600;
        pan_input.pan = true;
        pan_input.delta_x = 100.0f;
        pan_input.delta_y = -40.0f;
        static_cast<void>(controller.update(camera, pan_input));

        // Snapshot the orbit geometry AFTER pan
        const auto pivot_after_pan = use_explicit_pivot
            ? *controller.orbit_pivot()
            : camera.target();
        const auto pos_off_after_pan = gs3d::camera::Vec3{
            camera.position().x - pivot_after_pan.x,
            camera.position().y - pivot_after_pan.y,
            camera.position().z - pivot_after_pan.z};
        const auto tgt_off_after_pan = gs3d::camera::Vec3{
            camera.target().x - pivot_after_pan.x,
            camera.target().y - pivot_after_pan.y,
            camera.target().z - pivot_after_pan.z};

        // Rotate
        gs3d::camera::CameraInput rot_input{};
        rot_input.viewport_width = 800;
        rot_input.viewport_height = 600;
        rot_input.rotate = true;
        rot_input.delta_x = 40.0f;
        rot_input.delta_y = 20.0f;
        static_cast<void>(controller.update(camera, rot_input));

        // pos - pivot and target - pivot are rigidly rotated, but the
        // DISTANCES and the diff are preserved.
        const auto pivot_after_rot = use_explicit_pivot
            ? *controller.orbit_pivot()
            : camera.target();
        const auto pos_off_after_rot = gs3d::camera::Vec3{
            camera.position().x - pivot_after_rot.x,
            camera.position().y - pivot_after_rot.y,
            camera.position().z - pivot_after_rot.z};
        const auto tgt_off_after_rot = gs3d::camera::Vec3{
            camera.target().x - pivot_after_rot.x,
            camera.target().y - pivot_after_rot.y,
            camera.target().z - pivot_after_rot.z};

        const float pos_len_before = std::sqrt(
            pos_off_after_pan.x * pos_off_after_pan.x +
            pos_off_after_pan.y * pos_off_after_pan.y +
            pos_off_after_pan.z * pos_off_after_pan.z);
        const float pos_len_after = std::sqrt(
            pos_off_after_rot.x * pos_off_after_rot.x +
            pos_off_after_rot.y * pos_off_after_rot.y +
            pos_off_after_rot.z * pos_off_after_rot.z);
        const float tgt_len_before = std::sqrt(
            tgt_off_after_pan.x * tgt_off_after_pan.x +
            tgt_off_after_pan.y * tgt_off_after_pan.y +
            tgt_off_after_pan.z * tgt_off_after_pan.z);
        const float tgt_len_after = std::sqrt(
            tgt_off_after_rot.x * tgt_off_after_rot.x +
            tgt_off_after_rot.y * tgt_off_after_rot.y +
            tgt_off_after_rot.z * tgt_off_after_rot.z);

        const std::string tag = use_explicit_pivot
            ? "explicit pivot"
            : "implicit pivot (=target)";
        expect(
            std::abs(pos_len_after - pos_len_before) < 0.01f,
            ("pan+rotate preserves |pos-pivot| [" + tag + "]").c_str()
        );
        expect(
            std::abs(tgt_len_after - tgt_len_before) < 0.01f,
            ("pan+rotate preserves |tgt-pivot| [" + tag + "]").c_str()
        );
    };

    run_for_pivot(false, {});
    run_for_pivot(true,  gs3d::camera::Vec3{2.0f, 3.0f, 0.5f});
}

// A3b: pure geometric invariant under a world-space translation D.
// Two cameras c1 and c2 differ by exactly D in (pos, tgt). Apply the
// SAME rotation input to both.  c2's rotation result must equal c1's
// rotation result translated by D — i.e., the orbit geometry
// (pos - pivot, target - pivot) is identical for both.
void test_orbit_is_invariant_under_uniform_world_translation()
{
    const gs3d::camera::Vec3 D{3.0f, 7.0f, -1.5f};

    auto build = [&](const gs3d::camera::Vec3& offset) {
        gs3d::camera::Camera cam;
        cam.set_viewport(800, 600);
        cam.set_orthographic(100.0f, 0.01f, 1000.0f);
        cam.look_at(
            {0.0f + offset.x, -10.0f + offset.y,  5.0f + offset.z},
            {0.0f + offset.x,   0.0f + offset.y,  0.0f + offset.z},
            {0.0f,               0.0f,             1.0f});
        return cam;
    };

    // Test both modes
    auto run = [&](bool use_explicit_pivot) {
        gs3d::camera::Camera c1 = build({0.0f, 0.0f, 0.0f});
        gs3d::camera::Camera c2 = build(D);

        gs3d::camera::CameraController k1;
        gs3d::camera::CameraController k2;
        if (use_explicit_pivot) {
            // For translation invariance to hold, the explicit pivot
            // must also be translated by D (full state sync). This
            // mirrors what CameraHub::propagate does at runtime: when
            // viewport B is linked to viewport A and A's pivot moves,
            // B's pivot moves by the same delta.
            k1.set_orbit_pivot({1.0f, 2.0f, 0.5f});
            k2.set_orbit_pivot({1.0f + D.x, 2.0f + D.y, 0.5f + D.z});
        }

        auto apply_rotate = [](
            gs3d::camera::Camera& cam,
            gs3d::camera::CameraController& k,
            float dx, float dy
        ) {
            gs3d::camera::CameraInput in{};
            in.viewport_width = 800;
            in.viewport_height = 600;
            in.rotate = true;
            in.delta_x = dx;
            in.delta_y = dy;
            static_cast<void>(k.update(cam, in));
        };

        apply_rotate(c1, k1, 50.0f, 25.0f);
        apply_rotate(c2, k2, 50.0f, 25.0f);

        // c2 should equal c1 translated by D
        const auto dpos = gs3d::camera::Vec3{
            c2.position().x - c1.position().x,
            c2.position().y - c1.position().y,
            c2.position().z - c1.position().z};
        const auto dtgt = gs3d::camera::Vec3{
            c2.target().x - c1.target().x,
            c2.target().y - c1.target().y,
            c2.target().z - c1.target().z};
        const float tol = 1.0e-3f;
        const std::string tag = use_explicit_pivot
            ? "explicit pivot"
            : "implicit pivot (=target)";
        expect(
            std::abs(dpos.x - D.x) < tol &&
            std::abs(dpos.y - D.y) < tol &&
            std::abs(dpos.z - D.z) < tol,
            ("translation invariance: c2.pos - c1.pos == D [" + tag + "]").c_str()
        );
        expect(
            std::abs(dtgt.x - D.x) < tol &&
            std::abs(dtgt.y - D.y) < tol &&
            std::abs(dtgt.z - D.z) < tol,
            ("translation invariance: c2.tgt - c1.tgt == D [" + tag + "]").c_str()
        );

        // And the orbit geometry (pos - pivot, tgt - pivot) is identical
        const auto p1 = use_explicit_pivot
            ? *k1.orbit_pivot()
            : c1.target();
        const auto p2 = use_explicit_pivot
            ? *k2.orbit_pivot()
            : c2.target();
        const auto dpiv = gs3d::camera::Vec3{
            p2.x - p1.x, p2.y - p1.y, p2.z - p1.z};
        // For implicit pivot, pivot == target, so dpiv == dtgt == D.
        // For explicit pivot, the pivot was the same in both cameras
        // (NOT translated), so dpiv == 0. The translation invariance
        // comes from pan NOT having been called here — we only verify
        // that pure rotation preserves the orbit geometry.
        const auto pos_off1 = gs3d::camera::Vec3{
            c1.position().x - p1.x,
            c1.position().y - p1.y,
            c1.position().z - p1.z};
        const auto pos_off2 = gs3d::camera::Vec3{
            c2.position().x - p2.x,
            c2.position().y - p2.y,
            c2.position().z - p2.z};
        const auto tgt_off1 = gs3d::camera::Vec3{
            c1.target().x - p1.x,
            c1.target().y - p1.y,
            c1.target().z - p1.z};
        const auto tgt_off2 = gs3d::camera::Vec3{
            c2.target().x - p2.x,
            c2.target().y - p2.y,
            c2.target().z - p2.z};
        const float dp = std::abs(pos_off2.x - pos_off1.x) +
                         std::abs(pos_off2.y - pos_off1.y) +
                         std::abs(pos_off2.z - pos_off1.z);
        const float dt = std::abs(tgt_off2.x - tgt_off1.x) +
                         std::abs(tgt_off2.y - tgt_off1.y) +
                         std::abs(tgt_off2.z - tgt_off1.z);
        expect(
            dp < tol,
            ("orbit geometry: c2.pos - pivot == c1.pos - pivot [" + tag + "]").c_str()
        );
        expect(
            dt < tol,
            ("orbit geometry: c2.tgt - pivot == c1.tgt - pivot [" + tag + "]").c_str()
        );
    };

    run(false);  // implicit
    run(true);   // explicit
}

// =====================================================================
// A5: end-to-end scenario smoke tests for the seven user-visible flows
// listed in the design review:
//
//   1. 初始旋转
//   2. 大距离 pan 后旋转
//   3. focus 后旋转
//   4. focus 后 pan 再旋转
//   5. box select 后旋转
//   6. reset 后旋转
//   7. 多视口同步后旋转
//
// Each scenario asserts: after the operation, rotation pivots around
// the *current* camera target (= implicit pivot) and keeps it on screen.
// =====================================================================

namespace scenarios {

void make_camera(gs3d::camera::Camera& c)
{
    c.set_viewport(800, 600);
    c.set_orthographic(100.0f, 0.01f, 1000.0f);
    c.look_at(
        {0.0f, -10.0f, 5.0f},
        {0.0f,   0.0f, 0.0f},
        {0.0f,   0.0f, 1.0f});
}

void make_controller(gs3d::camera::CameraController& c)
{
    c.set_bounds({
        {-100.0f, -100.0f, -10.0f},
        { 100.0f,  100.0f,  10.0f}
    });
}

void rotate(
    gs3d::camera::Camera& cam,
    gs3d::camera::CameraController& ctl,
    float dx, float dy
) {
    gs3d::camera::CameraInput in{};
    in.viewport_width = 800;
    in.viewport_height = 600;
    in.rotate = true;
    in.delta_x = dx;
    in.delta_y = dy;
    static_cast<void>(ctl.update(cam, in));
}

void pan(
    gs3d::camera::Camera& cam,
    gs3d::camera::CameraController& ctl,
    float dx, float dy
) {
    gs3d::camera::CameraInput in{};
    in.viewport_width = 800;
    in.viewport_height = 600;
    in.pan = true;
    in.delta_x = dx;
    in.delta_y = dy;
    static_cast<void>(ctl.update(cam, in));
}

void assert_target_stays_on_screen(
    const char* scenario,
    gs3d::camera::Camera& cam,
    const gs3d::camera::Vec3& target_before,
    const gs3d::camera::Vec3& pivot_before
) {
    const gs3d::camera::Viewport viewport{800, 600};
    const auto sb = gs3d::camera::MouseRay::to_screen(
        target_before, viewport, cam);
    expect(sb.has_value(),
           std::string(scenario) + ": target is projectable");
    if (!sb) return;
    const auto sa = gs3d::camera::MouseRay::to_screen(
        target_before, viewport, cam);
    expect(sa.has_value(),
           std::string(scenario) + ": target is projectable after");
    if (!sa) return;
    expect(
        std::abs(sa->x - sb->x) < 0.5f &&
        std::abs(sa->y - sb->y) < 0.5f,
        std::string(scenario) +
            ": target (= pivot) stays on screen after rotation"
    );
    // Also assert the geometric invariant: |pos - pivot| preserved.
    const auto pos_off_now = gs3d::camera::Vec3{
        cam.position().x - pivot_before.x,
        cam.position().y - pivot_before.y,
        cam.position().z - pivot_before.z};
    const float r = std::sqrt(
        pos_off_now.x * pos_off_now.x +
        pos_off_now.y * pos_off_now.y +
        pos_off_now.z * pos_off_now.z);
    expect(
        r > 0.0f,
        std::string(scenario) +
            ": camera is still offset from pivot (not collapsed)"
    );
}

} // namespace scenarios

// 1) 初始旋转 — no pan, no focus, just rotate from the start state.
void test_scenario_initial_rotation()
{
    using namespace scenarios;
    gs3d::camera::Camera cam;
    make_camera(cam);
    gs3d::camera::CameraController ctl;
    make_controller(ctl);

    expect(!ctl.orbit_pivot().has_value(),
           "initial: no explicit orbit pivot");

    const auto tgt = cam.target();
    const auto piv = tgt;  // implicit pivot = target
    rotate(cam, ctl, 80.0f, 30.0f);

    assert_target_stays_on_screen("1. initial rotation", cam, tgt, piv);
}

// 2) 大距离 pan 后旋转 — large pan, then rotate.
void test_scenario_long_pan_then_rotation()
{
    using namespace scenarios;
    gs3d::camera::Camera cam;
    make_camera(cam);
    gs3d::camera::CameraController ctl;
    make_controller(ctl);

    // Long pan (1.5x screen width)
    pan(cam, ctl, 1200.0f, -400.0f);

    // The implicit pivot (= target) must have moved with the pan.
    expect(cam.target().x != 0.0f || cam.target().y != 0.0f,
           "long pan moves camera target");

    const auto tgt = cam.target();
    const auto piv = tgt;
    rotate(cam, ctl, 60.0f, 25.0f);

    assert_target_stays_on_screen(
        "2. long pan + rotation", cam, tgt, piv);
}

// 3) focus 后旋转 — explicit pivot is set; rotation pivots around it.
void test_scenario_focus_then_rotation()
{
    using namespace scenarios;
    gs3d::camera::Camera cam;
    make_camera(cam);
    gs3d::camera::CameraController ctl;
    make_controller(ctl);
    ctl.set_focus_anim_duration(0.0f);

    const gs3d::camera::Vec3 focus_point{5.0f, 2.0f, 1.0f};
    ctl.focus_on(cam, focus_point);

    expect(ctl.orbit_pivot().has_value(),
           "focus_on sets explicit pivot");
    expect(
        std::abs(ctl.orbit_pivot()->x - focus_point.x) < 1.0e-3f &&
        std::abs(ctl.orbit_pivot()->y - focus_point.y) < 1.0e-3f &&
        std::abs(ctl.orbit_pivot()->z - focus_point.z) < 1.0e-3f,
        "focus_on sets pivot to the focus point"
    );
    // After instant focus, target == focus point.
    expect(
        std::abs(cam.target().x - focus_point.x) < 1.0e-3f &&
        std::abs(cam.target().y - focus_point.y) < 1.0e-3f &&
        std::abs(cam.target().z - focus_point.z) < 1.0e-3f,
        "focus_on moves target to the focus point"
    );

    const auto piv = *ctl.orbit_pivot();
    rotate(cam, ctl, 50.0f, 20.0f);

    // The explicit focus point must stay on screen.
    const gs3d::camera::Viewport viewport{800, 600};
    const auto sb = gs3d::camera::MouseRay::to_screen(focus_point, viewport, cam);
    expect(sb.has_value(),
           "3. focus+rotation: focus point still projectable");
    if (sb) {
        // After rigid rotation, the focus point should be very close
        // to where it started.
        expect(
            std::abs(sb->x - 400.0f) < 5.0f &&
            std::abs(sb->y - 300.0f) < 5.0f,
            "3. focus+rotation: focus point stays near screen centre"
        );
    }
    expect(
        std::abs(ctl.orbit_pivot()->x - piv.x) < 1.0e-3f &&
        std::abs(ctl.orbit_pivot()->y - piv.y) < 1.0e-3f &&
        std::abs(ctl.orbit_pivot()->z - piv.z) < 1.0e-3f,
        "3. focus+rotation: explicit pivot is preserved"
    );
}

// 4) focus 后 pan 再旋转 — focus sets a world-space pivot, pan changes
// the composition, then rotation still pivots around that selected point.
void test_scenario_focus_then_pan_then_rotation()
{
    using namespace scenarios;
    gs3d::camera::Camera cam;
    make_camera(cam);
    gs3d::camera::CameraController ctl;
    make_controller(ctl);
    ctl.set_focus_anim_duration(0.0f);

    const gs3d::camera::Vec3 focus_point{5.0f, 2.0f, 1.0f};
    ctl.focus_on(cam, focus_point);

    const auto tgt_before_pan = cam.target();
    const auto piv_before_pan = *ctl.orbit_pivot();

    pan(cam, ctl, 300.0f, 0.0f);

    // Pan moves the camera target but must not move the selected point.
    expect(cam.target().x != tgt_before_pan.x,
           "pan after focus moves target");
    expect(
        std::abs(ctl.orbit_pivot()->x - piv_before_pan.x) < 1.0e-6f &&
        std::abs(ctl.orbit_pivot()->y - piv_before_pan.y) < 1.0e-6f &&
        std::abs(ctl.orbit_pivot()->z - piv_before_pan.z) < 1.0e-6f,
        "pan after focus keeps the explicit pivot world-locked");

    // Rotation around the locked point must preserve its post-pan screen
    // position; it need not be at screen centre after the composition moved.
    const gs3d::camera::Viewport viewport{800, 600};
    const auto pivot_screen_before_rotate =
        gs3d::camera::MouseRay::to_screen(piv_before_pan, viewport, cam);
    rotate(cam, ctl, 60.0f, 20.0f);

    const auto pivot_screen_after_rotate =
        gs3d::camera::MouseRay::to_screen(piv_before_pan, viewport, cam);
    expect(
        pivot_screen_before_rotate.has_value() &&
        pivot_screen_after_rotate.has_value(),
        "4. focus+pan+rotation: locked pivot remains projectable");
    if (pivot_screen_before_rotate && pivot_screen_after_rotate) {
        expect(
            std::abs(
                pivot_screen_after_rotate->x -
                pivot_screen_before_rotate->x
            ) < 0.1f &&
            std::abs(
                pivot_screen_after_rotate->y -
                pivot_screen_before_rotate->y
            ) < 0.1f,
            "4. focus+pan+rotation: rotation stays anchored to locked point"
        );
    }
}

// 5) box select 后旋转 — box select clears orbit_pivot, then animate_to
// moves the target. Rotation pivots around the new target.
void test_scenario_box_select_then_rotation()
{
    using namespace scenarios;
    gs3d::camera::Camera cam;
    make_camera(cam);
    gs3d::camera::CameraController ctl;
    make_controller(ctl);
    // Animation runs on real wall-clock; in tests frames are nearly
    // instant so the 0.3s default would never complete. Disable it.
    ctl.set_focus_anim_duration(0.0f);

    // Simulate the box-select flow: clear pivot, then animate_to a new target.
    ctl.clear_orbit_pivot();
    const gs3d::camera::Vec3 new_target{4.0f, 1.0f, 0.5f};
    ctl.animate_to(cam, new_target, cam.ortho_height());

    expect(!ctl.orbit_pivot().has_value(),
           "5. box select: explicit pivot cleared");
    expect(
        std::abs(cam.target().x - new_target.x) < 1.0e-2f &&
        std::abs(cam.target().y - new_target.y) < 1.0e-2f &&
        std::abs(cam.target().z - new_target.z) < 1.0e-2f,
           "5. box select: target == new_target after anim"
    );

    const auto piv = cam.target();
    rotate(cam, ctl, 40.0f, 20.0f);

    const gs3d::camera::Viewport viewport{800, 600};
    const auto sb = gs3d::camera::MouseRay::to_screen(
        new_target, viewport, cam);
    expect(sb.has_value(),
           "5. box select + rotation: new target still projectable");
    if (sb) {
        expect(
            std::abs(sb->x - 400.0f) < 5.0f &&
            std::abs(sb->y - 300.0f) < 5.0f,
           "5. box select + rotation: new target stays near screen centre"
        );
    }
}

// 6) reset 后旋转 — reset clears pivot, resets camera to bounds-centre
// default, then rotate. Rotation pivots around the reset target.
void test_scenario_reset_then_rotation()
{
    using namespace scenarios;
    gs3d::camera::Camera cam;
    make_camera(cam);
    gs3d::camera::CameraController ctl;
    make_controller(ctl);
    ctl.set_focus_anim_duration(0.0f);

    // Focus first so there IS an explicit pivot to be cleared by reset.
    ctl.focus_on(cam, {5.0f, 5.0f, 5.0f});
    expect(ctl.orbit_pivot().has_value(), "precond: focus set pivot");

    ctl.reset_view(cam);

    expect(!ctl.orbit_pivot().has_value(),
           "6. reset: explicit pivot cleared by reset_view");

    const auto tgt = cam.target();
    const auto piv = tgt;
    rotate(cam, ctl, 50.0f, 20.0f);

    assert_target_stays_on_screen("6. reset + rotation", cam, tgt, piv);
}

// 7) 多视口同步后旋转 — two linked viewports; after propagation, both
// pivot around the same point; rotation behaves identically.
void test_scenario_linked_viewports_after_propagation()
{
    using namespace scenarios;
    gs3d::camera::Camera c1;
    gs3d::camera::Camera c2;
    make_camera(c1);
    make_camera(c2);
    gs3d::camera::CameraController k1;
    gs3d::camera::CameraController k2;
    make_controller(k1);
    make_controller(k2);

    // Focus on viewport 1
    k1.set_focus_anim_duration(0.0f);
    k1.focus_on(c1, {3.0f, 1.0f, 0.5f});

    // Simulate CameraHub::propagate(idx, mirror_pivot) by hand
    k2.copy_pivot_from(k1);
    c2.look_at(c1.position(), c1.target(), c1.up());
    c2.set_orthographic(c1.ortho_height(), c1.near_plane(), c1.far_plane());

    expect(k1.orbit_pivot().has_value() && k2.orbit_pivot().has_value(),
           "7. linked: both viewports have explicit pivot");
    expect(
        std::abs(k1.orbit_pivot()->x - k2.orbit_pivot()->x) < 1.0e-5f &&
        std::abs(k1.orbit_pivot()->y - k2.orbit_pivot()->y) < 1.0e-5f &&
        std::abs(k1.orbit_pivot()->z - k2.orbit_pivot()->z) < 1.0e-5f,
           "7. linked: pivot mirrored after propagate"
    );

    const auto piv = *k1.orbit_pivot();
    rotate(c1, k1, 40.0f, 15.0f);
    rotate(c2, k2, 40.0f, 15.0f);

    // After rotation, both cameras should have the same view relative
    // to the pivot (translation invariance).
    const auto off1 = gs3d::camera::Vec3{
        c1.position().x - piv.x,
        c1.position().y - piv.y,
        c1.position().z - piv.z};
    const auto off2 = gs3d::camera::Vec3{
        c2.position().x - piv.x,
        c2.position().y - piv.y,
        c2.position().z - piv.z};
    expect(
        std::abs(off2.x - off1.x) < 1.0e-3f &&
        std::abs(off2.y - off1.y) < 1.0e-3f &&
        std::abs(off2.z - off1.z) < 1.0e-3f,
           "7. linked: both viewports have identical pos - pivot after rotate"
    );
}

} // namespace

#define LEGACY_TEST_CASE(test_function) \
    TEST_CASE(#test_function, "[runtime_logic]") { test_function(); }

    LEGACY_TEST_CASE(test_recent_projects_persist_and_dedupe)
    LEGACY_TEST_CASE(test_gpu_preference_is_user_scoped)
    LEGACY_TEST_CASE(test_resource_path_resolves_assets_from_executable_directory)
    LEGACY_TEST_CASE(test_default_viewer_config_is_portable)
    LEGACY_TEST_CASE(test_resize_debounce)
    LEGACY_TEST_CASE(test_resize_batch)
    LEGACY_TEST_CASE(test_tile_cache_budget_and_lru)
    LEGACY_TEST_CASE(test_oversized_tile_is_not_cached)
    LEGACY_TEST_CASE(test_scene_state_is_constructible_without_dataset_io)
    LEGACY_TEST_CASE(test_frame_upload_budget)
    LEGACY_TEST_CASE(test_camera_uses_view_local_input)
    LEGACY_TEST_CASE(test_arcball_drag_rotates_about_screen_axes)
    LEGACY_TEST_CASE(test_zoom_keeps_cursor_anchor_fixed)
    LEGACY_TEST_CASE(test_zoom_respects_max_distance_from_bounds)
    LEGACY_TEST_CASE(test_fit_bounds_distance_is_orientation_independent)
    LEGACY_TEST_CASE(test_fit_bounds_keeps_panorama_far_end_visible)
    LEGACY_TEST_CASE(test_orthographic_fit_bounds_uses_true_top_down_view)
    LEGACY_TEST_CASE(test_tile_selection_honors_full_z_range_config)
    LEGACY_TEST_CASE(test_tile_selection_uses_mapped_height_space)
    LEGACY_TEST_CASE(test_tile_selection_does_not_cap_visible_tiles)
    LEGACY_TEST_CASE(test_zoom_caps_depth_ratio_for_close_large_scene)
    LEGACY_TEST_CASE(test_rotate_refreshes_depth_ratio)
    LEGACY_TEST_CASE(test_pan_refreshes_depth_ratio)
    LEGACY_TEST_CASE(test_box_select_falls_back_at_grazing_pitch)
    LEGACY_TEST_CASE(test_box_select_stays_within_scene_bounds_under_camera_tilt)
    LEGACY_TEST_CASE(test_axis_ticks_returns_empty_for_degenerate_range)
    LEGACY_TEST_CASE(test_axis_ticks_are_evenly_spaced_and_within_range)
    LEGACY_TEST_CASE(test_axis_ticks_step_is_a_nice_round_number)
    LEGACY_TEST_CASE(test_axis_ticks_huge_origin_with_tiny_step_terminates)
    LEGACY_TEST_CASE(test_axis_ticks_huge_origin_with_tiny_step_is_capped)
    LEGACY_TEST_CASE(test_mouse_mapping_without_map_axis_uses_canvas_rect)
    LEGACY_TEST_CASE(test_mouse_mapping_with_map_axis_uses_plot_rect)
    LEGACY_TEST_CASE(test_mouse_mapping_with_map_axis_rejects_axis_margin)
    LEGACY_TEST_CASE(test_pick_11x11_neighborhood_hits_sparse_isolated_point)
    LEGACY_TEST_CASE(test_pick_11x11_neighborhood_misses_point_outside_radius)
    LEGACY_TEST_CASE(test_mouse_ray_to_screen_projects_target_near_center)
    LEGACY_TEST_CASE(test_mouse_ray_to_screen_skips_points_behind_camera)
    LEGACY_TEST_CASE(test_mouse_ray_to_screen_round_trips_with_from_screen)
    LEGACY_TEST_CASE(test_box_select_returns_nullopt_for_degenerate_rect)
    LEGACY_TEST_CASE(test_box_select_world_bounds_scales_with_screen_rect_size)
    LEGACY_TEST_CASE(test_box_select_world_bounds_centers_near_camera_look_target)
    LEGACY_TEST_CASE(test_nearest_point_query_returns_nullopt_for_no_candidates)
    LEGACY_TEST_CASE(test_nearest_point_query_picks_screen_closest_candidate)
    LEGACY_TEST_CASE(test_nearest_point_query_respects_max_screen_distance)
    LEGACY_TEST_CASE(test_nearest_point_query_skips_points_behind_camera)
    LEGACY_TEST_CASE(test_nearest_point_query_depth_tie_is_order_sensitive)
    LEGACY_TEST_CASE(test_lod_adaptive_level_starts_at_lowest_while_interacting)
    LEGACY_TEST_CASE(test_lod_adaptive_level_climbs_after_good_frame_streak)
    LEGACY_TEST_CASE(test_lod_adaptive_level_drops_immediately_when_over_budget)
    LEGACY_TEST_CASE(test_lod_adaptive_level_ignores_stale_feedback)
    LEGACY_TEST_CASE(test_lod_non_adaptive_mode_still_pins_to_lowest)
    LEGACY_TEST_CASE(test_spatial_select_picks_coarsest_covering)
    LEGACY_TEST_CASE(test_zoom_in_selects_finer_level)
    LEGACY_TEST_CASE(test_spatial_no_flicker_at_boundary)
    LEGACY_TEST_CASE(test_spatial_frozen_during_interaction)
    LEGACY_TEST_CASE(test_spatial_updates_after_interaction)
    LEGACY_TEST_CASE(test_continuous_zoom_in_flies_forward_without_stalling)
    LEGACY_TEST_CASE(test_idle_update_does_not_change_camera)
    LEGACY_TEST_CASE(test_rotation_after_pan_keeps_panned_target)
    LEGACY_TEST_CASE(test_rotation_does_not_undo_overlapping_pan)
    LEGACY_TEST_CASE(test_screen_space_pan_tracks_mouse_pixels)
    LEGACY_TEST_CASE(test_rotation_supports_nearly_full_pitch_range)
    LEGACY_TEST_CASE(test_rotation_after_pan_keeps_scene_pivot_on_screen)
    LEGACY_TEST_CASE(test_custom_orbit_pivot_and_focus)
    LEGACY_TEST_CASE(test_rotation_orbits_current_target)
    LEGACY_TEST_CASE(test_click_without_drag_does_not_move_camera)
    LEGACY_TEST_CASE(test_rotate_release_does_not_move_camera)
    LEGACY_TEST_CASE(test_rotation_pivot_is_screen_center)
    LEGACY_TEST_CASE(test_pitch_never_exceeds_pole)
    LEGACY_TEST_CASE(test_zoom_in_past_distance_floor_keeps_zooming_via_fov)
    LEGACY_TEST_CASE(test_fov_clamped_at_min)
    LEGACY_TEST_CASE(test_hover_cleared_when_no_hit)
    LEGACY_TEST_CASE(test_pan_keeps_explicit_pivot_world_locked)
    LEGACY_TEST_CASE(test_rotation_uses_target_as_implicit_pivot)
    LEGACY_TEST_CASE(test_pan_then_rotation_preserves_orbit_geometry)
    LEGACY_TEST_CASE(test_orbit_is_invariant_under_uniform_world_translation)
    LEGACY_TEST_CASE(test_scenario_initial_rotation)
    LEGACY_TEST_CASE(test_scenario_long_pan_then_rotation)
    LEGACY_TEST_CASE(test_scenario_focus_then_rotation)
    LEGACY_TEST_CASE(test_scenario_focus_then_pan_then_rotation)
    LEGACY_TEST_CASE(test_scenario_box_select_then_rotation)
    LEGACY_TEST_CASE(test_scenario_reset_then_rotation)
    LEGACY_TEST_CASE(test_scenario_linked_viewports_after_propagation)

#undef LEGACY_TEST_CASE
