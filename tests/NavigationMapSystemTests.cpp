#include "app/NavigationMapSystem.hpp"

#include "app/AppState.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("NavigationMapSystem maps each rendered view into thumbnail space")
{
    gs3d::app::AppState state;
    state.navigation_maps.resize(1);
    state.render_views.resize(1);
    state.active_viewport_index = 0;

    auto& map = state.navigation_maps.front();
    map.valid = true;
    map.tex_w = 200.0f;
    map.tex_h = 100.0f;
    map.bbox_min_x = 10.0f;
    map.bbox_max_x = 110.0f;
    map.bbox_min_y = 20.0f;
    map.bbox_max_y = 220.0f;

    auto& view = state.render_views.front();
    view.map_axis_x_min = 35.0f;
    view.map_axis_x_max = 85.0f;
    view.map_axis_y_min = 70.0f;
    view.map_axis_y_max = 170.0f;

    gs3d::app::NavigationMapSystem system;
    system.synchronize_view_rects(state);

    CHECK(map.view_rect_valid);
    CHECK(map.view_rect_min_x == Catch::Approx(50.0f));
    CHECK(map.view_rect_max_x == Catch::Approx(150.0f));
    CHECK(map.view_rect_min_y == Catch::Approx(25.0f));
    CHECK(map.view_rect_max_y == Catch::Approx(75.0f));
    CHECK(state.navigation_map.view_rect_valid);
}

TEST_CASE("NavigationMapSystem ignores a missing rendered view")
{
    gs3d::app::AppState state;
    state.navigation_maps.resize(1);
    state.navigation_maps.front().valid = true;

    gs3d::app::NavigationMapSystem system;
    system.synchronize_view_rects(state);

    CHECK_FALSE(state.navigation_maps.front().view_rect_valid);
}
