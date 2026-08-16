#pragma once

/*
 * TIA-111 工具栏/状态栏/Overlay/Gizmo/Canvas 组件：
 * 把所有非面板、非菜单的可交互 UI 元素接入 ComponentRegistry。
 */

#include "control/ComponentRegistry.hpp"
#include "app/AppState.hpp"
#include "app/UiActions.hpp"

#include <memory>
#include <vector>

namespace gs3d::control {

// 创建所有工具栏组件
[[nodiscard]]
std::vector<std::unique_ptr<Component>> make_toolbar_components(
    gs3d::app::AppState& app_state,
    gs3d::app::UiActions& actions
);

// 创建状态栏组件（只读）
[[nodiscard]]
std::vector<std::unique_ptr<Component>> make_status_components(
    gs3d::app::AppState& app_state
);

// 创建 overlay 组件（快捷键总览、面板命令面板）
[[nodiscard]]
std::vector<std::unique_ptr<Component>> make_overlay_components(
    gs3d::app::AppState& app_state
);

// 创建 gizmo 组件（导航球）
[[nodiscard]]
std::vector<std::unique_ptr<Component>> make_gizmo_components(
    gs3d::app::AppState& app_state,
    gs3d::app::UiActions& actions
);

// 创建 canvas 组件（视口画布）
[[nodiscard]]
std::vector<std::unique_ptr<Component>> make_canvas_components(
    gs3d::app::AppState& app_state,
    gs3d::app::UiActions& actions
);

} // namespace gs3d::control
