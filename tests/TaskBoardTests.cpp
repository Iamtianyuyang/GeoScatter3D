#include "ffi/TaskBoard.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <set>
#include <thread>
#include <vector>

using gs3d::ffi::TaskBoard;

namespace {

std::size_t count_state(const TaskBoard::Snapshot& snapshot, TaskBoard::State state) {
    return static_cast<std::size_t>(std::count_if(
        snapshot.tasks.begin(), snapshot.tasks.end(),
        [state](const TaskBoard::Entry& entry) { return entry.state == state; }));
}

}  // namespace

TEST_CASE("Task board keeps at most four rows until terminal rows are read")
{
    TaskBoard board;
    std::vector<std::uint64_t> ids;
    for (int i = 0; i < 4; ++i) {
        ids.push_back(board.add("queue"));
    }
    REQUIRE(ids[3] != 0);
    REQUIRE(board.add("queue") == 0);

    board.finish(ids[0], TaskBoard::State::Done, {});
    // The unread terminal row still occupies its slot.
    REQUIRE(board.add("queue") == 0);

    TaskBoard::Snapshot snapshot;
    REQUIRE(board.poll(&snapshot));
    REQUIRE(snapshot.tasks.size() == 4);
    REQUIRE(count_state(snapshot, TaskBoard::State::Done) == 1);
    REQUIRE(board.add("queue") != 0);
}

TEST_CASE("Task board delivers a terminal state exactly once")
{
    TaskBoard board;
    const std::uint64_t id = board.add("queue");
    REQUIRE(id != 0);

    REQUIRE(board.begin(id, "install"));
    board.finish(id, TaskBoard::State::Failed, "disk read failed");

    TaskBoard::Snapshot first;
    REQUIRE(board.poll(&first));
    REQUIRE(first.tasks.size() == 1);
    REQUIRE(first.tasks[0].state == TaskBoard::State::Failed);
    REQUIRE(first.tasks[0].stage == "failed");
    REQUIRE(first.error == "disk read failed");

    TaskBoard::Snapshot second;
    REQUIRE(board.poll(&second));
    REQUIRE(second.tasks.empty());
    // Releasing a terminal row is a read, not a state change.
    REQUIRE(second.generation == first.generation);

    TaskBoard::Snapshot warm;
    TaskBoard fresh;
    REQUIRE_FALSE(fresh.poll(&warm));
}

TEST_CASE("Cancel stops queued work and discards a cancelled result")
{
    TaskBoard board;
    const std::uint64_t queued = board.add("queue");
    REQUIRE(board.cancel(queued));
    REQUIRE_FALSE(board.begin(queued, "install"));

    TaskBoard::Snapshot after_queued_cancel;
    REQUIRE(board.poll(&after_queued_cancel));
    REQUIRE(after_queued_cancel.tasks.size() == 1);
    REQUIRE(after_queued_cancel.tasks[0].state == TaskBoard::State::Cancelled);
    REQUIRE_FALSE(board.cancel(queued));  // already terminal

    const std::uint64_t running = board.add("queue");
    REQUIRE(board.begin(running, "install"));
    REQUIRE(board.cancel(running));
    // The worker still reports its result; the board refuses to publish it.
    board.finish(running, TaskBoard::State::Done, {});
    TaskBoard::Snapshot after_running_cancel;
    REQUIRE(board.poll(&after_running_cancel));
    REQUIRE(after_running_cancel.tasks.size() == 1);
    REQUIRE(after_running_cancel.tasks[0].state == TaskBoard::State::Cancelled);
    REQUIRE_FALSE(after_running_cancel.dataset_loaded);
}

TEST_CASE("Dataset status is part of the published snapshot")
{
    TaskBoard board;
    board.set_dataset(true, 50'000'000);

    TaskBoard::Snapshot snapshot;
    REQUIRE(board.poll(&snapshot));
    REQUIRE(snapshot.dataset_loaded);
    REQUIRE(snapshot.point_count == 50'000'000);

    board.set_dataset(false, 0);
    REQUIRE(board.poll(&snapshot));
    REQUIRE_FALSE(snapshot.dataset_loaded);
    REQUIRE(snapshot.point_count == 0);
}

TEST_CASE("Concurrent submissions and polls never expose a partial snapshot")
{
    TaskBoard board;
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> last_generation{0};
    std::atomic<bool> generation_monotonic{true};
    std::atomic<bool> duplicate_rows{false};

    std::thread reader([&] {
        TaskBoard::Snapshot snapshot;
        while (!stop.load()) {
            if (!board.poll(&snapshot)) {
                continue;
            }
            if (snapshot.generation < last_generation.load()) {
                generation_monotonic.store(false);
            }
            last_generation.store(snapshot.generation);
            if (snapshot.tasks.size() > TaskBoard::kMaxTasks) {
                duplicate_rows.store(true);
            }
            std::set<std::uint64_t> ids;
            for (const auto& entry : snapshot.tasks) {
                if (!ids.insert(entry.id).second) {
                    duplicate_rows.store(true);
                }
            }
        }
    });

    std::vector<std::thread> writers;
    for (int worker = 0; worker < 4; ++worker) {
        writers.emplace_back([&] {
            for (int i = 0; i < 200; ++i) {
                const std::uint64_t id = board.add("queue");
                if (id == 0) {
                    continue;  // table full: expected backpressure
                }
                if (board.begin(id, "install")) {
                    board.finish(id, TaskBoard::State::Done, {});
                }
            }
        });
    }
    for (std::thread& writer : writers) {
        writer.join();
    }
    stop.store(true);
    reader.join();

    REQUIRE(generation_monotonic.load());
    REQUIRE_FALSE(duplicate_rows.load());
}
