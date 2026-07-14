#include "ui/ViewportAxisTicks.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <limits>

TEST_CASE("ViewportAxisTicks fills intervals between major ticks", "[viewport_axis]")
{
    const auto ticks = gs3d::ui::compute_minor_axis_ticks(
        10.0f,
        0.0f,
        -10.0f,
        10.0f
    );

    REQUIRE(ticks.size() == 6);
    CHECK(ticks[0] == Catch::Approx(-2.5f));
    CHECK(ticks[2] == Catch::Approx(-7.5f));
    CHECK(ticks[3] == Catch::Approx(2.5f));
    CHECK(ticks[5] == Catch::Approx(7.5f));
}

TEST_CASE(
    "ViewportAxisTicks bounds large-coordinate tiny-step views",
    "[viewport_axis]"
)
{
    const auto ticks = gs3d::ui::compute_minor_axis_ticks(
        0.008f,
        -14544.0f,
        -14544.0f,
        -14543.9912f
    );

    CHECK(ticks.size() <= 400);
    for (const float tick : ticks) {
        CHECK(std::isfinite(tick));
    }
}

TEST_CASE("ViewportAxisTicks rejects invalid major steps", "[viewport_axis]")
{
    CHECK(gs3d::ui::compute_minor_axis_ticks(
        0.0f, 0.0f, -1.0f, 1.0f
    ).empty());
    CHECK(gs3d::ui::compute_minor_axis_ticks(
        std::numeric_limits<float>::infinity(),
        0.0f,
        -1.0f,
        1.0f
    ).empty());
}

TEST_CASE("ViewportAxisTicks uses one precision policy for labels", "[viewport_axis]")
{
    CHECK(gs3d::ui::axis_label_precision(10.0f) == 0);
    CHECK(gs3d::ui::axis_label_precision(0.1f) == 1);
    CHECK(gs3d::ui::axis_label_precision(0.008f) == 3);
    CHECK(gs3d::ui::axis_label_precision(0.00000001f) == 6);
    CHECK(gs3d::ui::axis_label_precision(-1.0f) == 0);

    char label[32]{};
    gs3d::ui::format_axis_tick_label(
        label, sizeof(label), 1.2345f, 100.0, 0.01f
    );
    CHECK(std::strcmp(label, "101.23") == 0);
}
