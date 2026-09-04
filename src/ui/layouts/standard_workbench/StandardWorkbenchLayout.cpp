#include "ui/layouts/standard_workbench/StandardWorkbenchLayout.hpp"

#include "ui/layouts/standard_workbench/DockLayoutBuilder.hpp"
#include "ui/layouts/standard_workbench/WorkbenchViewControls.hpp"
#include "ui/layouts/LayoutMetrics.hpp"
#include "ui/layouts/LayoutRegistry.hpp"
#include "ui/AppChrome.hpp"
#include "ui/DatasetPanel.hpp"
#include "ui/NavigationMapPanel.hpp"
#include "ui/RenderSettingsPanel.hpp"
#include "ui/AuxiliaryPanels.hpp"
#include "ui/MeasurementPanel.hpp"
#include "ui/ViewportCanvas.hpp"
#include "ui/UiOverlays.hpp"
#include "ui/WorkspaceManager.hpp"
#include "ui/UiFonts.hpp"
#include "ui/UiRoot.hpp"
#include "ui/color/Theme.hpp"
#include "ui/color/UiPalette.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <string>
#include <vector>

namespace gs3d::ui {

/*
 * =========================================================================================
 * 方案 A · 标准工作台 (Standard Workbench) 核心布局渲染器
 * =========================================================================================
 *
 * 界面区域结构划分 (各区域由对应的局部函数负责渲染，方便后续单独扩展定制)：
 *
 * +---------------------------------------------------------------------------------------+
 * | 【区域 1】顶部主菜单栏 (draw_workbench_menu_bar)                                       |
 * | 文件、视图、窗口、帮助以及右侧当前数据集名称与全局快捷键入口                           |
 * +---------------------------+-------------------------------+---------------------------+
 * | 【区域 2】左侧停靠区      | 【区域 3】中央 3D 主视口栏    | 【区域 4】右侧检查器栏    |
 * | (draw_workbench_left)     | (draw_workbench_center)       | (draw_workbench_right)    |
 * |                           |                               |                           |
 * | 1. 项目数据源列表         | 1. 多视口标签页切换           | 1. 渲染属性配置面板       |
 * |    (DatasetPanel)         | 2. 视角快速控制栏 (Gizmo球)   |    (RenderSettingsPanel)  |
 * | 2. 鸟瞰空间导航图         | 3. 3D 点云主视口画布          | 2. 运行性能分析面板       |
 * |    (NavigationMapPanel)   |    (ViewportCanvas)           |    (PerformancePanel)     |
 * | 3. 空间测量/统计辅助面板  | 4. 自适应标尺坐标刻度         |                           |
 * +---------------------------+-------------------------------+---------------------------+
 * | 【区域 5】底部全局状态栏 (draw_workbench_status_bar)                                   |
 * | 全局帧率性能、内存/显存指标、点数统计与操作就绪状态                                    |
 * +---------------------------------------------------------------------------------------+
 */

namespace {

// 标准工作台内部持久化状态（从 UiRoot 中解耦下沉）
struct StandardWorkbenchUiState {
    DockLayoutPersistState dock_layout;
    float last_layout_work_w = -1.0f;
    float last_layout_work_h = -1.0f;
    bool focus_workbench_dataset = true;
};

static StandardWorkbenchUiState s_workbench_ui;

constexpr const char* kHostWindowName =
    "GeoScatter3D 工作台###GeoScatter3DWorkspace";
constexpr const char* kDatasetWindowName =
    "项目###DatasetPanel";

// 独立次级工作区窗口标识符
std::string workspace_window_name(int id)
{
    return "工作窗口 " + std::to_string(id) +
        "###WorkspaceWindow" + std::to_string(id);
}

std::string workspace_dataset_window_name(int id)
{
    return "项目###WorkspaceDataset" + std::to_string(id);
}

std::string workspace_measurement_window_name(int id)
{
    return "测量###WorkspaceMeasurement" + std::to_string(id);
}

std::string workspace_navigation_window_name(int id)
{
    return "导航图###WorkspaceNavigation" + std::to_string(id);
}

std::string workspace_render_settings_window_name(int id)
{
    return "属性###WorkspaceRenderSettings" + std::to_string(id);
}

std::string workspace_dockspace_id_name(int id)
{
    return "GeoScatter3D.WorkspaceDockSpace." + std::to_string(id);
}

// -----------------------------------------------------------------------------------------
// 【区域 1】顶部主菜单栏 (AppChrome)
// 包含：文件、视图、窗口、帮助以及右侧当前数据集名称
// -----------------------------------------------------------------------------------------
void draw_workbench_menu_bar(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    float ui_scale,
    AppChromeResult& chrome_result
)
{
    const bool menu_bar_visible = ImGui::BeginMenuBar();
    if (menu_bar_visible) {
        draw_top_bar(state, actions, ui_scale, chrome_result);
    }
    if (menu_bar_visible) {
        const ImRect menu_rect = ImGui::GetCurrentWindow()->MenuBarRect();
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(menu_rect.Min.x, menu_rect.Max.y - 1.0f),
            ImVec2(menu_rect.Max.x, menu_rect.Max.y - 1.0f),
            to_u32(palette::kBorder, 110),
            1.0f
        );
        ImGui::EndMenuBar();
    }
}

// -----------------------------------------------------------------------------------------
// 【区域 2】左侧停靠区：项目数据源面板、空间鸟瞰导航图与测量/统计辅助面板
// -----------------------------------------------------------------------------------------
void draw_workbench_left_panels(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions
)
{
    // 2.1 项目文件与图层数据源列表 (Dataset)
    draw_dataset_panel(state);

    // 2.2 空间鸟瞰与视锥体导航缩略图 (NavigationMap)
    draw_navigation_map(state);

    // 2.3 辅助停靠面板：测量 (Measurement)、区域统计 (RegionStats)、瓦片检查 (TileInspector)、LOD视图
    draw_auxiliary_panels(state, actions);
}

// -----------------------------------------------------------------------------------------
// 【区域 3】中央 3D 主视口栏：支持多视口标签页切换、嵌入式控制条 (Gizmo球/标尺/视角复位)
// -----------------------------------------------------------------------------------------
void draw_workbench_center_viewports(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions
)
{
    for (auto& view : state.render_views) {
        if (view.visible && !view_is_owned_by_workspace(state, view.viewport_index)) {
            draw_viewport_window(state, view, actions, 0, true);
        } else {
            if (!view.visible) {
                view.detached = false;
            }
            if (!view.visible || !view_is_owned_by_workspace(state, view.viewport_index)) {
                view.render_requested = false;
            }
        }
    }
}

// -----------------------------------------------------------------------------------------
// 【区域 4】右侧检查器栏：渲染属性配置面板 (RenderSettings) 与性能分析面板 (Performance)
// -----------------------------------------------------------------------------------------
void draw_workbench_right_panels(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions
)
{
    const auto main_viewports = main_workspace_viewports(state);
    const int main_active_view = active_view_for_indices(
        state,
        main_viewports,
        main_viewports.empty() ? 0 : main_viewports.front()
    );
    const std::vector<int> main_target_viewports{main_active_view};

    draw_render_settings(
        state,
        actions,
        nullptr,
        nullptr,
        &gs3d::app::render_settings_for_view(state, main_active_view),
        &main_target_viewports
    );
}

// -----------------------------------------------------------------------------------------
// 【独立多工作区】独立次级工作区窗口调度 (支持多屏多视口拖拽宿主)
// -----------------------------------------------------------------------------------------
void draw_secondary_workspace_window(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    gs3d::app::WorkspaceWindowState& workspace,
    float /*ui_scale*/
)
{
    if (!workspace.visible || workspace.viewport_indices.empty()) {
        return;
    }

    const auto host_name = workspace_window_name(workspace.id);
    const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    const float offset = 26.0f * static_cast<float>((workspace.id - 1) % 6);
    ImGui::SetNextWindowPos(
        ImVec2(main_viewport->WorkPos.x + 88.0f + offset, main_viewport->WorkPos.y + 72.0f + offset),
        ImGuiCond_FirstUseEver
    );
    ImGui::SetNextWindowSize(ImVec2(1180.0f, 720.0f), ImGuiCond_FirstUseEver);

    const ImVec4 host_bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, host_bg);
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, host_bg);
    ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, host_bg);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    const ImGuiWindowFlags host_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_MenuBar;
    if (ImGui::Begin(host_name.c_str(), &workspace.visible, host_flags)) {
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("视图")) {
                const bool can_add_view = has_hidden_view(state);
                if (ImGui::MenuItem("+ 视图", nullptr, false, can_add_view)) {
                    add_view_to_workspace(state, workspace);
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        const auto dockspace_name = workspace_dockspace_id_name(workspace.id);
        const ImGuiID dockspace_id = ImGui::GetID(dockspace_name.c_str());
        const ImVec2 dock_size = ImGui::GetContentRegionAvail();
        build_workspace_dock_layout(state, workspace, dockspace_id, dock_size.x, dock_size.y);
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    }
    ImGui::End();
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(3);

    if (!workspace.visible) {
        return;
    }

    const auto dataset_name = workspace_dataset_window_name(workspace.id);
    draw_dataset_panel(state, dataset_name.c_str(), nullptr, &workspace.components.dataset);

    const int workspace_active_view = active_view_for_indices(
        state,
        workspace.viewport_indices,
        workspace.viewport_indices.empty() ? 0 : workspace.viewport_indices.front()
    );

    const auto measurement_name = workspace_measurement_window_name(workspace.id);
    draw_measurement_panel(
        state,
        measurement_name.c_str(),
        nullptr,
        &gs3d::app::measurement_for_view(state, workspace_active_view)
    );

    const auto navigation_name = workspace_navigation_window_name(workspace.id);
    draw_navigation_map(
        state,
        navigation_name.c_str(),
        nullptr,
        &gs3d::app::navigation_map_for_view(state, workspace_active_view)
    );

    const auto render_settings_name = workspace_render_settings_window_name(workspace.id);
    const std::vector<int> workspace_target_viewports{workspace_active_view};
    draw_render_settings(
        state,
        actions,
        render_settings_name.c_str(),
        nullptr,
        &gs3d::app::render_settings_for_view(state, workspace_active_view),
        &workspace_target_viewports
    );

    for (const int view_index : workspace.viewport_indices) {
        if (view_index < 0 || view_index >= static_cast<int>(state.render_views.size())) {
            continue;
        }
        auto& view = state.render_views[static_cast<std::size_t>(view_index)];
        if (view.visible) {
            draw_viewport_window(state, view, actions, workspace.id);
        }
    }
}

void draw_secondary_workspaces(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    float ui_scale
)
{
    for (auto& workspace : state.workspace_windows) {
        draw_secondary_workspace_window(state, actions, workspace, ui_scale);
    }
    prune_workspace_windows(state);
}

// -----------------------------------------------------------------------------------------
// 【全局浮层】截屏成功提示、快捷键帮助遮罩、命令面板 (Palette)
// -----------------------------------------------------------------------------------------
void draw_workbench_overlays(
    gs3d::app::AppState& state,
    float ui_scale
)
{
    draw_screenshot_notice(state, ui_scale);
    draw_shortcut_overlay(state, ui_scale);
    draw_panel_command_palette(state, ui_scale);
}

} // namespace

void reset_standard_workbench_layout()
{
    s_workbench_ui.dock_layout.initialized = false;
    s_workbench_ui.last_layout_work_w = -1.0f;
    s_workbench_ui.last_layout_work_h = -1.0f;
    s_workbench_ui.focus_workbench_dataset = true;
    ImGui::DockBuilderRemoveNode(ImGui::GetID("GeoScatter3D.DockSpace"));
}

StandardWorkbenchFrameResult draw_standard_workbench_layout(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    float ui_scale
)
{
    StandardWorkbenchFrameResult frame_result;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiWindowFlags host_flags =
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_MenuBar;

    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, to_u32(palette::kMenuBg, 255));

    if (ImGui::Begin(kHostWindowName, nullptr, host_flags)) {
        AppChromeResult chrome_result;

        // 【区域 1】顶部主菜单栏
        draw_workbench_menu_bar(state, actions, ui_scale, chrome_result);

        if (chrome_result.layout_change_requested) {
            frame_result.layout_change_requested = true;
            frame_result.requested_layout_id = chrome_result.requested_layout_id;
            reset_standard_workbench_layout();
        }
        if (chrome_result.theme_change_requested) {
            frame_result.theme_change_requested = true;
            frame_result.requested_theme = chrome_result.requested_theme;
        }
        if (chrome_result.restore_default_workspace_requested) {
            frame_result.restore_default_workspace_requested = true;
            restore_default_workspace(state);
            reset_standard_workbench_layout();
        }

        // 计算 DockSpace 与底部状态栏高度
        const float content_avail_y = ImGui::GetContentRegionAvail().y;
        const float status_h = std::min(
            LayoutMetrics::kStatusBarHeightBase * ui_scale,
            std::max(0.0f, content_avail_y)
        );
        const float dock_h = std::max(0.0f, content_avail_y - status_h);

        // 构建标准工作台 DockSpace 拓扑树
        build_main_dock_layout(
            state,
            s_workbench_ui.dock_layout,
            s_workbench_ui.last_layout_work_w,
            s_workbench_ui.last_layout_work_h,
            s_workbench_ui.focus_workbench_dataset
        );

        ImGui::DockSpace(
            ImGui::GetID("GeoScatter3D.DockSpace"),
            ImVec2(0.0f, dock_h),
            ImGuiDockNodeFlags_None
        );

        // 【区域 5】底部全局状态栏
        ImGui::BeginChild(
            "##StatusBar",
            ImVec2(0.0f, status_h),
            false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
        );
        draw_status_bar(state, ui_scale);
        ImGui::EndChild();
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    // 绘制所有 Dock 面板与视口
    // 【区域 4】右侧检查器 (RenderSettings / Performance)
    draw_workbench_right_panels(state, actions);

    // 【区域 3】中央 3D 主视口
    draw_workbench_center_viewports(state, actions);

    // 【独立多工作区窗口】
    draw_secondary_workspaces(state, actions, ui_scale);

    // 【区域 2】左侧停靠区 (Dataset / NavigationMap / Auxiliary)
    draw_workbench_left_panels(state, actions);

    if (s_workbench_ui.focus_workbench_dataset) {
        ImGui::SetWindowFocus(kDatasetWindowName);
        s_workbench_ui.focus_workbench_dataset = false;
    }

    // 【全局浮层】
    draw_workbench_overlays(state, ui_scale);

    return frame_result;
}

} // namespace gs3d::ui
