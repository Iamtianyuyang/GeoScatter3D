#include "control/ComponentRegistry.hpp"
#include "control/MenuComponents.hpp"
#include "control/PanelComponents.hpp"
#include "control/ToolbarComponents.hpp"
#include "app/AppState.hpp"
#include "app/UiActions.hpp"
#include "ui/PanelRegistry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <set>
#include <string>

using gs3d::app::AppState;
using gs3d::app::UiActions;
using gs3d::control::Component;
using gs3d::control::ComponentError;
using gs3d::control::ComponentRegistry;
using gs3d::control::ComponentType;
using gs3d::control::make_menu_components;
using gs3d::control::make_overlay_components;
using gs3d::control::make_panel_components;
using gs3d::control::make_status_components;
using gs3d::control::make_toolbar_components;
using gs3d::control::make_gizmo_components;
using gs3d::control::make_canvas_components;
using gs3d::control::panel_component_id;
using gs3d::ui::PanelId;

namespace {

// 构造完整注册表（与 ControlPlaneSession 相同的装配）。
ComponentRegistry make_full_registry(AppState& state) {
    ComponentRegistry registry;
    UiActions temp_actions;
    for (auto& component : make_panel_components(state)) {
        registry.register_component(std::move(component));
    }
    for (auto& component : make_menu_components(state, temp_actions)) {
        registry.register_component(std::move(component));
    }
    for (auto& component : make_toolbar_components(state, temp_actions)) {
        registry.register_component(std::move(component));
    }
    for (auto& component : make_status_components(state)) {
        registry.register_component(std::move(component));
    }
    for (auto& component : make_overlay_components(state)) {
        registry.register_component(std::move(component));
    }
    for (auto& component : make_gizmo_components(state, temp_actions)) {
        registry.register_component(std::move(component));
    }
    for (auto& component : make_canvas_components(state, temp_actions)) {
        registry.register_component(std::move(component));
    }
    return registry;
}

} // namespace

// ── 面板注册表 1:1 映射测试 ──────────────────────────────────────

TEST_CASE("Panel registry maps 1:1 to control components", "[component_registry]") {
    AppState state;
    const ComponentRegistry registry = make_full_registry(state);

    for (int i = 0; i < gs3d::ui::kPanelCount; ++i) {
        const auto id = static_cast<PanelId>(i);
        const std::string component_id = panel_component_id(id);
        REQUIRE_FALSE(component_id.empty());
        const Component* component = registry.find(component_id);
        REQUIRE(component != nullptr);
        CHECK(component->info().type == ComponentType::kPanel);
        CHECK(component->info().debug == gs3d::ui::is_debug_panel(id));
        CHECK(component->info().name == gs3d::ui::panel_name(id));
    }
    CHECK(panel_component_id(static_cast<PanelId>(gs3d::ui::kPanelCount)).empty());
}

// ── 重复注册被拒绝 ─────────────────────────────────────────────

TEST_CASE("Duplicate component id registration is rejected", "[component_registry]") {
    AppState state;
    ComponentRegistry registry;
    for (auto& component : make_panel_components(state)) {
        registry.register_component(std::move(component));
    }
    REQUIRE_THROWS_AS(
        [&] {
            for (auto& component : make_panel_components(state)) {
                registry.register_component(std::move(component));
            }
        }(),
        std::invalid_argument
    );
}

// ── 未注册组件由构造失败 ──────────────────────────────────────

TEST_CASE("Unregistered component fails by construction", "[component_registry]") {
    AppState state;
    ComponentRegistry registry = make_full_registry(state);

    CHECK(registry.find("panel.nonexistent") == nullptr);
    REQUIRE_THROWS_AS(
        registry.execute("panel.nonexistent", "toggle", nlohmann::json::object()),
        ComponentError
    );
    REQUIRE_THROWS_AS(
        registry.execute("panel.dataset", "no_such_command", nlohmann::json::object()),
        ComponentError
    );
}

// ── 面板组件命令修改共享状态 ──────────────────────────────────

TEST_CASE("Panel component commands mutate the shared UI state", "[component_registry]") {
    AppState state;
    ComponentRegistry registry = make_full_registry(state);

    auto* dataset = registry.find("panel.dataset");
    REQUIRE(dataset != nullptr);
    CHECK(state.panels.dataset == true);

    const auto toggled = dataset->execute("toggle", nlohmann::json::object());
    CHECK(state.panels.dataset == false);
    CHECK(toggled["visible"] == false);

    const auto set = dataset->execute(
        "set_value",
        nlohmann::json{{"value", true}}
    );
    CHECK(state.panels.dataset == true);
    CHECK(set["visible"] == true);

    dataset->execute("set_visible", nlohmann::json{{"visible", false}});
    CHECK(state.panels.dataset == false);

    const auto state_json = dataset->execute("get_state", nlohmann::json::object());
    CHECK(state_json["id"] == "panel.dataset");
    CHECK(state_json["type"] == "panel");
    CHECK(state_json["visible"] == false);
}

// ── 面板组件拒绝畸形参数 ──────────────────────────────────────

TEST_CASE("Panel component rejects malformed params", "[component_registry]") {
    AppState state;
    ComponentRegistry registry = make_full_registry(state);
    auto* dataset = registry.find("panel.dataset");
    REQUIRE(dataset != nullptr);
    REQUIRE_THROWS_AS(
        dataset->execute("set_value", nlohmann::json{{"value", "yes"}}),
        ComponentError
    );
}

// ── 菜单组件命令修改共享状态 ──────────────────────────────────

TEST_CASE("Menu components modify shared AppState", "[component_registry]") {
    AppState state;
    ComponentRegistry registry = make_full_registry(state);

    // 测试截图菜单
    auto* screenshot = registry.find("menu.file.screenshot");
    REQUIRE(screenshot != nullptr);
    REQUIRE_NOTHROW(screenshot->execute("click", nlohmann::json::object()));
    CHECK(state.control_actions.screenshot_requested == true);

    // 测试欢迎页菜单
    state.control_actions = AppState::ControlActions{};
    auto* welcome = registry.find("menu.help.welcome");
    REQUIRE(welcome != nullptr);
    REQUIRE_NOTHROW(welcome->execute("click", nlohmann::json::object()));
    CHECK(state.control_actions.show_welcome_requested == true);

    // 测试快捷键总览菜单
    state.control_actions = AppState::ControlActions{};
    auto* shortcuts = registry.find("menu.help.shortcuts");
    REQUIRE(shortcuts != nullptr);
    REQUIRE_NOTHROW(shortcuts->execute("click", nlohmann::json::object()));
    CHECK(state.ui_chrome.shortcut_overlay_open == true);
}

// ── 工具栏组件命令修改共享状态 ────────────────────────────────

TEST_CASE("Toolbar components modify shared AppState", "[component_registry]") {
    AppState state;
    ComponentRegistry registry = make_full_registry(state);

    // 测量按钮
    auto* measure = registry.find("toolbar.measure");
    REQUIRE(measure != nullptr);
    REQUIRE_NOTHROW(measure->execute("click", nlohmann::json::object()));
    auto& meas = gs3d::app::measurement_for_view(state, state.active_viewport_index);
    CHECK(meas.measure_mode_active() == true);

    // 联动相机按钮
    auto* link = registry.find("toolbar.link_camera");
    REQUIRE(link != nullptr);
    REQUIRE_NOTHROW(link->execute("click", nlohmann::json::object()));
    bool any_linked = false;
    for (const auto& v : state.render_views) {
        if (v.camera_linked) { any_linked = true; break; }
    }
    // 没有视口时不会有变化，但命令应该成功
    REQUIRE_NOTHROW(link->execute("get_state", nlohmann::json::object()));
}

TEST_CASE("Runtime render settings queue a native render command", "[component_registry]") {
    AppState state;
    ComponentRegistry registry = make_full_registry(state);
    auto* settings = registry.find("runtime.render_settings");
    REQUIRE(settings != nullptr);

    const auto response = settings->execute(
        "set",
        nlohmann::json{
            {"point_size", 3.5},
            {"height_exaggeration", 2.0},
            {"colormap_index", 6},
            {"point_shape", 1}
        }
    );

    CHECK(response["queued"] == true);
    REQUIRE(state.control_actions.render_settings_commands.size() == 1);
    const auto& command = state.control_actions.render_settings_commands.front();
    CHECK(command.point_size_changed == true);
    CHECK(command.point_size == 3.5f);
    CHECK(command.height_exag_changed == true);
    CHECK(command.height_exag == 2.0f);
    CHECK(command.colormap_changed == true);
    CHECK(command.colormap_index == 6);
    CHECK(command.point_shape_changed == true);
    CHECK(command.point_shape == 1);
}

TEST_CASE("Runtime camera queues a native viewport frame", "[component_registry]") {
    AppState state;
    ComponentRegistry registry = make_full_registry(state);
    auto* camera = registry.find("runtime.camera");
    REQUIRE(camera != nullptr);

    const auto response = camera->execute(
        "input",
        nlohmann::json{{"rotate_delta_x", 12.0}, {"rotate_delta_y", -4.0}}
    );

    CHECK(response["queued"] == true);
    REQUIRE(state.control_actions.viewport_frames.size() == 1);
    const auto& frame = state.control_actions.viewport_frames.front();
    CHECK(frame.rotate == true);
    CHECK(frame.pan == false);
    CHECK(frame.mouse_delta_x == 12.0f);
    CHECK(frame.mouse_delta_y == -4.0f);
}

TEST_CASE("Runtime dataset export queues a complete native export", "[component_registry]") {
    AppState state;
    ComponentRegistry registry = make_full_registry(state);
    auto* exporter = registry.find("runtime.dataset_export");
    REQUIRE(exporter != nullptr);

    const auto response = exporter->execute(
        "export",
        nlohmann::json{{"output_path", "tmp/export.csv"}, {"format", "csv"}}
    );

    CHECK(response["queued"] == true);
    CHECK(response["request_id"] == 1);
    REQUIRE(state.control_actions.dataset_export_commands.size() == 1);
    const auto& command = state.control_actions.dataset_export_commands.front();
    CHECK(command.request_id == 1);
    CHECK(command.output_path == "tmp/export.csv");
    CHECK(command.format == "csv");
    REQUIRE_THROWS_AS(
        exporter->execute(
            "export",
            nlohmann::json{
                {"output_path", "tmp/second-export.csv"},
                {"format", "csv"}
            }
        ),
        ComponentError
    );
}

// ── Overlay 组件 toggle/set_visible ───────────────────────────

TEST_CASE("Overlay components support toggle and set_visible", "[component_registry]") {
    AppState state;
    ComponentRegistry registry = make_full_registry(state);

    auto* overlay = registry.find("overlay.shortcut");
    REQUIRE(overlay != nullptr);

    // 初始状态应为 false
    auto initial = overlay->execute("get_state", nlohmann::json::object());
    CHECK(initial["visible"] == false);

    // toggle
    auto toggled = overlay->execute("toggle", nlohmann::json::object());
    CHECK(toggled["visible"] == true);
    CHECK(state.ui_chrome.shortcut_overlay_open == true);

    // set_visible false
    auto hidden = overlay->execute("set_visible", nlohmann::json{{"visible", false}});
    CHECK(hidden["visible"] == false);
    CHECK(state.ui_chrome.shortcut_overlay_open == false);
}

// ── 状态栏组件只读 ───────────────────────────────────────────

TEST_CASE("Status bar component is read-only", "[component_registry]") {
    AppState state;
    ComponentRegistry registry = make_full_registry(state);

    auto* status = registry.find("status.bar");
    REQUIRE(status != nullptr);

    auto state_json = status->execute("get_state", nlohmann::json::object());
    CHECK(state_json.contains("fps"));
    CHECK(state_json.contains("visible_points"));
    CHECK(state_json.contains("gpu_memory_bytes"));
    CHECK(state_json.contains("camera_position"));

    // 不支持 click
    REQUIRE_THROWS_AS(
        status->execute("click", nlohmann::json::object()),
        ComponentError
    );
}

// ── 100% 覆盖驱动测试 ────────────────────────────────────────

TEST_CASE("Every non-debug component capability is driven to success (100%)",
          "[component_registry][coverage]") {
    AppState state;
    ComponentRegistry registry = make_full_registry(state);

    std::size_t driven_capabilities = 0;
    std::size_t total_capabilities = 0;
    std::set<std::string> driven_ids;

    for (const auto& component : registry.components()) {
        for (const auto& capability : component->capabilities()) {
            ++total_capabilities;
            if (component->info().debug) {
                continue;
            }
            CAPTURE(component->info().id, capability.command);
            nlohmann::json params = nlohmann::json::object();
            if (capability.requires_param) {
                if (capability.command == "set_value") {
                    params["value"] = true;
                } else if (capability.command == "set_visible") {
                    params["visible"] = true;
                } else if (capability.command == "click" &&
                           component->info().id == "gizmo.navigation") {
                    params["axis"] = 0;
                } else if (capability.command == "set" &&
                           component->info().id == "runtime.render_settings") {
                    params["point_size"] = 2.0;
                } else if (capability.command == "input" &&
                           component->info().id == "runtime.camera") {
                    params["scroll_y"] = 1.0;
                } else if (capability.command == "input" &&
                           component->info().id == "runtime.pick") {
                    params["kind"] = "measure";
                } else if (capability.command == "export" &&
                           component->info().id == "runtime.dataset_export") {
                    params["output_path"] = "tmp/export.ply";
                    params["format"] = "ply";
                }
            }
            nlohmann::json result;
            REQUIRE_NOTHROW(result = component->execute(capability.command, params));
            REQUIRE(result.is_object());
            ++driven_capabilities;
            driven_ids.insert(component->info().id);
        }
    }

    // 所有非调试组件的每项能力都必须被驱动
    CHECK(driven_capabilities >= 40);  // 至少 40 项能力被驱动
    CHECK(driven_ids.count("panel.dataset") == 1);
    CHECK(driven_ids.count("menu.file.open") == 1);
    CHECK(driven_ids.count("toolbar.open") == 1);
    CHECK(driven_ids.count("status.bar") == 1);
    CHECK(driven_ids.count("overlay.shortcut") == 1);
    CHECK(driven_ids.count("gizmo.navigation") == 1);
    CHECK(driven_ids.count("canvas.viewport") == 1);
    CHECK(driven_ids.count("runtime.render_settings") == 1);
    CHECK(driven_ids.count("runtime.camera") == 1);
    CHECK(driven_ids.count("runtime.pick") == 1);
    CHECK(driven_ids.count("runtime.diagnostics") == 1);
    CHECK(driven_ids.count("runtime.dataset_export") == 1);
}

// ── list_json 暴露稳定 ID 和能力 ─────────────────────────────

TEST_CASE("list_json exposes stable ids and capabilities", "[component_registry]") {
    AppState state;
    ComponentRegistry registry = make_full_registry(state);
    const nlohmann::json listing = registry.list_json();
    REQUIRE(listing.is_array());

    std::set<std::string> ids;
    for (const auto& entry : listing) {
        CHECK(entry.contains("id"));
        CHECK(entry.contains("type"));
        CHECK(entry.contains("debug"));
        CHECK(entry["capabilities"].is_array());
        ids.insert(entry["id"].get<std::string>());
    }

    // 检查关键组件都存在
    CHECK(ids.count("panel.dataset") == 1);
    CHECK(ids.count("panel.render_settings") == 1);
    CHECK(ids.count("menu.file.open") == 1);
    CHECK(ids.count("menu.view.theme") == 1);
    CHECK(ids.count("toolbar.open") == 1);
    CHECK(ids.count("toolbar.measure") == 1);
    CHECK(ids.count("status.bar") == 1);
    CHECK(ids.count("overlay.shortcut") == 1);
    CHECK(ids.count("overlay.panel_palette") == 1);
    CHECK(ids.count("gizmo.navigation") == 1);
    CHECK(ids.count("canvas.viewport") == 1);
    CHECK(ids.count("runtime.render_settings") == 1);
    CHECK(ids.count("runtime.camera") == 1);
    CHECK(ids.count("runtime.pick") == 1);
    CHECK(ids.count("runtime.diagnostics") == 1);
    CHECK(ids.count("runtime.dataset_export") == 1);
    // 注意：viewport.main 在 ViewerAppControlPlane.cpp 中创建，
    // 测试中的 make_full_registry 不包含它

    // 总组件数检查：8 panels + 9 menus + 11 toolbars + 1 status + 2 overlays + 1 gizmo + 1 canvas = 33
    CHECK(ids.size() >= 33);
}

// ── 主题组件支持 set_value ───────────────────────────────────

TEST_CASE("Theme component supports set_value command", "[component_registry]") {
    AppState state;
    ComponentRegistry registry = make_full_registry(state);

    auto* theme = registry.find("menu.view.theme");
    REQUIRE(theme != nullptr);

    // 切换到下一个主题
    REQUIRE_NOTHROW(theme->execute("click", nlohmann::json::object()));
    CHECK(state.control_actions.theme_change_requested == true);

    // set_value 到指定主题（通过索引）
    state.control_actions = AppState::ControlActions{};
    auto result = theme->execute("set_value", nlohmann::json{{"value", 1}});
    CHECK(state.control_actions.theme_change_requested == true);
    CHECK(state.control_actions.theme_id == 1);
}
