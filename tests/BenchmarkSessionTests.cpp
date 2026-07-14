#include "app/BenchmarkSession.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("BenchmarkSession records only enabled runs", "[benchmark_session]")
{
    gs3d::app::BenchmarkSession session(false, 60, 2);
    session.record_frame({.wall_frame_ms = 4.0}, 3.0);

    CHECK(session.should_continue());
    CHECK(session.app_frame_index() == 1);
    CHECK(session.frame_index() == 0);
    CHECK(session.samples().wall_frame_times_ms.empty());
}

TEST_CASE("BenchmarkSession stops at configured frame count", "[benchmark_session]")
{
    gs3d::app::BenchmarkSession session(true, 3, 2);
    CHECK(session.should_orbit());

    session.record_frame({.wall_frame_ms = 1.0}, std::nullopt);
    session.record_frame({.wall_frame_ms = 2.0}, 1.5);
    session.record_frame({.wall_frame_ms = 3.0}, 2.5);

    CHECK_FALSE(session.should_continue());
    CHECK(session.frame_index() == 3);
    CHECK(session.samples().wall_frame_times_ms ==
          std::vector<double>{1.0, 2.0, 3.0});
    CHECK(session.samples().gpu_frame_times_ms ==
          std::vector<double>{1.5, 2.5});
}

TEST_CASE(
    "BenchmarkSession reserves two thirds of a normal run for settling",
    "[benchmark_session]"
) {
    gs3d::app::BenchmarkSession session(true, 6, 2);

    CHECK(session.should_orbit());
    session.record_frame({}, std::nullopt);
    CHECK(session.should_orbit());
    session.record_frame({}, std::nullopt);
    CHECK_FALSE(session.should_orbit());
    CHECK(session.should_force_tile_reload());

    for (std::uint32_t frame = 0; frame < 4; ++frame) {
        session.record_frame({}, std::nullopt);
        CHECK_FALSE(session.should_orbit());
        CHECK_FALSE(session.should_force_tile_reload());
    }
}

TEST_CASE("BenchmarkSession accounts for pick warmup and GPU latency", "[benchmark_session]")
{
    gs3d::app::BenchmarkSession session(true, 600, 3);
    session.configure_pick_script(5);

    for (std::uint32_t frame = 0;
         frame < gs3d::app::BenchmarkSession::kPickWarmupFrames + 5 + 3 + 1;
         ++frame) {
        CHECK(session.should_continue());
        session.record_frame({}, std::nullopt);
    }
    CHECK_FALSE(session.should_continue());
    CHECK(session.frame_index() ==
          gs3d::app::BenchmarkSession::kPickWarmupFrames + 5 + 3 + 1);
}
