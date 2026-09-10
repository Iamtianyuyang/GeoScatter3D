#pragma once

// FFI 侧 headless Viewer 进程桥：启动 GeoScatter3D.exe 并通过 C-ABI
// 代理 JSON-RPC 控制面，使 Flutter 不必直接持有 TCP socket。

#include <cstdint>
#include <mutex>
#include <string>

namespace gs3d::ffi {

class ViewerBridge {
public:
    static ViewerBridge& instance();

    // 启动 headless Viewer 并连接控制面。成功返回 0。
    int start(const std::string& executable_path, const std::string& dataset_path);
    void stop(bool force = false);
    [[nodiscard]] bool connected() const;
    [[nodiscard]] int32_t last_capture_width() const;
    [[nodiscard]] int32_t last_capture_height() const;
    [[nodiscard]] std::string startup_log() const;
    [[nodiscard]] std::string last_error() const;

    // 发送一行 JSON-RPC，返回一行 JSON 响应（超时/失败返回 error JSON）。
    std::string rpc(const std::string& request_line);
    // 帧同步截图：返回 {"data":"<base64>","width":w,"height":h} 或 error。
    std::string screenshot_json();
    // execute 便捷封装。
    std::string execute(const std::string& component,
                        const std::string& command,
                        const std::string& params_json);
    std::string get_state(const std::string& component);

    ~ViewerBridge();

    ViewerBridge(const ViewerBridge&) = delete;
    ViewerBridge& operator=(const ViewerBridge&) = delete;

private:
    ViewerBridge() = default;

    void close_socket();
    bool connect_with_timeout(std::uint16_t port, int timeout_ms);
    void record_startup(const std::string& text);
    [[nodiscard]] std::uint16_t reserve_port();

    mutable std::mutex mutex_;
    bool connected_ = false;
    int32_t last_width_ = 0;
    int32_t last_height_ = 0;
    std::uint16_t port_ = 0;
    std::string startup_log_;
    std::string last_error_;
    std::intptr_t process_handle_ = 0;  // Windows HANDLE
    std::intptr_t socket_ = -1;
    int next_id_ = 1;
    std::string recv_buffer_;
};

}  // namespace gs3d::ffi
