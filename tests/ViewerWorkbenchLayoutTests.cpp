#include "app/ViewerWorkbenchLayout.hpp"
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
