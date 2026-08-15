#include "control/ComponentRegistry.hpp"
#include "control/PanelComponents.hpp"
#include "app/AppState.hpp"
#include "ui/PanelRegistry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <set>
#include <string>

using gs3d::app::AppState;
using gs3d::control::Component;
using gs3d::control::ComponentError;
using gs3d::control::ComponentRegistry;
using gs3d::control::ComponentType;
using gs3d::control::make_panel_components;
using gs3d::control::panel_component_id;
using gs3d::ui::PanelId;

namespace {

// 构造一张包含全部面板组件的注册表（与 ControlPlaneSession 相同的装配）。
ComponentRegistry make_registry(AppState& state) {
    ComponentRegistry registry;
    for (auto& component : make_panel_components(state)) {
        registry.register_component(std::move(component));
    }
    return registry;
}

} // namespace

TEST_CASE("Panel registry maps 1:1 to control components", "[component_registry]") {
    AppState state;
    const ComponentRegistry registry = make_registry(state);

    // 结构性强绑定：UI 侧唯一面板清单（kPanelRegistry）里的每一张面板
    // 都必须有对应的控制组件 —— 新增面板不注册，此测试立即失败。
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

TEST_CASE("Unregistered component fails by construction", "[component_registry]") {
    AppState state;
    ComponentRegistry registry = make_registry(state);

    // 未注册即失败：注册表查不到 → 分发直接抛错，不存在"默认可用"路径。
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

TEST_CASE("Panel component commands mutate the shared UI state", "[component_registry]") {
    AppState state;
    ComponentRegistry registry = make_registry(state);

    // 初始可见性与 PanelVisibilityState 默认一致（UI 绘制读取的同一份状态）。
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

TEST_CASE("Panel component rejects malformed params", "[component_registry]") {
    AppState state;
    ComponentRegistry registry = make_registry(state);
    auto* dataset = registry.find("panel.dataset");
    REQUIRE(dataset != nullptr);
    REQUIRE_THROWS_AS(
        dataset->execute("set_value", nlohmann::json{{"value", "yes"}}),
        ComponentError
    );
}

TEST_CASE("Every non-debug component capability is driven to success (100%)",
          "[component_registry][coverage]") {
    AppState state;
    ComponentRegistry registry = make_registry(state);

    // 覆盖强校验（任务要求 100%，不是 90%）：遍历注册表，对每个非调试
    // 组件的每项能力真实执行一次并断言成功。参数按 requires_param 生成。
    std::size_t driven_capabilities = 0;
    for (const auto& component : registry.components()) {
        if (component->info().debug) {
            continue;
        }
        for (const auto& capability : component->capabilities()) {
            CAPTURE(component->info().id, capability.command);
            nlohmann::json params = nlohmann::json::object();
            if (capability.requires_param) {
                params["value"] = true;
            }
            nlohmann::json result;
            REQUIRE_NOTHROW(result = component->execute(capability.command, params));
            REQUIRE(result.is_object());
            CHECK(result.contains("id"));
            ++driven_capabilities;
        }
    }
    // 至少把全部 6 张业务面板 × 4 项能力都驱动过。
    CHECK(driven_capabilities >= 24);
}

TEST_CASE("list_json exposes stable ids and capabilities", "[component_registry]") {
    AppState state;
    ComponentRegistry registry = make_registry(state);
    const nlohmann::json listing = registry.list_json();
    REQUIRE(listing.is_array());

    bool found_dataset = false;
    for (const auto& entry : listing) {
        CHECK(entry.contains("id"));
        CHECK(entry.contains("type"));
        CHECK(entry.contains("debug"));
        CHECK(entry["capabilities"].is_array());
        if (entry["id"] == "panel.dataset") {
            found_dataset = true;
            CHECK(entry["type"] == "panel");
            CHECK(entry["debug"] == false);
            std::set<std::string> commands;
            for (const auto& capability : entry["capabilities"]) {
                commands.insert(capability["command"].get<std::string>());
            }
            CHECK(commands.count("toggle") == 1);
            CHECK(commands.count("set_value") == 1);
            CHECK(commands.count("set_visible") == 1);
            CHECK(commands.count("get_state") == 1);
        }
    }
    CHECK(found_dataset);
}
