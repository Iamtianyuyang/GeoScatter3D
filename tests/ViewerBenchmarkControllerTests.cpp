#include "app/ViewerBenchmarkController.hpp"
#include "app/UiActions.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

TEST_CASE("ViewerBenchmarkController keeps an ordinary viewer session inert")
{
    gs3d::app::ViewerBenchmarkController controller(
        false,
        120,
        2,
        {}
    );

    CHECK(!controller.session().enabled());
    CHECK(!controller.pick_enabled());
    CHECK(controller.queries().empty());
    CHECK(controller.results().empty());
}

TEST_CASE("ViewerBenchmarkController enables frame collection without a pick script")
{
    gs3d::app::ViewerBenchmarkController controller(
        true,
        120,
        2,
        {}
    );

    CHECK(controller.session().enabled());
    CHECK(!controller.pick_enabled());
    CHECK(controller.session().should_continue());
}

TEST_CASE("ViewerBenchmarkController injects and consumes one scripted query")
{
    const auto script_path =
        std::filesystem::temp_directory_path() /
        "geoscatter3d_benchmark_pick_controller.txt";
    {
        std::ofstream script(script_path);
        REQUIRE(script);
        script << "12.5 23.5\n";
    }

    gs3d::app::ViewerBenchmarkController controller(
        true,
        120,
        2,
        script_path
    );
    for (std::size_t i = 0;
         i < gs3d::app::BenchmarkSession::kPickWarmupFrames;
         ++i) {
        controller.session().record_frame({}, std::nullopt);
    }

    gs3d::app::UiActions actions;
    controller.apply_scripted_viewport(actions, 800, 600);
    REQUIRE(actions.viewport_frames.size() == 1);
    const auto& frame = actions.viewport_frames.front();
    CHECK(frame.index == 0);
    CHECK(frame.width == 800);
    CHECK(frame.height == 600);
    CHECK(frame.mouse_local_x == 12.5f);
    CHECK(frame.mouse_local_y == 23.5f);
    CHECK_FALSE(controller.take_active_query_for_viewport(1).has_value());
    REQUIRE(controller.take_active_query_for_viewport(0).has_value());
    CHECK_FALSE(controller.take_active_query_for_viewport(0).has_value());

    std::filesystem::remove(script_path);
}
