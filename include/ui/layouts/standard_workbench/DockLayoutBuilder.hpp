#pragma once

#include "app/AppState.hpp"

#include <cstdint>

namespace gs3d::ui {

/*
 * =========================================================================================
 * 方案 A · 标准工作台 (Standard Workbench) - Dock 停靠空间拓扑架构
 * =========================================================================================
 * 
 * 整个工作台的主界面通过 ImGui DockSpace 划分为五大核心区域：
 *
 * +---------------------------------------------------------------------------------------+
 * |  [顶栏 AppChrome] 菜单栏 / 标题 / 运行状态 / 布局切换 / 窗口控制                      |
 * +---------------------------------------------------------------------------------------+
 * |  [左侧停靠区 dock_left]  |         [中央主视口 dock_center]        | [右侧停靠区 dock_right] |
 * |  (默认宽 280px, 可拖拽) |                                         | (默认宽 280px, 可拖拽) |
 * | +---------------------+ | +-------------------------------------+ | +---------------------+ |
 * | | 上半部 (65% 高度):   | | | 渲染视口 (RenderView0, 1...)        | | | 上半部 (60% 高度):   | |
 * | | - 项目 (Dataset)    | | | - 3D 点云主画布 ViewportCanvas      | | | - 属性 (RenderSet)  | |
 * | | - 瓦片 (TileInsp)   | | | - 视图视角控制栏 ViewControls       | | | - 区域统计 (Stats)  | |
 * | | - 细节层级 (LodView)| | | - 导航球 / 坐标轴刻度 / 框选 HUD    | | +---------------------+ |
 * | +---------------------+ | |                                     | | 下半部 (40% 高度):   | |
 * | | 下半部 (35% 高度):   | | |                                     | | - 空间测量 (Measure)  | |
 * | | - 空间导航图 (NavMap) | | +-------------------------------------+ +---------------------+ |
 * +---------------------------------------------------------------------------------------+
 * |  [底部停靠区 dock_bottom] 性能监控 (Performance) 帧率/显存/流式预算 (默认折叠/按需展示)|
 * +---------------------------------------------------------------------------------------+
 * |  [底栏 AppChrome] 状态栏 (当前状态、点数、显存占用、快捷提示)                         |
 * +---------------------------------------------------------------------------------------+
 *
 * 区域与负责源码对应速查表：
 * 1. 顶栏 & 菜单栏：src/ui/AppChrome.cpp (`draw_menu_bar`, `draw_status_bar`)
 * 2. 左侧上部数据：src/ui/panels/DatasetPanel.cpp, AuxiliaryPanels.cpp
 * 3. 左侧下部导航：src/ui/panels/NavigationMapPanel.cpp
 * 4. 中央 3D 主视口：src/ui/ViewportCanvas.cpp + standard_workbench/WorkbenchViewControls.cpp
 * 5. 右侧上部渲染属性：src/ui/panels/RenderSettingsPanel.cpp, RegionStatsPanel.cpp
 * 6. 右侧下部测量工具：src/ui/panels/MeasurementPanel.cpp
 * 7. 底部性能监视：src/ui/panels/AuxiliaryPanels.cpp (`draw_performance_panel`)
 * 8. Dock 空间拓扑切分：src/ui/layouts/standard_workbench/DockLayoutBuilder.cpp (`build_main_dock_layout`)
 */

// Dock 布局持久化状态与首帧决策（纯逻辑，无 ImGui 依赖，便于单测）：
// 本会话首次构建时若 ini 已恢复出持久化 DockSpace 节点，应采纳用户布局而
// 不重建（TIA-90）。built_once 只记录"首次构建已发生"：运行期"恢复默认
// 工作区"清空 initialized 后不会重新采纳旧布局，而是照常重建默认布局。
struct DockLayoutPersistState {
    // 工作台布局当前是否已生效（恢复默认工作区会清空以触发重建）。
    bool initialized = false;
    bool built_once = false;
    std::uint32_t signature = 0;
};

// 返回 true 表示保留当前布局（含首帧采纳 ini 恢复的持久化布局），
// 本次无需重建默认布局；返回 false 表示调用方应重建。
[[nodiscard]]
inline bool keep_current_dock_layout(
    DockLayoutPersistState& state,
    bool signature_matches,
    bool size_changed_significantly,
    bool persisted_node_available,
    std::uint32_t signature
) noexcept
{
    if (state.initialized && signature_matches &&
        !size_changed_significantly) {
        return true;
    }
    if (!state.built_once) {
        state.built_once = true;
        if (!state.initialized && persisted_node_available) {
            // 首帧采纳 ini 恢复的持久化布局，不再整树重建。
            state.initialized = true;
            state.signature = signature;
            return true;
        }
    }
    return false;
}

// 主视口 DockSpace 默认布局节点编排
void build_main_dock_layout(
    const gs3d::app::AppState& state,
    DockLayoutPersistState& dock_layout,
    float& last_layout_work_w,
    float& last_layout_work_h,
    bool& focus_workbench_dataset
);

// 独立工作区窗口的 DockSpace 节点编排
void build_workspace_dock_layout(
    const gs3d::app::AppState& state,
    gs3d::app::WorkspaceWindowState& workspace,
    unsigned int dockspace_id,
    float dock_size_x,
    float dock_size_y
);

} // namespace gs3d::ui
