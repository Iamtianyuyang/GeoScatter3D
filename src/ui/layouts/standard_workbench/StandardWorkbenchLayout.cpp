#include "ui/layouts/standard_workbench/StandardWorkbenchLayout.hpp"

#include "ui/AppChrome.hpp"
#include "ui/DatasetPanel.hpp"
#include "ui/NavigationMapPanel.hpp"
#include "ui/RenderSettingsPanel.hpp"
#include "ui/ViewportCanvas.hpp"
#include "ui/layouts/standard_workbench/WorkbenchViewControls.hpp"
#include "ui/color/UiPalette.hpp"

#include "imgui.h"

#include <algorithm>
#include <vector>

namespace gs3d::ui {

/*
 * =========================================================================================
 * 方案 A · 标准工作台 (Standard Workbench) - 三栏独立布局渲染器
 * =========================================================================================
 *
 * 界面区域纵览 (从上至下、从左至右)：
 *
 * +---------------------------------------------------------------------------------------+
 * | 【区域 1】左侧场景栏      | 【区域 2】中央主视口栏        | 【区域 3】右侧检查器栏    |
 * | (draw_scene_column)       | (draw_viewport_column)        | (draw_inspector_column)   |
 * |                           |                               |                           |
 * | 1. 项目数据源列表         | 1. 视口标头与视角快速控制栏   | 1. 检查器标头             |
 * |    (DatasetPanel)         |    (WorkbenchViewControls)    | 2. 渲染属性配置面板       |
 * | 2. 鸟瞰空间导航图         | 2. 3D 点云主视口画布          |    (RenderSettingsPanel)  |
 * |    (NavigationMapPanel)   |    (ViewportCanvas)           |    - 点云大小/颜色映射    |
 * |                           |                               |    - 色带LUT/高度夸张     |
 * +---------------------------+-------------------------------+---------------------------+
 * | 【区域 4】底部状态栏 (draw_status_bar)                                                |
 * | 全局渲染状态、内存/点数统计、提示消息                                                  |
 * +---------------------------------------------------------------------------------------+
 *
 * 提示（供后续定制优化参考）：
 * - 调整侧栏宽度：参见 draw_standard_workbench_layout 中的 left_width 与 right_width 计算公式
 * - 调整栏间距：参见 draw_standard_workbench_layout 中的 gutter 与 WindowPadding
 * - 增加/替换侧边栏组件：在 draw_scene_column 或 draw_inspector_column 中嵌入对应 panel content
 */

namespace {

// 底部状态栏基础高度（实际高度会乘以 ui_scale）
constexpr float kStatusBarHeight = 26.0f;

// -----------------------------------------------------------------------------------------
// 【区域 1】左侧场景栏：包含项目数据源列表（上半部）和鸟瞰空间导航图（下半部）
// -----------------------------------------------------------------------------------------
void draw_scene_column(
    gs3d::app::AppState& state,
    int active_view,
    float scale
)
{
    ImGui::SetCursorPosX(10.0f * scale);
    ImGui::TextUnformatted("场景");
    ImGui::SameLine();
    ImGui::TextDisabled("数据与导航");
    ImGui::Separator();

    const float navigation_height = std::clamp(
        150.0f * scale,
        120.0f * scale,
        ImGui::GetContentRegionAvail().y * 0.40f
    );
    const float gap = 10.0f * scale;
    const float dataset_height = std::max(
        72.0f * scale,
        ImGui::GetContentRegionAvail().y - navigation_height - gap
    );

    // 2.1 项目文件与图层数据源列表
    if (ImGui::BeginChild("##StandardWorkbenchDataset", ImVec2(0.0f, dataset_height), false)) {
        draw_dataset_panel_content(state);
        ImGui::EndChild();
    }
    ImGui::Spacing();
    ImGui::TextDisabled("导航图");
    // 2.2 空间鸟瞰与视锥体导航缩略图
    if (ImGui::BeginChild("##StandardWorkbenchNavigation", ImVec2(0.0f, 0.0f), false)) {
        draw_navigation_map_content(
            state,
            &gs3d::app::navigation_map_for_view(state, active_view)
        );
        ImGui::EndChild();
    }
}

// -----------------------------------------------------------------------------------------
// 【区域 2】中央主视口栏：视口标头、快速视角控制栏 (复位/坐标轴/十字线) 与 3D 点云画布
// -----------------------------------------------------------------------------------------
void draw_viewport_column(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    int active_view,
    float scale
)
{
    if (state.render_views.empty()) {
        ImGui::TextDisabled("没有可用视图");
        return;
    }

    auto& view = state.render_views[static_cast<std::size_t>(active_view)];
    view.render_requested = false;
    ImGui::Text("视图 %02d", active_view + 1);
    ImGui::SameLine();
    ImGui::TextDisabled("实时点云");
    ImGui::SameLine();
    // 2.1 视口快捷操作栏 (复位视角、地图轴、十字线、更多选项)
    draw_workbench_view_controls(state, view, actions, scale);
    ImGui::Separator();

    // 2.2 3D 点云主视口画布（接收鼠标交互、相机变换与拾取）
    ViewportCanvasOptions options;
    options.workspace_id = 0;
    options.show_info_badge = true;
    options.interaction_enabled = true;
    draw_viewport_canvas(view, actions, options);
}

// -----------------------------------------------------------------------------------------
// 【区域 3】右侧检查器栏：属性配置面板 (着色模式、点大小、色带映射、高度夸张等)
// -----------------------------------------------------------------------------------------
void draw_inspector_column(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    int active_view
)
{
    ImGui::TextUnformatted("检查器");
    ImGui::SameLine();
    ImGui::TextDisabled("渲染属性");
    ImGui::Separator();

    const std::vector<int> target_viewports{active_view};
    draw_render_settings_content(
        state,
        actions,
        &gs3d::app::render_settings_for_view(state, active_view),
        &target_viewports
    );
}

} // namespace

// -----------------------------------------------------------------------------------------
// 标准工作台主布局入口 (三栏流式布局编排)
// -----------------------------------------------------------------------------------------
void draw_standard_workbench_layout(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    float ui_scale
)
{
    const float scale = std::max(1.0f, ui_scale);
    const int active_view = gs3d::app::resolve_viewport_index(
        state,
        state.active_viewport_index
    );
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float status_height = std::min(kStatusBarHeight * scale, available.y);

    // -------------------------------------------------------------------------------------
    // 栏目几何度量与自适应计算（后续优化布局尺寸重点调整此处）：
    // - left_width: 左侧场景栏宽度（默认约占屏幕宽度 16%，限制在 220~300px）
    // - right_width: 右侧属性栏宽度（默认约占屏幕宽度 19%，限制在 264~340px）
    // - gutter: 三栏之间的间隔槽（默认 12px）
    // - viewport_width: 中央 3D 主视口自适应吞吐剩余所有可用空间
    // -------------------------------------------------------------------------------------
    const float body_height = std::max(
        0.0f,
        ImGui::GetContentRegionAvail().y - status_height
    );
    const float left_width = std::clamp(
        available.x * 0.16f,
        220.0f * scale,
        300.0f * scale
    );
    const float right_width = std::clamp(
        available.x * 0.19f,
        264.0f * scale,
        340.0f * scale
    );
    const float gutter = 12.0f * scale;
    const float viewport_width = std::max(
        1.0f,
        available.x - left_width - right_width - 2.0f * gutter
    );

    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(10.0f * scale, 8.0f * scale)
    );
    if (ImGui::BeginChild("##StandardWorkbenchScene", ImVec2(left_width, body_height), false)) {
        draw_scene_column(state, active_view, scale);
        ImGui::EndChild();
    }
    ImGui::SameLine(0.0f, gutter);
    if (ImGui::BeginChild(
            "##StandardWorkbenchViewport",
            ImVec2(viewport_width, body_height),
            false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
        )) {
        draw_viewport_column(state, actions, active_view, scale);
        ImGui::EndChild();
    }
    ImGui::SameLine(0.0f, gutter);
    if (ImGui::BeginChild("##StandardWorkbenchInspector", ImVec2(right_width, body_height), false)) {
        draw_inspector_column(state, actions, active_view);
        ImGui::EndChild();
    }
    ImGui::PopStyleVar();

    // -------------------------------------------------------------------------------------
    // 【区域 4】底部状态栏：显示性能帧率、点数、显存占用与状态提示
    // -------------------------------------------------------------------------------------
    if (ImGui::BeginChild(
            "##StandardWorkbenchStatus",
            ImVec2(0.0f, status_height),
            false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
        )) {
        draw_status_bar(state, scale);
        ImGui::EndChild();
    }
}

} // namespace gs3d::ui
