#pragma once

#include <cstdint>
#include <string>

struct GLFWwindow;

namespace gs3d::platform {

struct WindowConfig {
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    std::string title = "GeoScatter3D 三维散点查看器";
    bool resizable = true;
    // false = 创建隐藏窗口（--headless 控制面模式；渲染照常进行）。
    bool visible = true;
};

struct WindowSize {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct MouseState {
    double x = 0.0;
    double y = 0.0;
    double last_x = 0.0;
    double last_y = 0.0;

    double delta_x = 0.0;
    double delta_y = 0.0;

    double scroll_x = 0.0;
    double scroll_y = 0.0;

    bool left_pressed = false;
    bool right_pressed = false;
    bool middle_pressed = false;
};

class Window {
public:
    explicit Window(const WindowConfig& config = {});
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    void poll_events();
    void wait_events();

    [[nodiscard]]
    bool should_close() const noexcept;

    void request_close() noexcept;

    // 无边框全屏 ↔ 窗口模式切换（GLFW monitor 切换）。恢复时回到进入
    // 全屏前记录的位置和尺寸。framebuffer resize 走现有回调链。
    void toggle_fullscreen() noexcept;

    [[nodiscard]]
    bool is_fullscreen() const noexcept;

    [[nodiscard]]
    WindowSize window_size() const noexcept;

    [[nodiscard]]
    WindowSize framebuffer_size() const noexcept;

    [[nodiscard]]
    bool framebuffer_resized() const noexcept;

    void clear_framebuffer_resized() noexcept;

    [[nodiscard]]
    const MouseState& mouse_state() const noexcept;

    void reset_mouse_delta() noexcept;

    [[nodiscard]]
    bool key_pressed(int key) const noexcept;

    [[nodiscard]]
    GLFWwindow* native_handle() const noexcept;

    [[nodiscard]]
    static bool is_glfw_available() noexcept;

private:
    GLFWwindow* window_ = nullptr;

    WindowConfig config_{};

    WindowSize window_size_{};
    WindowSize framebuffer_size_{};

    bool framebuffer_resized_ = false;

    // 进入全屏前的窗口几何，退出全屏时恢复。
    int windowed_pos_x_ = 0;
    int windowed_pos_y_ = 0;
    int windowed_width_ = 0;
    int windowed_height_ = 0;

    MouseState mouse_{};

private:
    static void ensure_glfw_initialized();
    static void release_glfw();

    static void framebuffer_size_callback(
        GLFWwindow* window,
        int width,
        int height
    );

    static void cursor_position_callback(
        GLFWwindow* window,
        double x,
        double y
    );

    static void mouse_button_callback(
        GLFWwindow* window,
        int button,
        int action,
        int mods
    );

    static void scroll_callback(
        GLFWwindow* window,
        double xoffset,
        double yoffset
    );

    static Window* from_native(GLFWwindow* window) noexcept;

    void update_window_size() noexcept;
    void update_framebuffer_size() noexcept;
};

} // namespace gs3d::platform
