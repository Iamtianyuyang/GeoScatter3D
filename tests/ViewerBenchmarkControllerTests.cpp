#include "app/ViewerBenchmarkController.hpp"

#include <catch2/catch_test_macros.hpp>

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
