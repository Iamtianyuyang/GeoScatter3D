#include "app/ScreenshotService.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Screenshot capture region scales and clamps an ImGui canvas")
{
    const auto region = gs3d::app::resolve_screenshot_capture_region(
        90.0f, 40.0f, 240.0f, 170.0f,
        100.0f, 50.0f,
        2.0f, 2.0f,
        {200, 200}
    );

    REQUIRE(region.valid());
    CHECK(region.x == 0);
    CHECK(region.y == 0);
    CHECK(region.width == 200);
    CHECK(region.height == 200);
}

TEST_CASE("Screenshot capture region rejects an offscreen or empty canvas")
{
    CHECK(!gs3d::app::resolve_screenshot_capture_region(
        300.0f, 20.0f, 400.0f, 80.0f,
        0.0f, 0.0f,
        1.0f, 1.0f,
        {200, 100}
    ).valid());
    CHECK(!gs3d::app::resolve_screenshot_capture_region(
        10.0f, 10.0f, 10.0f, 20.0f,
        0.0f, 0.0f,
        1.0f, 1.0f,
        {200, 100}
    ).valid());
}

TEST_CASE("Screenshot save panel default names are unique PNG files")
{
    const auto first = gs3d::app::make_screenshot_output_path(
        "screenshots",
        1'721'234'567'890ULL
    );
    const auto second = gs3d::app::make_screenshot_output_path(
        "screenshots",
        1'721'234'567'891ULL
    );

    CHECK(first.parent_path() == "screenshots");
    CHECK(first.extension() == ".png");
    CHECK(first.filename().string().starts_with("screenshot_"));
    CHECK(first != second);
}

TEST_CASE("Screenshot save paths are normalized to PNG")
{
    CHECK(
        gs3d::app::normalize_screenshot_output_path("result") ==
        "result.png"
    );
    CHECK(
        gs3d::app::normalize_screenshot_output_path("result.jpg") ==
        "result.png"
    );
    CHECK(
        gs3d::app::normalize_screenshot_output_path("result.PNG") ==
        "result.PNG"
    );
}
