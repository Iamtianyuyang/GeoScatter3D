#include "control/PanelComponents.hpp"

#include "app/AppState.hpp"
#include "ui/PanelRegistry.hpp"

#include <array>
#include <utility>

namespace gs3d::control {

namespace {

constexpr std::array<std::string_view, gs3d::ui::kPanelCount> kPanelComponentIds{{
    "panel.dataset",        // PanelId::kDataset
    "panel.render_settings",// PanelId::kRenderSettings
    "panel.performance",    // PanelId::kPerformance
    "panel.navigation_map", // PanelId::kNavigationMap
    "panel.measurement",    // PanelId::kMeasurement
    "panel.region_stats",   // PanelId::kRegionStats
    "panel.tile_inspector", // PanelId::kTileInspector
    "panel.lod_view",       // PanelId::kLodView
}};

class PanelComponent final : public Component {
public:
    explicit PanelComponent(gs3d::app::AppState& app_state, gs3d::ui::PanelId id)
        : app_state_(app_state), id_(id) {}

    [[nodiscard]] const ComponentInfo& info() const noexcept override {
        return info_;
    }

    [[nodiscard]] const std::vector<CommandSpec>& capabilities() const noexcept override {
        return capabilities_;
    }

    [[nodiscard]] nlohmann::json get_state() override {
        const bool* visible = gs3d::ui::panel_visibility(app_state_.panels, id_);
        return {
            {"id", info_.id},
            {"name", info_.name},
            {"description", info_.description},
            {"type", "panel"},
            {"debug", info_.debug},
            {"visible", visible != nullptr && *visible}
        };
    }

    [[nodiscard]] nlohmann::json execute(
        const std::string& command,
        const nlohmann::json& params
    ) override {
        if (command == "toggle") {
            gs3d::ui::toggle_panel(app_state_.panels, id_);
            return get_state();
        }
        if (command == "set_value" || command == "set_visible") {
            bool* visible = gs3d::ui::panel_visibility(app_state_.panels, id_);
            if (visible == nullptr) {
                throw ComponentError(-32602, "panel has no visibility state");
            }
            bool new_visible = false;
            if (params.is_object() && params.contains("value") &&
                params["value"].is_boolean()) {
                new_visible = params["value"].get<bool>();
            } else if (params.is_object() && params.contains("visible") &&
                       params["visible"].is_boolean()) {
                new_visible = params["visible"].get<bool>();
            } else {
                throw ComponentError(
                    -32602,
                    command + " requires {\"value\"|" +
                    "\"visible\": bool}"
                );
            }
            *visible = new_visible;
            return get_state();
        }
        if (command == "get_state") {
            return get_state();
        }
        throw ComponentError(
            -32601,
            "unknown command for panel: " + command
        );
    }

private:
    gs3d::app::AppState& app_state_;
    gs3d::ui::PanelId id_;
    ComponentInfo info_{
        .id = panel_component_id(id_),
        .name = std::string(gs3d::ui::panel_name(id_)),
        .description = std::string(
            gs3d::ui::kPanelRegistry[static_cast<std::size_t>(id_)].description
        ),
        .type = ComponentType::kPanel,
        .debug = gs3d::ui::is_debug_panel(id_)
    };
    std::vector<CommandSpec> capabilities_{
        {"toggle", "切换面板显示/隐藏", false},
        {"set_value", "设置面板显示状态（{\"value\": bool}）", true},
        {"set_visible", "设置面板显示状态（{\"visible\": bool}）", true},
        {"get_state", "查询面板状态（visible 等）", false}
    };
};

} // namespace

std::string panel_component_id(const gs3d::ui::PanelId id) {
    const int index = static_cast<int>(id);
    if (index < 0 || index >= gs3d::ui::kPanelCount) {
        return {};
    }
    return std::string(kPanelComponentIds[static_cast<std::size_t>(index)]);
}

std::vector<std::unique_ptr<Component>> make_panel_components(
    gs3d::app::AppState& app_state
) {
    std::vector<std::unique_ptr<Component>> components;
    components.reserve(gs3d::ui::kPanelCount);
    for (int i = 0; i < gs3d::ui::kPanelCount; ++i) {
        components.push_back(std::make_unique<PanelComponent>(
            app_state,
            static_cast<gs3d::ui::PanelId>(i)
        ));
    }
    return components;
}

} // namespace gs3d::control
