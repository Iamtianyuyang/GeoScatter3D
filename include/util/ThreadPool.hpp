#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

namespace gs3d::util {

/*
 * Fixed-size thread pool for CPU-bound and I/O-bound work.
 *
 * Design references:
 *   - Cesium-Native AsyncSystem: fixed pool, tasks enqueued via std::function
 *   - PDAL: 7 hardcoded I/O threads for chunk loading
 *   - Naughty Dog job system (GDC): work-stealing, but this simpler FIFO
 *     queue is sufficient for sequential tile I/O.
 *
 * Usage:
 *   ThreadPool pool(4);
 *   auto f = pool.submit([] { return expensive_work(); });
 *   auto result = f.get();
 *
 * shutdown() cancels queued work but lets tasks that a worker already started
 * finish. This keeps application exit bounded by active work, not by an
 * unbounded backlog of stale requests.
 */
class ThreadPool {
public:
    explicit ThreadPool(std::size_t num_threads) {
        if (num_threads == 0) {
            throw std::invalid_argument("ThreadPool: num_threads must be > 0");
        }
        workers_.reserve(num_threads);
        for (std::size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    ~ThreadPool() {
        shutdown();
        join_workers();
    }

    // Reject future submissions and cancel tasks that no worker has started.
    // Futures for cancelled packaged tasks become ready with broken_promise.
    void shutdown() {
        std::queue<std::function<void()>> cancelled_tasks;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (shutdown_) {
                return;
            }
            shutdown_ = true;
            tasks_.swap(cancelled_tasks);
        }
        cv_.notify_all();
    }

    [[nodiscard]]
    std::size_t thread_count() const noexcept {
        return workers_.size();
    }

private:
    void join_workers() {
        for (auto& w : workers_) {
            if (w.joinable()) w.join();
        }
    }

public:
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    /*
     * Submit a callable, returns a std::future for its result.
     * The callable is executed on one of the pool threads.
     */
    template<typename F>
    auto submit(F&& f) -> std::future<std::invoke_result_t<F>> {
        using R = std::invoke_result_t<F>;
        auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
        auto future = task->get_future();
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (shutdown_) {
                throw std::runtime_error("ThreadPool: submit after shutdown");
            }
            tasks_.push([task = std::move(task)] { (*task)(); });
        }
        cv_.notify_one();
        return future;
    }

private:
    void worker_loop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] {
                    return shutdown_ || !tasks_.empty();
                });
                if (shutdown_ && tasks_.empty()) {
                    return;
                }
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }

    std::vector<std::thread>         workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex                        mutex_;
    std::condition_variable           cv_;
    bool                              shutdown_ = false;
};

/*
 * Returns the recommended I/O thread count for tile loading.
 * Capped at 6; I/O-bound work rarely benefits beyond this.
 * Based on PDAL's default (7) and Cesium-Native empirical tuning.
 */
inline std::size_t recommended_io_threads() noexcept {
    const auto hw = std::thread::hardware_concurrency();
    if (hw <= 2) return 2;
    if (hw <= 4) return 3;
    return 4; // cap: disk I/O rarely gains from >4 parallel readers
}

} // namespace gs3d::util
