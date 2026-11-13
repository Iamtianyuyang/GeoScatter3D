#pragma once

#include "ffi/FrameChannel.hpp"
#include "ffi/HeadlessRenderKernel.hpp"
#include "ffi/TaskBoard.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace gs3d::ffi {

/*
 * 引擎宿主：把 HeadlessRenderKernel 的全部 Vulkan 调用收敛到唯一渲染线程
 * （docs/spec/vulkan-core.md §4「渲染线程独占 Vk* 对象」）。
 *
 * - invoke()：控制面命令（load/resize/camera/export/...）在渲染线程执行并
 *   等待结果；调用线程按原有同步语义阻塞，但不触碰 Vulkan。
 * - request_frame()：异步渲染请求，渲染中再次请求会被合并（不排队）——
 *   手机端按 120Hz ticker 请求、内核按实际耗时出帧时不会积压。
 * - read_frame_rgba()：只读已发布帧，等待代价上限是一次 memcpy，
 *   不再让 UI 线程等完整的 GPU 提交 + 回读。
 */
class EngineHost {
public:
    using KernelCommand = std::function<int(HeadlessRenderKernel&)>;


    EngineHost() = default;
    ~EngineHost();

    EngineHost(const EngineHost&) = delete;
    EngineHost& operator=(const EngineHost&) = delete;
    EngineHost(EngineHost&&) = delete;
    EngineHost& operator=(EngineHost&&) = delete;

    /// 启动渲染线程并在其上创建内核；0 成功，负值失败（-2 已启动）。
    int start(const HeadlessRenderKernel::Config& config);
    /// 停止渲染线程：先排空已入队命令，再在渲染线程上销毁 Vulkan 资源。
    void stop() noexcept;

    [[nodiscard]] bool running() const noexcept;

    /// 在渲染线程执行命令并等待结果；未运行返回 -2，命令抛异常返回 -3。
    int invoke(const KernelCommand& command);

    /// 请求渲染一帧；渲染中重复请求返回 0 且不重复排队。
    int request_frame();

    /// 异步读取工程包：工作线程读盘，渲染线程装载。路径按值拷贝。
    /// 0 成功（任务号写入 out_task），-1 参数错误，-2 未运行，-3 无法排队，
    /// -4 任务表已满（上限 4 项，含尚未被读取的终态）。
    int submit_open_dataset(const std::string& path, std::uint64_t* out_task);

    /// 请求取消；文件读取本身不可中断，任务在装载前被丢弃。0 成功、-1 未知或已结束。
    int cancel(std::uint64_t task);

    /// 拷贝最近发布的一代状态；终态任务在读取后释放。0 成功、-1 参数错误、-2 未发布。
    int poll_state(TaskBoard::Snapshot* out);

    /// 读取最近完成帧（不触发 Vulkan）。0 成功、-1 容量不足、-2 尚无帧。
    int read_frame_rgba(
        std::uint8_t* out,
        std::uint32_t max_bytes,
        std::uint32_t* out_w,
        std::uint32_t* out_h,
        std::uint32_t* out_stride,
        std::uint64_t* out_sequence
    ) const;

    [[nodiscard]] std::uint64_t frame_sequence() const noexcept {
        return channel_.sequence();
    }

    [[nodiscard]] bool frame_ready() const noexcept {
        return channel_.ready();
    }

    /// 最近一帧的渲染耗时（毫秒；仅统计 Vulkan 提交 + 回读）。
    [[nodiscard]] double last_render_ms() const noexcept;

    /// 最近一次错误（渲染线程内的失败也会记录到这里）。
    [[nodiscard]] std::string last_error() const;

private:
    void worker_loop();
    bool post(std::function<void()> job);

    void io_loop();
    bool post_io(std::function<void()> job);
    void stop_io();
    void run_open_dataset(std::uint64_t id, std::string path);
    void render_pending_frame();
    void set_error(const std::string& message);

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> jobs_;
    std::thread worker_;
    bool stopping_ = false;
    bool running_ = false;

    mutable std::mutex error_mutex_;
    std::string last_error_;

    // 仅渲染线程访问
    std::unique_ptr<HeadlessRenderKernel> kernel_;
    std::vector<std::uint8_t> scratch_;

    TaskBoard board_;
    std::thread io_thread_;
    std::mutex io_mutex_;
    std::condition_variable io_cv_;
    std::deque<std::function<void()>> io_jobs_;
    bool io_stopping_ = false;

    FrameChannel channel_;
    std::atomic<bool> frame_in_flight_{false};
    std::atomic<std::int64_t> last_render_us_{0};
};

}  // namespace gs3d::ffi
