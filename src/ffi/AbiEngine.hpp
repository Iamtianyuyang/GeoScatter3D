#pragma once

#include "abi/AbiCore.hpp"
#include "abi/geoscatter3d.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace gs3d::abi {

/// Implements the v1 ABI over a Core: one worker thread for long tasks, a
/// mutex-guarded task registry, and a read-and-clear wake mask.
class AbiEngine {
public:
    explicit AbiEngine(Core& core);
    ~AbiEngine();

    AbiEngine(const AbiEngine&) = delete;
    AbiEngine& operator=(const AbiEngine&) = delete;

    int32_t submit(const gs3d_command& command, std::uint64_t* out_task_id);
    int32_t poll_state(gs3d_state* out_state);
    int32_t task_state(std::uint64_t task_id, gs3d_task_state* out_state);
    int32_t cancel(std::uint64_t task_id);
    int32_t read_frame(std::uint8_t* out_rgba, std::uint32_t max_bytes, std::uint32_t* out_w,
                       std::uint32_t* out_h, std::uint32_t* out_stride,
                       std::uint64_t* out_sequence);
    int32_t set_wake(gs3d_wake_fn on_wake, void* user);

private:
    struct Task {
        std::uint64_t id = 0;
        int32_t kind = 0;
        std::atomic<int32_t> status{GS3D_TASK_PENDING};
        std::atomic<int32_t> progress_permille{0};
        std::atomic<bool> cancel_requested{false};
        std::int32_t error_code = GS3D_OK;
        std::mutex message_mutex;
        std::string message;

        void set_message(const std::string& text);
        std::string message_copy();
    };

    class Sink final : public ProgressSink {
    public:
        Sink(AbiEngine& engine, Task& task) : engine_(engine), task_(task) {}
        void report(float progress) override;
        [[nodiscard]] bool cancelled() const override;

    private:
        AbiEngine& engine_;
        Task& task_;
    };

    void worker_loop();
    void run_task(Task& task, const std::string& payload);
    void notify(std::uint32_t mask);
    void finish_task(Task& task, int32_t status, int32_t error_code, const std::string& message);
    void prune_finished_locked();
    std::shared_ptr<Task> find_task(std::uint64_t task_id);

    Core& core_;
    std::thread worker_;
    std::mutex tasks_mutex_;
    std::condition_variable tasks_cv_;
    std::deque<std::uint64_t> queue_;
    std::map<std::uint64_t, std::shared_ptr<Task>> tasks_;
    std::uint64_t next_task_id_ = 1;
    bool stopping_ = false;

    std::atomic<std::uint32_t> pending_wake_{0};
    std::mutex wake_mutex_;
    gs3d_wake_fn wake_fn_ = nullptr;
    void* wake_user_ = nullptr;

    std::atomic<std::int32_t> last_error_{GS3D_OK};
};

} // namespace gs3d::abi
