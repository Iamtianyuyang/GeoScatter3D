#include "app/ViewerAppControlPlane.hpp"

#include "app/AppState.hpp"
#include "app/ScreenshotService.hpp"
#include "control/MenuComponents.hpp"
#include "control/PanelComponents.hpp"
#include "control/ToolbarComponents.hpp"
#include "render/VulkanSwapchain.hpp"
#include "util/Log.hpp"

#include <string>
#include <utility>

namespace gs3d::app {

namespace {

/*
 * viewport.main：非面板组件契约示例 —— 只读渲染/运行状态。
 * 下一阶段每个可交互 UI 元素按 docs/component-registry.md 接入。
 */
class ViewportStateComponent final : public control::Component {
public:
    explicit ViewportStateComponent(const AppState& app_state)
        : app_state_(app_state) {}

    [[nodiscard]] const control::ComponentInfo& info() const noexcept override {
        return info_;
    }

    [[nodiscard]] const std::vector<control::CommandSpec>&
    capabilities() const noexcept override {
        return capabilities_;
    }

    [[nodiscard]] nlohmann::json get_state() override {
        const int active = resolve_viewport_index(
            app_state_,
            app_state_.active_viewport_index
        );
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        if (active >= 0 &&
            active < static_cast<int>(app_state_.render_views.size())) {
            width = app_state_.render_views[static_cast<std::size_t>(active)]
                        .image_width;
            height = app_state_.render_views[static_cast<std::size_t>(active)]
                         .image_height;
        }
        return {
            {"id", info_.id},
            {"viewport_count", app_state_.render_views.size()},
            {"active_viewport_index", active},
            {"width", width},
            {"height", height},
            {"fps", app_state_.status_bar.fps},
            {"visible_points", app_state_.status_bar.visible_points},
            {"loaded_tiles", app_state_.status_bar.loaded_tiles},
            {"pending_tiles", app_state_.status_bar.pending_tiles},
            {"gpu_memory_bytes", app_state_.status_bar.gpu_memory_bytes},
            {"camera_position", app_state_.status_bar.camera_position},
            {"ready_state", app_state_.status_bar.ready_state}
        };
    }

    [[nodiscard]] nlohmann::json execute(
        const std::string& command,
        const nlohmann::json& params
    ) override {
        static_cast<void>(params);
        if (command == "get_state") {
            return get_state();
        }
        throw control::ComponentError(
            -32601,
            "viewport.main is read-only; unknown command: " + command
        );
    }

private:
    const AppState& app_state_;
    control::ComponentInfo info_{
        .id = "viewport.main",
        .name = "主视口",
        .description = "当前活动三维视口的渲染状态",
        .type = control::ComponentType::kViewport,
        .debug = false
    };
    std::vector<control::CommandSpec> capabilities_{
        {"get_state", "查询视口渲染状态", false}
    };
};

} // namespace

ControlPlaneSession::ControlPlaneSession(
    const ViewerControlPlaneConfig& config,
    AppState& app_state,
    ScreenshotService& screenshot_service,
    const render::VulkanSwapchain& swapchain
)
    : registry_(),
      plane_(
          registry_,
          control::CaptureCallbacks{
              .arm_capture = [&app_state, &screenshot_service, &swapchain]() {
                  return screenshot_service.request_control_capture(
                      app_state,
                      swapchain
                  );
              },
              .take_png = [&screenshot_service]() {
                  return screenshot_service.take_control_png();
              }
          }
      )
{
    // TIA-111：全量组件注册。
    // 面板组件与 UI 绘制共享同一份 AppState::panels；未注册的组件
    // 在控制面上「构造上就不存在」—— 命令直接报错，不依赖白名单自觉。
    // 新增面板不注册 → ComponentRegistryTests 的 1:1 映射测试立即失败。
    
    // 1. 面板组件（8 张面板，含 2 张调试面板）
    gs3d::app::UiActions temp_actions;  // 菜单/工具栏组件不使用 UiActions
    for (auto& component : control::make_panel_components(app_state)) {
        registry_.register_component(std::move(component));
    }
    // 2. 视口状态组件（只读）
    registry_.register_component(
        std::make_unique<ViewportStateComponent>(app_state)
    );
    // 3. 菜单组件（9 个菜单项）
    for (auto& component : control::make_menu_components(app_state, temp_actions)) {
        registry_.register_component(std::move(component));
    }
    // 4. 工具栏组件（6 个按钮）
    for (auto& component : control::make_toolbar_components(app_state, temp_actions)) {
        registry_.register_component(std::move(component));
    }
    // 5. 状态栏组件（只读）
    for (auto& component : control::make_status_components(app_state)) {
        registry_.register_component(std::move(component));
    }
    // 6. Overlay 组件（2 个：快捷键总览、面板命令面板）
    for (auto& component : control::make_overlay_components(app_state)) {
        registry_.register_component(std::move(component));
    }
    // 7. Gizmo 组件（导航球）
    for (auto& component : control::make_gizmo_components(app_state, temp_actions)) {
        registry_.register_component(std::move(component));
    }
    // 8. Canvas 组件（视口画布）
    for (auto& component : control::make_canvas_components(app_state, temp_actions)) {
        registry_.register_component(std::move(component));
    }

    if (!config.enabled) {
        return;
    }
    std::string error;
    if (!plane_.start(config.port, error)) {
        gs3d::util::log::error() << "[CONTROL] " << error << '\n';
        return;
    }
    gs3d::util::log::info() << "[CONTROL] listening on 127.0.0.1:"
              << plane_.bound_port() << '\n';
}

ControlPlaneSession::~ControlPlaneSession() = default;

void ControlPlaneSession::poll() {
    plane_.poll();
}

bool ControlPlaneSession::quit_requested() const noexcept {
    return plane_.quit_requested();
}

bool ControlPlaneSession::running() const noexcept {
    return plane_.running();
}

const control::ComponentRegistry& ControlPlaneSession::registry() const noexcept {
    return registry_;
}

} // namespace gs3d::app
