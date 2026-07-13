#include "app/ViewerFrameClock.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <limits>

namespace {

using Clock = std::chrono::steady_clock;

} // namespace

TEST_CASE("ViewerFrameClock smooths frame rates from a deterministic origin", "[frame_clock]")
{
    const Clock::time_point origin{};
    gs3d::app::ViewerFrameClock clock(origin);

    const auto first = clock.tick(origin + std::chrono::milliseconds(16));
    CHECK(first.delta_seconds == Catch::Approx(0.016));
    CHECK(first.smoothed_fps == Catch::Approx(62.5f));

    const auto second = clock.tick(
        origin + std::chrono::milliseconds(26)
    );
    CHECK(second.delta_seconds == Catch::Approx(0.010));
    CHECK(second.smoothed_fps == Catch::Approx(66.25f));

    const auto stalled = clock.tick(
        origin + std::chrono::milliseconds(26)
    );
    CHECK(stalled.delta_seconds == Catch::Approx(0.0));
    CHECK(stalled.smoothed_fps == Catch::Approx(66.25f));
}

TEST_CASE("ViewerFrameClock excludes present acquire wait from LOD timing", "[frame_clock]")
{
    using gs3d::app::ViewerFrameClock;

    CHECK(ViewerFrameClock::estimate_render_work_milliseconds(
        0.020, 4.0
    ) == Catch::Approx(16.0));
    CHECK(ViewerFrameClock::estimate_render_work_milliseconds(
        0.016, 16.67
    ) == Catch::Approx(0.0));
    CHECK(ViewerFrameClock::estimate_render_work_milliseconds(
        -1.0, 0.0
    ) == Catch::Approx(0.0));
    CHECK(ViewerFrameClock::estimate_render_work_milliseconds(
        std::numeric_limits<double>::quiet_NaN(), 0.0
    ) == Catch::Approx(0.0));
}
