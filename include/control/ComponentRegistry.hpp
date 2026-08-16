#pragma once

/*
 * TIA-109 控制面契约：组件注册表（ComponentRegistry）。
 *
 * 这是下一阶段 imgui-coder 把「每一个」UI 组件接入控制面的唯一入口：
 *   - 未注册的组件，控制面命令一律报错（未注册即失败，不是白名单约定）；
 *   - 每个组件声明稳定 ID、类型与能力（可执行命令 / 可读状态）；
 *   - 命令经 Component::execute 分发，结果以 JSON 回传。
 *
 * 本头文件不依赖 ImGui / Vulkan 运行时对象，纯逻辑可直接单测。
 * 契约与接入步骤见 docs/component-registry.md。
 */

#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace gs3d::control {

// 组件类型（覆盖面板、视口、工具栏、菜单、控件、gizmo、状态栏、overlay）。
enum class ComponentType : int {
    kPanel = 0,
    kViewport = 1,
    kToolbar = 2,
    kMenu = 3,
    kButton = 4,
    kSlider = 5,
    kDropdown = 6,
    kCheckbox = 7,
    kInput = 8,
    kGizmo = 9,
    kStatus = 10,
    kOverlay = 11,
    kCanvas = 12,
    kCount,
};

[[nodiscard]] std::string_view component_type_name(ComponentType type) noexcept;

// 单项能力的声明：外部 AI 通过 list_components 读到它来自我发现。
struct CommandSpec {
    std::string command;        // 稳定命令名："toggle" / "set_visible" / "get_state" ...
    std::string description;    // 人可读说明（中英皆可）
    bool requires_param = false;
};

struct ComponentInfo {
    std::string id;             // 稳定 ID，如 "panel.dataset"、"menu.view.theme"
    std::string name;           // 显示名
    std::string description;    // 描述
    ComponentType type = ComponentType::kButton;
    bool debug = false;         // 调试组件：注册但仍可驱动，但不参与 100% 覆盖驱动测试
};

// 命令执行失败。code 为 JSON-RPC 风格错误码（-32601 未知命令 / -32602 非法参数）。
class ComponentError : public std::runtime_error {
public:
    ComponentError(int code, std::string message)
        : std::runtime_error(std::move(message)), code_(code) {}

    [[nodiscard]] int code() const noexcept { return code_; }

private:
    int code_;
};

/*
 * 所有 UI 组件必须实现的契约。get_state 与 execute 在主线程（渲染循环）
 * 调用；实现不得阻塞、不得触碰注册表本身（避免重入）。
 */
class Component {
public:
    virtual ~Component() = default;

    [[nodiscard]] virtual const ComponentInfo& info() const noexcept = 0;
    [[nodiscard]] virtual const std::vector<CommandSpec>& capabilities() const noexcept = 0;

    // 可读状态的快照（JSON 对象）。
    [[nodiscard]] virtual nlohmann::json get_state() = 0;

    // 执行一条命令。失败抛 ComponentError，成功返回结果 JSON（可为 null）。
    [[nodiscard]] virtual nlohmann::json execute(
        const std::string& command,
        const nlohmann::json& params
    ) = 0;
};

/*
 * 注册表：进程内一份（ControlPlaneSession 持有），测试可独立构造。
 * 仅在主线程访问；服务线程只搬运 JSON 字符串，不触碰注册表。
 */
class ComponentRegistry {
public:
    // 注册组件；id 重复时抛 std::invalid_argument。
    void register_component(std::unique_ptr<Component> component);

    [[nodiscard]] Component* find(std::string_view id) noexcept;
    [[nodiscard]] const Component* find(std::string_view id) const noexcept;

    [[nodiscard]] const std::vector<std::unique_ptr<Component>>& components() const noexcept;

    // 便捷分发：未注册组件抛 ComponentError(-32602, "unknown component")。
    [[nodiscard]] nlohmann::json execute(
        std::string_view id,
        const std::string& command,
        const nlohmann::json& params
    );

    // list_components 的结果载荷：组件清单（含能力描述）。
    [[nodiscard]] nlohmann::json list_json() const;

private:
    std::vector<std::unique_ptr<Component>> components_;
};

} // namespace gs3d::control
