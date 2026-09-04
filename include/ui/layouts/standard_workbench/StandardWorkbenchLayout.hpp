#pragma once

#include "app/AppState.hpp"
#include "app/UiActions.hpp"
#include "ui/color/Theme.hpp"

#include <string>

namespace gs3d::ui {

// draw_standard_workbench_layout 的帧结果
struct StandardWorkbenchFrameResult {
    bool theme_change_requested = false;
    ThemeId requested_theme = ThemeId::kCarbonBlue;
    bool layout_change_requested = false;
    std::string requested_layout_id;
    bool restore_default_workspace_requested = false;
};

/*
 * =========================================================================================
 * 方案 A · 标准工作台 (Standard Workbench) 布局入口
 * =========================================================================================
 * 
 * 布局形态：基于 Dear ImGui DockSpace 的经典工作台拓扑
 *
 * +---------------------------------------------------------------------------------------+
 * | 【区域 1】顶部主菜单栏 (AppChrome: 文件 / 视图 / 窗口 / 帮助 / 当前数据名称)            |
 * +-----------------------+---------------------------------------+-----------------------+
 * | 【区域 2】左侧停靠区   | 【区域 3】中央 3D 主视口栏             | 【区域 4】右侧检查器栏 |
 * | - 数据集列表 (Dataset)| - 视口标题与视角快捷控制条 (Gizmo球)   | - 渲染属性 (Settings) |
 * | - 空间导航图 (NavMap) | - 3D 点云主画布 (ViewportCanvas)      | - 性能指标 (Perf)     |
 * | - 测量 / 区域统计     | - 自适应标尺刻度与多视口标签页         |                       |
 * +-----------------------+---------------------------------------+-----------------------+
 * | 【区域 5】底部全局状态栏 (StatusBar: FPS / 延迟 / 点数 / 显存占用 / 就绪状态)           |
 * +---------------------------------------------------------------------------------------+
 */
[[nodiscard]]
StandardWorkbenchFrameResult draw_standard_workbench_layout(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    float ui_scale
);

void reset_standard_workbench_layout();

} // namespace gs3d::ui
