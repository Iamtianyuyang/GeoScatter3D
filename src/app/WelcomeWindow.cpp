#include "app/WelcomeWindow.hpp"

#include "app/AppConfig.hpp"
#include "app/RecentProjects.hpp"
#include "app/UserPreferences.hpp"
#include "ui/ImGuiLayer.hpp"
#include "ui/UiFonts.hpp"
#include "platform/CpuInfo.hpp"
#include "platform/Window.hpp"
#include "render/VulkanContext.hpp"
#include "render/VulkanRenderer.hpp"
#include "render/VulkanSwapchain.hpp"
#include "ui/SvgLogoTexture.hpp"
#include "ui/WelcomePage.hpp"

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>

namespace gs3d::app {

namespace {

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

struct ProjectPreprocessRuntimeSnapshot {
    gs3d::ui::ProjectPreprocessView view;
    bool active = false;
    bool finished = false;
    bool succeeded = false;
};

class ProjectPreprocessRuntime {
public:
    void begin(const gs3d::ui::WelcomePageAction& action)
    {
        std::scoped_lock lock(mutex_);
        active_ = true;
        finished_ = false;
        succeeded_ = false;
        started_at_ = std::chrono::steady_clock::now();
        view_ = {};
        view_.source_path = action.path;
        view_.project_name = action.project_name;
        view_.thread_count = action.thread_count;
        view_.stage = "准备项目";
        view_.detail = "正在检查源数据与项目输出路径…";
        view_.progress = 0.02f;
    }

    void report(const ProjectPreprocessProgress& progress)
    {
        std::scoped_lock lock(mutex_);
        view_.progress =
            std::clamp(progress.fraction, 0.0f, 1.0f);
        view_.stage_index = std::min(3u, progress.stage_index);
        view_.stage = progress.stage;
        view_.detail = progress.detail;
    }

    void succeed()
    {
        std::scoped_lock lock(mutex_);
        view_.progress = 1.0f;
        view_.stage_index = 3;
        view_.stage = "预处理完成";
        view_.detail = "项目数据已写入，即将进入三维视图。";
        finished_ = true;
        succeeded_ = true;
    }

    void fail(std::string message)
    {
        std::scoped_lock lock(mutex_);
        view_.failed = true;
        view_.error_message = std::move(message);
        finished_ = true;
        succeeded_ = false;
    }

    void reset()
    {
        std::scoped_lock lock(mutex_);
        active_ = false;
        finished_ = false;
        succeeded_ = false;
        view_ = {};
    }

    [[nodiscard]]
    ProjectPreprocessRuntimeSnapshot snapshot() const
    {
        std::scoped_lock lock(mutex_);
        auto view = view_;
        if (active_) {
            view.elapsed_seconds =
                std::chrono::duration<double>(
                    std::chrono::steady_clock::now() -
                    started_at_
                ).count();
        }
        return {
            .view = std::move(view),
            .active = active_,
            .finished = finished_,
            .succeeded = succeeded_
        };
    }

private:
    mutable std::mutex mutex_;
    std::chrono::steady_clock::time_point started_at_{};
    gs3d::ui::ProjectPreprocessView view_;
    bool active_ = false;
    bool finished_ = false;
    bool succeeded_ = false;
};

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
    context_config.preferred_gpu = config_.preferred_gpu;
    gs3d::render::VulkanContext context(window, context_config);
    gs3d::render::VulkanSwapchain swapchain(context, window);
    gs3d::render::VulkanRenderer renderer(context, swapchain);

    gs3d::ui::ImGuiLayer imgui;
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
    clear_color.r = 30.0f / 255.0f;
    clear_color.g = 30.0f / 255.0f;
    clear_color.b = 30.0f / 255.0f;
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
    model.logo_texture =
        reinterpret_cast<TextureHandle>(logo.descriptor());
    model.current_path = config_.current_path;
    model.recent_projects = config_.recent_projects;
    const auto logical_threads =
        gs3d::platform::logical_cpu_thread_count();
    const auto physical_cores =
        gs3d::platform::physical_cpu_core_count();
    model.new_project_dialog.thread_count = physical_cores;
    model.new_project_dialog.recommended_thread_count =
        physical_cores;
    model.new_project_dialog.max_thread_count =
        logical_threads;

    // Populate GPU list from the already-created Vulkan context.
    model.gpu_list = context.gpu_list();
    model.active_gpu_index = context.active_gpu_index();
    model.preferred_gpu = config_.preferred_gpu;
    model.on_preferred_gpu_changed =
        [this](const std::string& gpu) {
            config_.preferred_gpu = gpu;
            gs3d::app::save_preferred_gpu_preference(gpu);
        };

    WelcomeWindowResult result;
    WelcomeWindowResult pending_project_result;
    ProjectPreprocessRuntime preprocess_runtime;
    std::thread preprocess_thread;
    for (;;) {
        if (window.should_close()) {
            const auto close_snapshot =
                preprocess_runtime.snapshot();
            if (close_snapshot.active &&
                !close_snapshot.finished) {
                // The preprocessing pipeline is not cancellable. Keep the
                // styled progress surface alive instead of hiding the
                // application while a background join blocks.
                glfwSetWindowShouldClose(
                    window.native_handle(),
                    GLFW_FALSE
                );
            } else {
                break;
            }
        }

        window.poll_events();
        imgui.begin_frame();

        const auto preprocess_snapshot =
            preprocess_runtime.snapshot();
        if (preprocess_snapshot.active) {
            const auto progress_action =
                gs3d::ui::draw_project_preprocess_page(
                    model,
                    preprocess_snapshot.view,
                    gs3d::gui::ui_fonts().ui_scale
                );
            if (preprocess_snapshot.finished &&
                preprocess_snapshot.succeeded) {
                if (preprocess_thread.joinable()) {
                    preprocess_thread.join();
                }
                result = pending_project_result;
                result.preprocessed = true;
                window.request_close();
            } else if (
                preprocess_snapshot.finished &&
                progress_action ==
                    gs3d::ui::
                        ProjectPreprocessPageAction::
                            BackToWelcome
            ) {
                if (preprocess_thread.joinable()) {
                    preprocess_thread.join();
                }
                preprocess_runtime.reset();
                model.new_project_dialog.active = true;
                model.new_project_dialog.should_open = true;
            }
        } else {
            const auto action = gs3d::ui::draw_welcome_page(
                model,
                gs3d::gui::ui_fonts().ui_scale
            );

            switch (action.kind) {
            case gs3d::ui::WelcomePageActionKind::ContinueCurrent:
                result.kind =
                    WelcomeWindowResultKind::ContinueCurrent;
                // 带上卡片实际显示的路径，调用方按它打开项目——否则会
                // 回落到 viewer.toml 里可能过期/相对的 bundle_dir。
                result.path = config_.current_path;
                window.request_close();
                break;
            case gs3d::ui::WelcomePageActionKind::OpenProject:
                result.kind =
                    WelcomeWindowResultKind::OpenProject;
                result.path = action.path;
                window.request_close();
                break;
            case gs3d::ui::WelcomePageActionKind::NewProject:
                pending_project_result.kind =
                    WelcomeWindowResultKind::NewProject;
                pending_project_result.path = action.path;
                pending_project_result.project_name =
                    action.project_name;
                pending_project_result.thread_count =
                    action.thread_count;

                if (!config_.preprocess_new_project) {
                    result = pending_project_result;
                    window.request_close();
                    break;
                }

                preprocess_runtime.begin(action);
                preprocess_thread = std::thread(
                    [
                        task = config_.preprocess_new_project,
                        source_path = action.path,
                        project_name = action.project_name,
                        thread_count = action.thread_count,
                        &preprocess_runtime
                    ] {
                        try {
                            task(
                                source_path,
                                project_name,
                                thread_count,
                                [&preprocess_runtime](
                                    const ProjectPreprocessProgress&
                                        progress
                                ) {
                                    preprocess_runtime.report(
                                        progress
                                    );
                                }
                            );
                            preprocess_runtime.succeed();
                        } catch (const std::exception& error) {
                            preprocess_runtime.fail(error.what());
                        } catch (...) {
                            preprocess_runtime.fail(
                                "预处理失败：发生未知错误"
                            );
                        }
                    }
                );
                break;
            case gs3d::ui::WelcomePageActionKind::ClearRecent:
                clear_recent_projects();
                model.recent_projects.clear();
                break;
            case gs3d::ui::WelcomePageActionKind::None:
                break;
            }
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

    if (preprocess_thread.joinable()) {
        preprocess_thread.join();
    }
    vkDeviceWaitIdle(context.device());
    return result;
}

} // namespace gs3d::app
