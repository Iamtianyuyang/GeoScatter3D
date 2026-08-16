#include "control/ComponentRegistry.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace gs3d::control {

std::string_view component_type_name(const ComponentType type) noexcept {
    switch (type) {
    case ComponentType::kPanel:    return "panel";
    case ComponentType::kViewport: return "viewport";
    case ComponentType::kToolbar:  return "toolbar";
    case ComponentType::kMenu:     return "menu";
    case ComponentType::kButton:   return "button";
    case ComponentType::kSlider:   return "slider";
    case ComponentType::kDropdown: return "dropdown";
    case ComponentType::kCheckbox: return "checkbox";
    case ComponentType::kInput:    return "input";
    case ComponentType::kGizmo:    return "gizmo";
    case ComponentType::kStatus:   return "status";
    case ComponentType::kOverlay:  return "overlay";
    case ComponentType::kCanvas:   return "canvas";
    case ComponentType::kCount:    break;
    }
    return "unknown";
}

void ComponentRegistry::register_component(std::unique_ptr<Component> component) {
    if (!component) {
        throw std::invalid_argument(
            "ComponentRegistry: null component"
        );
    }
    if (find(component->info().id) != nullptr) {
        throw std::invalid_argument(
            "ComponentRegistry: duplicate component id: " +
            component->info().id
        );
    }
    components_.push_back(std::move(component));
}

Component* ComponentRegistry::find(const std::string_view id) noexcept {
    const auto it = std::find_if(
        components_.begin(),
        components_.end(),
        [id](const std::unique_ptr<Component>& component) {
            return component->info().id == id;
        }
    );
    return it == components_.end() ? nullptr : it->get();
}

const Component* ComponentRegistry::find(const std::string_view id) const noexcept {
    const auto it = std::find_if(
        components_.begin(),
        components_.end(),
        [id](const std::unique_ptr<Component>& component) {
            return component->info().id == id;
        }
    );
    return it == components_.end() ? nullptr : it->get();
}

const std::vector<std::unique_ptr<Component>>&
ComponentRegistry::components() const noexcept {
    return components_;
}

nlohmann::json ComponentRegistry::execute(
    const std::string_view id,
    const std::string& command,
    const nlohmann::json& params
) {
    Component* component = find(id);
    if (component == nullptr) {
        throw ComponentError(
            -32602,
            "unknown component: " + std::string(id)
        );
    }
    return component->execute(command, params);
}

nlohmann::json ComponentRegistry::list_json() const {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& component : components_) {
        nlohmann::json capabilities = nlohmann::json::array();
        for (const auto& spec : component->capabilities()) {
            capabilities.push_back({
                {"command", spec.command},
                {"description", spec.description},
                {"requires_param", spec.requires_param}
            });
        }
        out.push_back({
            {"id", component->info().id},
            {"name", component->info().name},
            {"description", component->info().description},
            {"type", component_type_name(component->info().type)},
            {"debug", component->info().debug},
            {"capabilities", std::move(capabilities)}
        });
    }
    return out;
}

} // namespace gs3d::control
