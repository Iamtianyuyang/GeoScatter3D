#include "platform/CpuInfo.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("CPU topology reports a usable physical-core default")
{
    const auto logical =
        gs3d::platform::logical_cpu_thread_count();
    const auto physical =
        gs3d::platform::physical_cpu_core_count();

    REQUIRE(logical >= 1);
    REQUIRE(physical >= 1);
    REQUIRE(physical <= logical);
}
