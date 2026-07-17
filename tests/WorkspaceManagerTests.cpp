#include "ui/WorkspaceManager.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

gs3d::app::AppState make_state()
{
    gs3d::app::AppState state;
    state.render_views.resize(3);
    for (int index = 0; index < 3; ++index) {
        state.render_views[static_cast<std::size_t>(index)].viewport_index = index;
        state.render_views[static_cast<std::size_t>(index)].visible = index == 0;
    }
    state.active_viewport_index = 0;
    return state;
}

} // namespace

TEST_CASE("Workspace creation claims one hidden viewport", "[workspace]")
{
    auto state = make_state();
    REQUIRE(gs3d::ui::create_workspace_window(state));
    REQUIRE(state.workspace_windows.size() == 1);
    CHECK(state.workspace_windows.front().id == 1);
    CHECK(state.workspace_windows.front().viewport_indices == std::vector<int>{1});
    CHECK(state.render_views[1].visible);
    CHECK(gs3d::ui::view_is_owned_by_workspace(state, 1));
    CHECK(gs3d::ui::main_workspace_viewports(state) == std::vector<int>{0});
    CHECK(gs3d::ui::visible_view_signature(state) == 1u);
}

TEST_CASE("Closed workspace returns its views to the hidden pool", "[workspace]")
{
    auto state = make_state();
    REQUIRE(gs3d::ui::create_workspace_window(state));
    state.workspace_windows.front().visible = false;
    state.render_views[1].detached = true;
    state.render_views[1].render_requested = true;

    gs3d::ui::prune_workspace_windows(state);

    CHECK(state.workspace_windows.empty());
    CHECK_FALSE(state.render_views[1].visible);
    CHECK_FALSE(state.render_views[1].detached);
    CHECK_FALSE(state.render_views[1].render_requested);
    CHECK(gs3d::ui::has_hidden_view(state));
}

TEST_CASE("Restoring the default workspace rejoins every view", "[workspace]")
{
    auto state = make_state();
    REQUIRE(gs3d::ui::create_workspace_window(state));
    state.render_views[0].detached = true;
    state.render_views[0].force_undock_next_frame = true;

    gs3d::ui::restore_default_workspace(state);

    CHECK(state.workspace_windows.empty());
    for (const auto& view : state.render_views) {
        CHECK_FALSE(view.detached);
        CHECK_FALSE(view.force_undock_next_frame);
    }
}

TEST_CASE("Popping out the immersive view keeps a main view available", "[workspace]")
{
    auto state = make_state();

    REQUIRE(gs3d::ui::pop_out_view_window(state, 0));

    CHECK(state.render_views[0].visible);
    CHECK(state.render_views[0].detached);
    CHECK(state.render_views[0].force_undock_next_frame);
    CHECK(state.active_viewport_index == 1);
    CHECK(state.render_views[1].visible);
    CHECK_FALSE(state.render_views[1].detached);
    CHECK_FALSE(state.render_views[1].force_undock_next_frame);
}
