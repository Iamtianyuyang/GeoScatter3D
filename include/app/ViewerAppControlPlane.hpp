#pragma once

/*
 * TIA-109：控制面会话（应用侧胶水）。
 *
 * 组装 ComponentRegistry（面板组件 + viewport.main）、TCP/JSON-RPC 服务与
 * 帧同步截图：创建于 ViewerApp::run()，每帧在 UI 命令应用前 poll() 一次，
 * 析构时优雅关闭服务线程。
 */

#include "control/ComponentRegistry.hpp"
#include "control/ControlPlane.hpp"

#include <cstdint>

namespace gs3d::app {
class ScreenshotService;
struct AppState;
} // namespace gs3d::app

namespace gs3d::render { class VulkanSwapchain; }

namespace gs3d::app {

// --control-plane[=port] 的运行时配置；默认端口 12735，只绑 127.0.0.1。
struct ViewerControlPlaneConfig {
    bool enabled = false;
    std::uint16_t port = 12735;
};

class ControlPlaneSession {
public:
    ControlPlaneSession(
        const ViewerControlPlaneConfig& config,
        AppState& app_state,
        ScreenshotService& screenshot_service,
        const render::VulkanSwapchain& swapchain
    );

    ~ControlPlaneSession();

    ControlPlaneSession(const ControlPlaneSession&) = delete;
    ControlPlaneSession& operator=(const ControlPlaneSession&) = delete;

    // 每帧调用（渲染主线程）：处理待执行请求、投递已完成的截图响应。
    void poll();

    // 客户端通过 JSON-RPC `quit` 请求退出时返回 true。
    [[nodiscard]] bool quit_requested() const noexcept;

    // 服务是否正在监听。
    [[nodiscard]] bool running() const noexcept;

    // 供外部测试/调试读取当前注册表。
    [[nodiscard]] const control::ComponentRegistry& registry() const noexcept;

private:
    control::ComponentRegistry registry_;
    control::ControlPlane plane_;
};

} // namespace gs3d::app
