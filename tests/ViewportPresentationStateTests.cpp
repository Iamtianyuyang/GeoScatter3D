#include "app/ViewportPresentationState.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

gs3d::app::AppState make_app_state()
{
    gs3d::app::AppState state;
    state.render_views.resize(3);
    state.render_settings_by_view.resize(3);
    state.navigation_maps.resize(3);
    state.measurements.resize(3);
    state.region_stats_by_view.resize(3);
    for (int index = 0; index < 3; ++index) {
        state.render_views[static_cast<std::size_t>(index)].viewport_index = index;
        state.render_views[static_cast<std::size_t>(index)].visible = index == 0;
    }
    return state;
}

} // namespace

TEST_CASE("ViewportPresentationState copies a newly visible main view", "[viewport_presentation]")
{
    auto state = make_app_state();
    gs3d::render::PointPushConstants push;
    push.point_size = 1.5f;
    gs3d::scene::SceneState scene;
    gs3d::app::ViewportPresentationState presentation(3, push, scene, 1.0f);
    presentation.initialize_visibility(state);

    presentation.pushes()[0].point_size = 4.0f;
    presentation.scenes()[0].active_attribute_index = 7;
    presentation.height_exaggerations()[0] = 2.5f;
    state.render_settings_by_view[0].point_size = 4.0f;
    state.render_views[1].visible = true;

    presentation.copy_newly_visible_views(
        state,
        presentation.first_visible_main_view(state)
    );

    CHECK(presentation.pushes()[1].point_size == 4.0f);
    CHECK(presentation.scenes()[1].active_attribute_index == 7);
    CHECK(presentation.height_exaggerations()[1] == 2.5f);
    CHECK(state.render_settings_by_view[1].point_size == 4.0f);
    CHECK(state.navigation_maps[1].dirty);
    CHECK_FALSE(state.navigation_maps[1].view_rect_valid);
}

TEST_CASE("ViewportPresentationState only seeds a view once", "[viewport_presentation]")
{
    auto state = make_app_state();
    gs3d::render::PointPushConstants push;
    gs3d::scene::SceneState scene;
    gs3d::app::ViewportPresentationState presentation(3, push, scene, 1.0f);
    presentation.initialize_visibility(state);
    state.render_views[1].visible = true;
    presentation.pushes()[0].point_size = 3.0f;
    presentation.copy_newly_visible_views(
        state,
        presentation.first_visible_main_view(state)
    );

    presentation.pushes()[0].point_size = 8.0f;
    presentation.copy_newly_visible_views(
        state,
        presentation.first_visible_main_view(state)
    );

    CHECK(presentation.pushes()[1].point_size == 3.0f);
}

TEST_CASE(
    "ViewportPresentationState prefers a visible workspace sibling",
    "[viewport_presentation]"
)
{
    auto state = make_app_state();
    state.render_views[1].visible = true;
    state.workspace_windows.push_back({});
    state.workspace_windows.back().viewport_indices = {1, 2};

    gs3d::render::PointPushConstants push;
    gs3d::scene::SceneState scene;
    gs3d::app::ViewportPresentationState presentation(3, push, scene, 1.0f);
    presentation.initialize_visibility(state);
    presentation.pushes()[0].point_size = 3.0f;
    presentation.pushes()[1].point_size = 7.0f;
    presentation.scenes()[1].active_height_index = 9;
    presentation.height_exaggerations()[1] = 4.0f;
    state.render_settings_by_view[1].point_size = 7.0f;
    state.navigation_maps[2].dirty = false;
    state.navigation_maps[2].view_rect_valid = true;
    state.region_stats_by_view[1].point_count = 42;
    state.render_views[2].visible = true;

    presentation.copy_newly_visible_views(
        state,
        presentation.first_visible_main_view(state)
    );

    CHECK(presentation.pushes()[2].point_size == 7.0f);
    CHECK(presentation.scenes()[2].active_height_index == 9);
    CHECK(presentation.height_exaggerations()[2] == 4.0f);
    CHECK(state.render_settings_by_view[2].point_size == 7.0f);
    CHECK(state.navigation_maps[2].dirty);
    CHECK_FALSE(state.navigation_maps[2].view_rect_valid);
    CHECK(state.region_stats_by_view[2].point_count == 42);
}

TEST_CASE(
    "ViewportPresentationState reconciles draw requests and streaming focus",
    "[viewport_presentation]"
)
{
    auto state = make_app_state();
    state.render_views[0].render_requested = true;
    state.render_views[1].visible = true;
    state.render_views[2].render_requested = true;

    gs3d::render::PointPushConstants push;
    gs3d::scene::SceneState scene;
    gs3d::app::ViewportPresentationState presentation(3, push, scene, 1.0f);
    std::vector<int> visible_viewports;
    visible_viewports.reserve(3);

    const auto stable = presentation.reconcile_runtime_viewports(
        state,
        3,
        1,
        visible_viewports
    );
    CHECK(stable.active_viewport_count == 2);
    CHECK(stable.streaming_viewport_index == 1);
    CHECK_FALSE(stable.streaming_viewport_changed);
    REQUIRE(visible_viewports.size() == 2);
    CHECK(visible_viewports[0] == 0);
    CHECK(visible_viewports[1] == 2);

    state.render_views[1].visible = false;
    const auto fallback = presentation.reconcile_runtime_viewports(
        state,
        3,
        1,
        visible_viewports
    );
    CHECK(fallback.active_viewport_count == 1);
    CHECK(fallback.streaming_viewport_index == 0);
    CHECK(fallback.streaming_viewport_changed);
}
