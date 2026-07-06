#include "app/WelcomeWindow.hpp"

#include "app/RecentProjects.hpp"
#include "gui/ImGuiLayer.hpp"
#include "gui/UiFonts.hpp"
#include "platform/Window.hpp"
#include "render/VulkanContext.hpp"
#include "render/VulkanRenderer.hpp"
#include "render/VulkanSwapchain.hpp"
#include "ui/SvgLogoTexture.hpp"
#include "ui/WelcomePage.hpp"

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace gs3d::app {

namespace {

float srgb_to_linear(float value)
{
    return value <= 0.04045f
        ? value / 12.92f
        : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

int preferred_outer_width(float ui_scale)
{
    constexpr int kBaseOuterWidth = 1024;
    constexpr int kMinimumOuterWidth = 1100;
    constexpr float kHeightToWidth = 0.618f;
    int target_width = std::max(
        kMinimumOuterWidth,
        static_cast<int>(std::lround(
            static_cast<float>(kBaseOuterWidth) * ui_scale
        ))
    );

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (monitor == nullptr) {
        return target_width;
    }
    int work_x = 0;
    int work_y = 0;
    int work_width = 0;
    int work_height = 0;
    glfwGetMonitorWorkarea(
        monitor,
        &work_x,
        &work_y,
        &work_width,
        &work_height
    );
    if (work_width <= 0 || work_height <= 0) {
        return target_width;
    }

    const int width_limit = static_cast<int>(
        std::floor(static_cast<float>(work_width) * 0.90f)
    );
    const int height_limit = static_cast<int>(
        std::floor(static_cast<float>(work_height) * 0.90f)
    );
    const int width_allowed_by_height = static_cast<int>(
        std::floor(
            static_cast<float>(height_limit) / kHeightToWidth
        )
    );
    return std::clamp(
        target_width,
        kMinimumOuterWidth,
        std::max(
            kMinimumOuterWidth,
            std::min(width_limit, width_allowed_by_height)
        )
    );
}

void apply_golden_outer_window_size(
    GLFWwindow* window,
    int outer_width
)
{
    if (window == nullptr) {
        return;
    }

    // Some desktop environments remember the previous maximized state for
    // windows with the same title. The welcome window must always start in
    // its explicit golden-ratio default geometry.
    glfwRestoreWindow(window);
    glfwPollEvents();

    constexpr float kHeightToWidth = 0.618f;
    const int outer_height = static_cast<int>(std::lround(
        static_cast<float>(outer_width) * kHeightToWidth
    ));

    int frame_left = 0;
    int frame_top = 0;
    int frame_right = 0;
    int frame_bottom = 0;
    glfwGetWindowFrameSize(
        window,
        &frame_left,
        &frame_top,
        &frame_right,
        &frame_bottom
    );

    const int client_width = std::max(
        1,
        outer_width - frame_left - frame_right
    );
    const int client_height = std::max(
        1,
        outer_height - frame_top - frame_bottom
    );
    glfwSetWindowSize(window, client_width, client_height);

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (monitor != nullptr) {
        int work_x = 0;
        int work_y = 0;
        int work_width = 0;
        int work_height = 0;
        glfwGetMonitorWorkarea(
            monitor,
            &work_x,
            &work_y,
            &work_width,
            &work_height
        );
        glfwSetWindowPos(
            window,
            work_x + (work_width - outer_width) / 2,
            work_y + (work_height - outer_height) / 2
        );
    }
}

} // namespace

WelcomeWindow::WelcomeWindow(WelcomeWindowConfig config)
    : config_(std::move(config))
{
}

WelcomeWindowResult WelcomeWindow::run()
{
    gs3d::platform::WindowConfig window_config;
    // Initial client size; it is adjusted below so the complete decorated
    // window (including the native title bar) has height / width ~= 0.618.
    window_config.width = 1200;
    window_config.height = 700;
    window_config.title = "GeoScatter3D — 欢迎";
    window_config.resizable = true;
    gs3d::platform::Window window(window_config);
    apply_golden_outer_window_size(window.native_handle(), 1200);

    gs3d::render::VulkanContextConfig context_config;
    context_config.enable_validation_layers =
        config_.enable_validation_layers;
    context_config.application_name = "GeoScatter3D Welcome";
    gs3d::render::VulkanContext context(window, context_config);
    gs3d::render::VulkanSwapchain swapchain(context, window);
    gs3d::render::VulkanRenderer renderer(context, swapchain);

    gs3d::gui::ImGuiLayer imgui;
    imgui.init(
        window.native_handle(),
        context,
        renderer,
        swapchain.image_count(),
        {},
        config_.ui_scale_multiplier
    );
    apply_golden_outer_window_size(
        window.native_handle(),
        preferred_outer_width(gs3d::gui::ui_fonts().ui_scale)
    );

    gs3d::render::ClearColor clear_color;
    clear_color.r = srgb_to_linear(30.0f / 255.0f);
    clear_color.g = srgb_to_linear(30.0f / 255.0f);
    clear_color.b = srgb_to_linear(30.0f / 255.0f);
    clear_color.a = 1.0f;
    renderer.set_clear_color(clear_color);

    gs3d::ui::SvgLogoTexture logo(
        context.device(),
        context.physical_device(),
        context.graphics_queue(),
        renderer.command_pool(),
        "assets/icon.svg",
        128
    );

    gs3d::ui::WelcomePageModel model;
    model.logo_texture = logo.descriptor();
    model.current_path = config_.current_path;
    model.recent_projects = config_.recent_projects;

    WelcomeWindowResult result;
    while (!window.should_close()) {
        window.poll_events();
        imgui.begin_frame();
        const auto action = gs3d::ui::draw_welcome_page(
            model,
            gs3d::gui::ui_fonts().ui_scale
        );

        switch (action.kind) {
        case gs3d::ui::WelcomePageActionKind::ContinueCurrent:
            result.kind = WelcomeWindowResultKind::ContinueCurrent;
            window.request_close();
            break;
        case gs3d::ui::WelcomePageActionKind::OpenProject:
            result.kind = WelcomeWindowResultKind::OpenProject;
            result.path = action.path;
            window.request_close();
            break;
        case gs3d::ui::WelcomePageActionKind::OpenRawData:
            result.kind = WelcomeWindowResultKind::OpenRawData;
            result.path = action.path;
            window.request_close();
            break;
        case gs3d::ui::WelcomePageActionKind::ClearRecent:
            clear_recent_projects();
            model.recent_projects.clear();
            break;
        case gs3d::ui::WelcomePageActionKind::None:
            break;
        }

        renderer.draw_frame(
            window,
            [&](VkCommandBuffer command_buffer) {
                imgui.render(command_buffer);
            }
        );
        imgui.render_platform_windows();
        imgui.discard_frame();
    }

    vkDeviceWaitIdle(context.device());
    return result;
}

} // namespace gs3d::app
