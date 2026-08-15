#pragma once

/*
 * TIA-109：面板组件（panel.*）。
 *
 * 面板是第一阶段接入控制面的组件类型：每张 ui::PanelRegistry 里登记的面板
 * 都对应一个控制组件，命令直接作用于 AppState::panels —— 与 UI 绘制读取的
 * 是同一份状态。因此「控制面 toggle 成功」与「界面上真的出现/消失」是同一
 * 件事，不存在两条真相来源。
 *
 * 覆盖强绑定：tests/ComponentRegistryTests.cpp 遍历 ui::kPanelRegistry（UI
 * 侧唯一面板清单），要求每张面板都有对应的控制组件并逐项驱动 —— 新增面板
 * 不注册会让测试失败（由构造保证，不是白名单自觉）。
 */

#include "control/ComponentRegistry.hpp"

#include <memory>
#include <string>
#include <vector>

namespace gs3d::app { struct AppState; }
namespace gs3d::ui { enum class PanelId : int; }

namespace gs3d::control {

// panel.<id> 稳定 ID（如 "panel.dataset"）。未知 id 返回空串。
[[nodiscard]] std::string panel_component_id(gs3d::ui::PanelId id);

// 为 AppState::panels 的每一张面板构造控制组件。
[[nodiscard]] std::vector<std::unique_ptr<Component>> make_panel_components(
    gs3d::app::AppState& app_state
);

} // namespace gs3d::control
