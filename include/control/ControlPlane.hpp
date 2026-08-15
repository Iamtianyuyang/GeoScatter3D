#pragma once

/*
 * TIA-109 控制面：本地 TCP（127.0.0.1）+ JSON-RPC 2.0。
 *
 * 线程模型（阶段 1 定版，经过真实挂死问题验证后收敛为单服务器线程）：
 *   - 服务器线程独占全部 socket I/O：一次 select() 同时等待监听 socket、
 *     唤醒 socket 与所有连接 socket（超时 50ms）。Windows 上 select 的
 *     timeval 超时可靠；WSAPoll 存在已关闭 socket 上永久阻塞的缺陷，不用。
 *   - 主线程（渲染循环）：每帧 poll() 取出请求 → ControlDispatcher 分发
 *     （唯一触碰 ComponentRegistry 的路径）→ 响应进 per-connection 出站
 *     队列，并通过 UDP 唤醒 socket 立即通知服务器线程发送。
 *   - 无跨线程并发访问同一 socket；stop() 置标志 + 唤醒字节后 join，随后
 *     才关闭 socket —— 不存在"别的线程还在 select 时关闭 socket"的路径。
 *
 * 帧同步（spike 项 2）：screenshot 请求不立即响应；主线程在当帧渲染前
 * 通过 CaptureCallbacks::arm_capture 武装截图，渲染帧的 post_pass 里把
 * 交换链图像拷回 staging buffer，读回并编码 PNG 后，由下一次 poll() 的
 * poll_capture() 取回并回传 —— 返回的截图必定是「请求被处理那一帧」的
 * 渲染结果，不会截到上一帧。
 */

#include "control/CapturedImage.hpp"
#include "control/ComponentRegistry.hpp"
#include "control/JsonRpc.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace gs3d::control {

// 截图能力注入点：控制面不直接依赖渲染层。
struct CaptureCallbacks {
    // 主线程调用：武装本帧截图。成功返回 true（区域无效/已有截图进行中返回 false）。
    std::function<bool()> arm_capture;
    // 主线程调用：取回已完成的内存 PNG（一次性；未完成返回 nullopt）。
    std::function<std::optional<CapturedImage>()> take_png;
};

struct ControlResponse {
    std::uint64_t connection_id = 0;
    std::string payload;
};

/*
 * 纯逻辑分发器：请求行 → 响应。不碰 socket，可单测。
 * 支持方法：list_components / get_state / execute / toggle / set_value /
 * click / screenshot / ping / quit。
 */
class ControlDispatcher {
public:
    explicit ControlDispatcher(
        ComponentRegistry& registry,
        CaptureCallbacks capture = {}
    );

    // 处理一条请求行，返回应立即回送的响应（通知无 id 时返回 nullopt）。
    // screenshot 请求不在此返回，而是武装截图后由 poll_capture() 产出。
    [[nodiscard]] std::optional<ControlResponse> handle_request(
        std::uint64_t connection_id,
        const std::string& request_line
    );

    // 每帧调用：驱动截图完成/超时判定，产出待回送响应。
    [[nodiscard]] std::vector<ControlResponse> poll_capture();

    [[nodiscard]] bool quit_requested() const noexcept { return quit_requested_; }

    // 截图等待超时（超过则回 -32000 capture timeout）。
    static constexpr std::chrono::milliseconds kCaptureTimeout{2000};

private:
    [[nodiscard]] nlohmann::json dispatch_method(
        const JsonRpcRequest& request
    );

    ComponentRegistry& registry_;
    CaptureCallbacks capture_;

    bool quit_requested_ = false;

    bool capture_armed_ = false;
    std::optional<std::uint64_t> capture_connection_;
    nlohmann::json capture_id_ = nullptr;
    std::chrono::steady_clock::time_point capture_armed_at_{};
};

/*
 * TCP 服务：只绑 127.0.0.1。start() 失败（端口占用等）返回 false 并给出
 * 错误信息，不抛异常 —— 控制面不可用时应用继续以 GUI 方式运行。
 */
class ControlPlane {
public:
    explicit ControlPlane(
        ComponentRegistry& registry,
        CaptureCallbacks capture = {}
    );
    ~ControlPlane();

    ControlPlane(const ControlPlane&) = delete;
    ControlPlane& operator=(const ControlPlane&) = delete;

    // 返回是否成功开始监听；失败时 error 含原因。
    [[nodiscard]] bool start(std::uint16_t port, std::string& error);

    // 停止服务器线程并关闭全部 socket（幂等）。
    void stop();

    [[nodiscard]] bool running() const noexcept { return running_.load(); }

    // 实际绑定的端口（start(0) 时由系统分配；测试用）。
    [[nodiscard]] std::uint16_t bound_port() const noexcept { return bound_port_; }

    // 主线程每帧调用：处理待执行请求 + 投递已完成的截图响应。
    void poll();

    [[nodiscard]] bool quit_requested() const noexcept {
        return dispatcher_.quit_requested();
    }

private:
    struct Connection {
        std::uint64_t id = 0;
        // 平台无关句柄（Windows SOCKET / POSIX int）。
        std::intptr_t sock = -1;
        std::mutex out_mutex;
        std::deque<std::string> outbound;
        std::string line_buffer;  // 服务器线程：未完成行的缓冲
    };

    void server_loop();
    void wake_server();
    void submit_response(std::uint64_t connection_id, std::string response_json);
    [[nodiscard]] Connection* find_connection(std::uint64_t id);
    [[nodiscard]] bool flush_outbound(Connection& connection);

    ComponentRegistry& registry_;
    CaptureCallbacks capture_;
    ControlDispatcher dispatcher_;

    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    std::uint16_t bound_port_ = 0;
    std::intptr_t listen_sock_ = -1;
    // 唤醒 socket 对：[0] 服务器线程读，[1] 主线程写（互联 UDP 回环对）。
    std::intptr_t wake_sock_[2] = {-1, -1};

    std::thread server_thread_;

    std::mutex connections_mutex_;
    std::map<std::uint64_t, std::unique_ptr<Connection>> connections_;
    std::uint64_t next_connection_id_ = 1;

    std::mutex inbound_mutex_;
    std::deque<std::pair<std::uint64_t, std::string>> inbound_;
};

} // namespace gs3d::control
