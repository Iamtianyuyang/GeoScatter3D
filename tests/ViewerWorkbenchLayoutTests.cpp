#include "app/ViewerWorkbenchLayout.hpp"
#include "ui/AnalysisRailLayout.hpp"
#include "ui/FloatingDockLayout.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Viewer workbench layout fits and centers on a valid work area", "[workbench_layout]")
{
    const auto layout = gs3d::app::compute_workbench_window_layout(
        1.15f,
        gs3d::app::DesktopWorkArea{
            .x = 0, .y = 0, .width = 1920, .height = 1080,
        },
        gs3d::app::WindowFrameInsets{
            .left = 8, .top = 30, .right = 8, .bottom = 8,
        }
    );

    CHECK(layout.client_width == 1452);
    CHECK(layout.client_height == 880);
    REQUIRE(layout.outer_x.has_value());
    REQUIRE(layout.outer_y.has_value());
    CHECK(*layout.outer_x == 226);
    CHECK(*layout.outer_y == 81);
}

TEST_CASE("Viewer workbench layout handles unavailable work areas safely", "[workbench_layout]")
{
    const auto layout = gs3d::app::compute_workbench_window_layout(
        0.0f,
        std::nullopt,
        gs3d::app::WindowFrameInsets{}
    );

    CHECK(layout.client_width == 1440);
    CHECK(layout.client_height == 900);
    CHECK_FALSE(layout.outer_x.has_value());
    CHECK_FALSE(layout.outer_y.has_value());
}

TEST_CASE("Viewer workbench layout clamps client dimensions after frame insets", "[workbench_layout]")
{
    const auto layout = gs3d::app::compute_workbench_window_layout(
        1.0f,
        gs3d::app::DesktopWorkArea{
            .width = 1200, .height = 700,
        },
        gs3d::app::WindowFrameInsets{
            .left = 2000, .top = 2000, .right = 2000, .bottom = 2000,
        }
    );

    CHECK(layout.client_width == 1);
    CHECK(layout.client_height == 1);
}

TEST_CASE("Floating dock top overlays clear the map-axis band", "[floating_dock]")
{
    const auto with_axis =
        gs3d::ui::compute_floating_dock_top_overlay_layout(
            10.0f,
            20.0f,
            1.25f,
            true
        );
    const auto without_axis =
        gs3d::ui::compute_floating_dock_top_overlay_layout(
            10.0f,
            20.0f,
            1.25f,
            false
        );

    CHECK(with_axis.left_x == 87.5f);
    CHECK(with_axis.primary_y == 70.0f);
    CHECK(with_axis.secondary_y == 130.0f);
    CHECK(without_axis.left_x == 30.0f);
    CHECK(without_axis.primary_y == 37.5f);
    CHECK(without_axis.secondary_y == 97.5f);
}

TEST_CASE(
    "Floating dock overlays take priority over viewport input",
    "[floating_dock]"
) {
    CHECK(gs3d::ui::floating_dock_allows_viewport_input(
        false,
        false,
        false
    ));
    CHECK_FALSE(gs3d::ui::floating_dock_allows_viewport_input(
        true,
        false,
        false
    ));
    CHECK_FALSE(gs3d::ui::floating_dock_allows_viewport_input(
        false,
        true,
        false
    ));
    CHECK_FALSE(gs3d::ui::floating_dock_allows_viewport_input(
        false,
        false,
        true
    ));
}

TEST_CASE("Navigation preview is square and contains its texture", "[floating_dock]")
{
    const auto layout = gs3d::ui::compute_navigation_preview_layout(
        10.0f,
        20.0f,
        300.0f,
        256.0f,
        128.0f
    );

    CHECK(layout.container.width == 300.0f);
    CHECK(layout.container.height == 300.0f);
    CHECK(layout.image.x == 10.0f);
    CHECK(layout.image.y == 95.0f);
    CHECK(layout.image.width == 300.0f);
    CHECK(layout.image.height == 150.0f);
    CHECK(
        layout.image.width / 256.0f ==
        layout.image.height / 128.0f
    );
}

TEST_CASE("Navigation preview fits a resizable rectangle", "[floating_dock]")
{
    const auto layout = gs3d::ui::compute_navigation_preview_layout(
        10.0f,
        20.0f,
        400.0f,
        240.0f,
        100.0f,
        200.0f
    );

    CHECK(layout.container.width == 400.0f);
    CHECK(layout.container.height == 240.0f);
    CHECK(layout.image.x == 150.0f);
    CHECK(layout.image.y == 20.0f);
    CHECK(layout.image.width == 120.0f);
    CHECK(layout.image.height == 240.0f);
}

TEST_CASE("Analysis rail matches the 1360 by 850 scheme C reference", "[analysis_rail]")
{
    const auto layout = gs3d::ui::compute_analysis_rail_layout(
        1360.0f,
        850.0f,
        1.0f,
        1.0f
    );

    CHECK(layout.rail_width == 54.0f);
    CHECK(layout.drawer_width == 272.0f);
    CHECK(layout.center_x == 326.0f);
    CHECK(layout.topbar_height == 50.0f);
    CHECK(layout.cards_width == 252.0f);
    CHECK(layout.viewport_width == 782.0f);
    CHECK(layout.viewport_height == 776.0f);
    CHECK(layout.status_y == 826.0f);
    CHECK(layout.status_height == 24.0f);
}

TEST_CASE("Analysis rail gives the viewport space when its drawer closes", "[analysis_rail]")
{
    const auto open = gs3d::ui::compute_analysis_rail_layout(
        1360.0f,
        850.0f,
        1.0f,
        1.0f
    );
    const auto closed = gs3d::ui::compute_analysis_rail_layout(
        1360.0f,
        850.0f,
        1.0f,
        0.0f
    );

    CHECK(closed.drawer_width == 0.0f);
    CHECK(closed.center_x == 54.0f);
    CHECK(closed.viewport_width ==
          open.viewport_width + open.drawer_width);
}
