#include "app/ViewerCameraFrameSystem.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

TEST_CASE(
    "InteractionDebouncer retains interaction between sparse input events",
    "[camera_frame]"
)
{
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::time_point{};
    gs3d::app::InteractionDebouncer debouncer(std::chrono::milliseconds(150));

    CHECK_FALSE(debouncer.update(false, start));
    CHECK(debouncer.update(true, start));
    CHECK(debouncer.update(false, start + std::chrono::milliseconds(149)));
    CHECK_FALSE(debouncer.update(false, start + std::chrono::milliseconds(150)));
}
