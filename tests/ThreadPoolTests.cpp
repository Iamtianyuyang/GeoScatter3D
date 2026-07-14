#include "util/ThreadPool.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <future>
#include <stdexcept>

using namespace std::chrono_literals;

TEST_CASE(
    "ThreadPool cancels queued work during shutdown",
    "[thread_pool][shutdown]"
) {
    std::promise<void> active_task_started;
    const auto active_task_started_future =
        active_task_started.get_future();
    std::promise<void> release_active_task;
    const auto release_active_task_future =
        release_active_task.get_future().share();

    gs3d::util::ThreadPool pool(1);
    auto active_task = pool.submit([&] {
        active_task_started.set_value();
        release_active_task_future.wait();
        return 7;
    });
    REQUIRE(
        active_task_started_future.wait_for(1s) == std::future_status::ready
    );

    auto cancelled_task = pool.submit([] { return 11; });
    pool.shutdown();

    CHECK_THROWS_AS(pool.submit([] {}), std::runtime_error);

    release_active_task.set_value();
    CHECK(active_task.get() == 7);
    REQUIRE(cancelled_task.wait_for(1s) == std::future_status::ready);

    CHECK_THROWS_AS(
        static_cast<void>(cancelled_task.get()),
        gs3d::util::ThreadPool::TaskCancelled
    );
}
