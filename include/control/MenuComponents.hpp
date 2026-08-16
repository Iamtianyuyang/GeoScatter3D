#pragma once

/*
 * TIA-111 菜单组件：把所有菜单项接入 ComponentRegistry。
 *
 * 菜单项的能力：
 *   - click：触发菜单动作（打开文件、新建视图等）
 *   - get_state：返回当前状态
 *
 * 状态来源：AppState（与 UI 绘制共享同一份状态）。
 */

#include "control/ComponentRegistry.hpp"
#include "app/AppState.hpp"
#include "app/UiActions.hpp"

#include <memory>
#include <vector>

namespace gs3d::control {

// 创建所有菜单组件。每个组件的 execute 会修改 actions 引用，
// actions 在每帧由 ViewerApp 读取并处理。
[[nodiscard]]
std::vector<std::unique_ptr<Component>> make_menu_components(
    gs3d::app::AppState& app_state,
    gs3d::app::UiActions& actions
);

} // namespace gs3d::control
