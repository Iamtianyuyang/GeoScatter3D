#include "platform/Window.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace gs3d::platform {

namespace {

int g_window_count = 0;
bool g_glfw_initialized = false;

std::uint32_t safe_u32_from_int(int value) noexcept {
    return static_cast<std::uint32_t>(std::max(value, 0));
}

void enable_per_monitor_dpi_awareness() noexcept {
#if defined(_WIN32)
    // GLFW must be initialized after the process is DPI-aware. Otherwise
    // Windows virtualizes coordinates using the DPI of the startup monitor,
    // which makes a window dragged to a monitor with a different scale render
    // at the wrong size.
    using SetProcessDpiAwarenessContextFn = BOOL (WINAPI*)(HANDLE);
    const auto set_dpi_awareness_context =
        reinterpret_cast<SetProcessDpiAwarenessContextFn>(
            GetProcAddress(
                GetModuleHandleW(L"user32.dll"),
                "SetProcessDpiAwarenessContext"
            )
        );
    if (set_dpi_awareness_context != nullptr) {
        const auto per_monitor_aware_v2 = reinterpret_cast<HANDLE>(
            static_cast<std::intptr_t>(-4)
        );
        (void)set_dpi_awareness_context(per_monitor_aware_v2);
        return;
    }

    // Windows 8.1 fallback: PROCESS_PER_MONITOR_DPI_AWARE == 2. Load it
    // dynamically so Windows 7 builds do not require linking against Shcore.
    using SetProcessDpiAwarenessFn = HRESULT (WINAPI*)(int);
    if (HMODULE shcore = LoadLibraryW(L"shcore.dll")) {
        const auto set_dpi_awareness =
            reinterpret_cast<SetProcessDpiAwarenessFn>(
                GetProcAddress(shcore, "SetProcessDpiAwareness")
            );
        if (set_dpi_awareness != nullptr) {
            (void)set_dpi_awareness(2);
        }
        FreeLibrary(shcore);
    } else {
        (void)SetProcessDPIAware();
    }
#endif
}

} // namespace

Window::Window(const WindowConfig& config)
    : config_(config)
{
    ensure_glfw_initialized();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, config_.resizable ? GLFW_TRUE : GLFW_FALSE);

    window_ = glfwCreateWindow(
        static_cast<int>(config_.width),
        static_cast<int>(config_.height),
        config_.title.c_str(),
        nullptr,
        nullptr
    );

    if (!window_) {
        release_glfw();
        throw std::runtime_error("Window: failed to create GLFW window");
    }

    ++g_window_count;

    glfwSetWindowUserPointer(window_, this);

    glfwSetFramebufferSizeCallback(window_, framebuffer_size_callback);
    glfwSetCursorPosCallback(window_, cursor_position_callback);
    glfwSetMouseButtonCallback(window_, mouse_button_callback);
    glfwSetScrollCallback(window_, scroll_callback);

    update_window_size();
    update_framebuffer_size();

    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(window_, &x, &y);

    mouse_.x = x;
    mouse_.y = y;
    mouse_.last_x = x;
    mouse_.last_y = y;
}

Window::~Window() {
    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;

        --g_window_count;
        release_glfw();
    }
}

void Window::poll_events() {
    reset_mouse_delta();
    glfwPollEvents();
}

void Window::wait_events() {
    reset_mouse_delta();
    glfwWaitEvents();
}

bool Window::should_close() const noexcept {
    return window_ && glfwWindowShouldClose(window_);
}

void Window::request_close() noexcept {
    if (window_) {
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }
}

WindowSize Window::window_size() const noexcept {
    return window_size_;
}

WindowSize Window::framebuffer_size() const noexcept {
    return framebuffer_size_;
}

bool Window::framebuffer_resized() const noexcept {
    return framebuffer_resized_;
}

void Window::clear_framebuffer_resized() noexcept {
    framebuffer_resized_ = false;
}

const MouseState& Window::mouse_state() const noexcept {
    return mouse_;
}

void Window::reset_mouse_delta() noexcept {
    mouse_.delta_x = 0.0;
    mouse_.delta_y = 0.0;
    mouse_.scroll_x = 0.0;
    mouse_.scroll_y = 0.0;
}

bool Window::key_pressed(int key) const noexcept {
    if (!window_) {
        return false;
    }

    return glfwGetKey(window_, key) == GLFW_PRESS;
}

GLFWwindow* Window::native_handle() const noexcept {
    return window_;
}

bool Window::is_glfw_available() noexcept {
    return g_glfw_initialized;
}

void Window::ensure_glfw_initialized() {
    if (g_glfw_initialized) {
        return;
    }

    enable_per_monitor_dpi_awareness();

    if (glfwInit() != GLFW_TRUE) {
        throw std::runtime_error("Window: failed to initialize GLFW");
    }

    g_glfw_initialized = true;
}

void Window::release_glfw() {
    if (!g_glfw_initialized) {
        return;
    }

    if (g_window_count <= 0) {
        glfwTerminate();
        g_window_count = 0;
        g_glfw_initialized = false;
    }
}

void Window::framebuffer_size_callback(
    GLFWwindow* native_window,
    int width,
    int height
) {
    Window* window = from_native(native_window);
    if (!window) {
        return;
    }

    window->framebuffer_size_.width = safe_u32_from_int(width);
    window->framebuffer_size_.height = safe_u32_from_int(height);
    window->framebuffer_resized_ = true;
}

void Window::cursor_position_callback(
    GLFWwindow* native_window,
    double x,
    double y
) {
    Window* window = from_native(native_window);
    if (!window) {
        return;
    }

    window->mouse_.last_x = window->mouse_.x;
    window->mouse_.last_y = window->mouse_.y;

    window->mouse_.x = x;
    window->mouse_.y = y;

    window->mouse_.delta_x += window->mouse_.x - window->mouse_.last_x;
    window->mouse_.delta_y += window->mouse_.y - window->mouse_.last_y;
}

void Window::mouse_button_callback(
    GLFWwindow* native_window,
    int button,
    int action,
    int
) {
    Window* window = from_native(native_window);
    if (!window) {
        return;
    }

    const bool pressed = action == GLFW_PRESS;

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        window->mouse_.left_pressed = pressed;
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        window->mouse_.right_pressed = pressed;
    } else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        window->mouse_.middle_pressed = pressed;
    }
}

void Window::scroll_callback(
    GLFWwindow* native_window,
    double xoffset,
    double yoffset
) {
    Window* window = from_native(native_window);
    if (!window) {
        return;
    }

    window->mouse_.scroll_x += xoffset;
    window->mouse_.scroll_y += yoffset;
}

Window* Window::from_native(GLFWwindow* native_window) noexcept {
    if (!native_window) {
        return nullptr;
    }

    return static_cast<Window*>(glfwGetWindowUserPointer(native_window));
}

void Window::update_window_size() noexcept {
    if (!window_) {
        return;
    }

    int width = 0;
    int height = 0;

    glfwGetWindowSize(window_, &width, &height);

    window_size_.width = safe_u32_from_int(width);
    window_size_.height = safe_u32_from_int(height);
}

void Window::update_framebuffer_size() noexcept {
    if (!window_) {
        return;
    }

    int width = 0;
    int height = 0;

    glfwGetFramebufferSize(window_, &width, &height);

    framebuffer_size_.width = safe_u32_from_int(width);
    framebuffer_size_.height = safe_u32_from_int(height);
}

} // namespace gs3d::platform
