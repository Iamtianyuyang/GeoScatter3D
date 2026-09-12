#include "ffi/EngineHost.hpp"

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
