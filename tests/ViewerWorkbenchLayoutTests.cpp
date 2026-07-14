#include "app/ViewerWorkbenchLayout.hpp"

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
