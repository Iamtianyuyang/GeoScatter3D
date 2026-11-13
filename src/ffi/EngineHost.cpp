#include "ffi/EngineHost.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <future>
#include <utility>

namespace gs3d::ffi {

EngineHost::~EngineHost() {
    stop();
}

bool EngineHost::running() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

int EngineHost::start(const HeadlessRenderKernel::Config& config) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (worker_.joinable()) {
            return -2;
        }
        stopping_ = false;
        running_ = false;
        worker_ = std::thread([this] { worker_loop(); });
    }

    auto created = std::make_shared<std::promise<int>>();
    auto future = created->get_future();
    const HeadlessRenderKernel::Config cfg = config;

    const bool queued = post([this, cfg, created]() {
        try {
            auto kernel = std::make_unique<HeadlessRenderKernel>();
            const int rc = kernel->create(cfg);
            if (rc != 0) {
                set_error(kernel->last_error());
                created->set_value(rc);
                return;
            }
            kernel_ = std::move(kernel);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                running_ = true;
            }
            created->set_value(0);
        } catch (const std::exception& ex) {
            set_error(ex.what());
            created->set_value(-3);
        } catch (...) {
            set_error("unknown error while creating render kernel");
            created->set_value(-3);
        }
    });

    if (!queued) {
        created->set_value(-2);
    }

    const int rc = future.get();
    if (rc != 0) {
        stop();
    }
    return rc;
}

void EngineHost::stop() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!worker_.joinable()) {
            return;
        }
        stopping_ = true;
    }
    stop_io();
    cv_.notify_all();
    worker_.join();

    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
    frame_in_flight_.store(false);
}

void EngineHost::worker_loop() {
    // 退出前必须仍在本线程销毁 Vulkan 资源，保证「渲染线程独占」不变量。
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            if (jobs_.empty()) {
                if (stopping_) {
                    break;
                }
                continue;
            }
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        job();
    }

    kernel_.reset();
    scratch_.clear();
}

bool EngineHost::post(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_ || !worker_.joinable()) {
            return false;
        }
        jobs_.push_back(std::move(job));
    }
    cv_.notify_one();
    return true;
}

int EngineHost::invoke(const KernelCommand& command) {
    if (!running()) {
        return -2;
    }

    auto promise = std::make_shared<std::promise<int>>();
    auto future = promise->get_future();
    const bool queued = post([this, command, promise]() {
        try {
            if (!kernel_) {
                promise->set_value(-2);
                return;
            }
            const int rc = command(*kernel_);
            if (rc < 0 && !kernel_->last_error().empty()) {
                set_error(kernel_->last_error());
            }
            promise->set_value(rc);
        } catch (const std::exception& ex) {
            set_error(ex.what());
            promise->set_value(-3);
        } catch (...) {
            set_error("unknown error in render thread command");
            promise->set_value(-3);
        }
    });

    if (!queued) {
        return -2;
    }
    return future.get();
}

void EngineHost::io_loop() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(io_mutex_);
            io_cv_.wait(lock, [this] { return io_stopping_ || !io_jobs_.empty(); });
            if (io_jobs_.empty()) {
                if (io_stopping_) {
                    break;
                }
                continue;
            }
            job = std::move(io_jobs_.front());
            io_jobs_.pop_front();
        }
        job();
    }
}

bool EngineHost::post_io(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(io_mutex_);
        if (io_stopping_) {
            return false;
        }
        if (!io_thread_.joinable()) {
            io_thread_ = std::thread([this] { io_loop(); });
        }
        io_jobs_.push_back(std::move(job));
    }
    io_cv_.notify_one();
    return true;
}

void EngineHost::stop_io() {
    {
        std::lock_guard<std::mutex> lock(io_mutex_);
        if (!io_thread_.joinable()) {
            return;
        }
        io_stopping_ = true;
        io_jobs_.clear();
    }
    io_cv_.notify_all();
    io_thread_.join();
}

int EngineHost::submit_open_dataset(const std::string& path, std::uint64_t* out_task) {
    if (!running()) {
        return -2;
    }
    if (path.empty()) {
        return -1;
    }
    const std::uint64_t id = board_.add("queue");
    if (id == 0) {
        return -4;  // unread terminal rows still hold the four slots
    }

    const bool queued = post_io([this, id, path] { run_open_dataset(id, path); });
    if (!queued) {
        board_.finish(id, TaskBoard::State::Failed, "cannot queue dataset open");
        return -3;
    }
    if (out_task != nullptr) {
        *out_task = id;
    }
    return 0;
}

// Runs on the IO worker: no Vulkan, no kernel state.
void EngineHost::run_open_dataset(std::uint64_t id, std::string path) {
    auto loaded = std::make_shared<HeadlessRenderKernel::DatasetLoad>();
    std::string error;
    const bool ok = HeadlessRenderKernel::load_dataset_from_disk(path, loaded.get(), &error);
    post([this, id, loaded, ok, error]() {
        if (!board_.begin(id, "install")) {
            return;  // cancelled before install
        }
        if (!ok) {
            board_.finish(id, TaskBoard::State::Failed, error);
            return;
        }
        if (!kernel_) {
            board_.finish(id, TaskBoard::State::Failed, "engine not created");
            return;
        }
        const int rc = kernel_->install_dataset(std::move(*loaded));
        if (rc != 0) {
            board_.finish(id, TaskBoard::State::Failed, kernel_->last_error());
            return;
        }
        board_.set_dataset(kernel_->has_dataset(), kernel_->point_count());
        board_.finish(id, TaskBoard::State::Done, std::string{});
    });
}

int EngineHost::cancel(std::uint64_t task_id) {
    return board_.cancel(task_id) ? 0 : -1;
}

int EngineHost::poll_state(TaskBoard::Snapshot* out) {
    if (out == nullptr) {
        return -1;
    }
    return board_.poll(out) ? 0 : -2;
}

int EngineHost::request_frame() {
    if (!running()) {
        return -2;
    }

    bool expected = false;
    if (!frame_in_flight_.compare_exchange_strong(expected, true)) {
        // 已有一帧在渲染：合并请求，返回成功让调用方继续读旧帧。
        return 0;
    }

    const bool queued = post([this]() {
        render_pending_frame();
        frame_in_flight_.store(false);
    });
    if (!queued) {
        frame_in_flight_.store(false);
        return -2;
    }
    return 0;
}

void EngineHost::render_pending_frame() {
    if (!kernel_) {
        return;
    }
    try {
        /*
         * 窗口呈现模式：直接画进交换链并 present，不做任何回读。
         * 一帧被跳过（交换链刚重建）不算失败。
         */
        if (kernel_->has_surface()) {
            const auto started = std::chrono::steady_clock::now();
            const int present_rc = kernel_->present_frame();
            last_render_us_.store(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - started
                ).count()
            );
            if (present_rc == -2) {
                return;
            }
            if (present_rc != 0) {
                set_error(kernel_->last_error());
            }
            return;
        }
        const auto viewport = kernel_->viewport_state();
        if (viewport.width == 0 || viewport.height == 0) {
            return;
        }
        const std::size_t bytes =
            static_cast<std::size_t>(viewport.width) * viewport.height * 4u;
        if (scratch_.size() < bytes) {
            scratch_.resize(bytes);
        }

        std::uint32_t width = 0;
        std::uint32_t height = 0;
        const auto started = std::chrono::steady_clock::now();
        const int rc = kernel_->render_rgba(
            scratch_.data(),
            static_cast<std::uint32_t>(scratch_.size()),
            &width,
            &height
        );
        const auto finished = std::chrono::steady_clock::now();
        last_render_us_.store(
            std::chrono::duration_cast<std::chrono::microseconds>(
                finished - started
            ).count()
        );

        if (rc != 0) {
            set_error(kernel_->last_error());
            return;
        }
        channel_.publish(scratch_, width, height, width * 4u);
    } catch (const std::exception& ex) {
        set_error(ex.what());
    } catch (...) {
        set_error("unknown error while rendering frame");
    }
}

int EngineHost::read_frame_rgba(
    std::uint8_t* out,
    std::uint32_t max_bytes,
    std::uint32_t* out_w,
    std::uint32_t* out_h,
    std::uint32_t* out_stride,
    std::uint64_t* out_sequence
) const {
    return channel_.copy(out, max_bytes, out_w, out_h, out_stride, out_sequence);
}

double EngineHost::last_render_ms() const noexcept {
    return static_cast<double>(last_render_us_.load()) / 1000.0;
}

void EngineHost::set_error(const std::string& message) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_ = message;
}

std::string EngineHost::last_error() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_;
}

}  // namespace gs3d::ffi
