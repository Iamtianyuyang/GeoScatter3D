#include "ui/layouts/standard_workbench/DockLayoutBuilder.hpp"
#include "ui/layouts/LayoutMetrics.hpp"
#include "ui/layouts/LayoutRegistry.hpp"
#include "ui/WorkspaceManager.hpp"
#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace gs3d::ui {

namespace {

constexpr const char* kToolsWindowName = "工具###ToolsPanel";
constexpr const char* kDatasetWindowName = "项目###DatasetPanel";
constexpr const char* kRenderSettingsWindowName = "属性###RenderSettings";
constexpr const char* kTileInspectorWindowName = "瓦片###TileInspector";
constexpr const char* kLodViewWindowName = "细节层级###LodView";
constexpr const char* kPerformanceWindowName = "性能###Performance";
constexpr const char* kNavigationMapWindowName = "导航图###NavigationMap";
constexpr const char* kMeasurementWindowName = "测量###Measurement";
constexpr const char* kRegionStatsWindowName = "区域统计###RegionStats";
constexpr const char* kDebugLogWindowName = "日志###DebugLog";

std::string render_view_window_name(int index)
{
    return "视图 " + std::to_string(index + 1) +
        "###RenderView" + std::to_string(index);
}

std::string workspace_tools_window_name(int id)
{
    return "工具###WorkspaceTools" + std::to_string(id);
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

} // namespace

void build_workspace_dock_layout(
    const gs3d::app::AppState& state,
    gs3d::app::WorkspaceWindowState& workspace,
    unsigned int dockspace_id,
    float dock_size_x,
    float dock_size_y
) {
    if (workspace.dock_layout_initialized) {
        return;
    }

    ImVec2 dock_size{std::max(1.0f, dock_size_x), std::max(1.0f, dock_size_y)};

    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(
        dockspace_id,
        ImGuiDockNodeFlags_DockSpace
    );
    ImGui::DockBuilderSetNodeSize(dockspace_id, dock_size);

    const float work_width = std::max(1.0f, dock_size.x);
    const float left_width = std::clamp(
        work_width * LayoutMetrics::kDockLeftRatio,
        LayoutMetrics::kDockLeftMinPx,
        LayoutMetrics::kDockLeftMaxPx
    );
    const float right_width = std::clamp(
        work_width * LayoutMetrics::kDockRightRatio,
        LayoutMetrics::kDockRightMinPx,
        LayoutMetrics::kDockRightMaxPx
    );

    ImGuiID center_id = dockspace_id;
    const ImGuiID left_id = ImGui::DockBuilderSplitNode(
        center_id,
        ImGuiDir_Left,
        left_width / work_width,
        nullptr,
        &center_id
    );
    const ImGuiID right_id = ImGui::DockBuilderSplitNode(
        center_id,
        ImGuiDir_Right,
        right_width / std::max(1.0f, work_width - left_width),
        nullptr,
        &center_id
    );

    ImGuiID left_top_id = left_id;
    const ImGuiID left_bottom_id = ImGui::DockBuilderSplitNode(
        left_id,
        ImGuiDir_Down,
        0.35f,
        nullptr,
        &left_top_id
    );

    ImGuiID right_top_id = right_id;
    const ImGuiID right_bottom_id = ImGui::DockBuilderSplitNode(
        right_id,
        ImGuiDir_Down,
        0.40f,
        nullptr,
        &right_top_id
    );

    ImGuiID view_area_id = center_id;
    const float tools_ratio = std::clamp(
        LayoutMetrics::kToolsBarHeightBase /
            std::max(1.0f, dock_size.y),
        0.045f,
        0.12f
    );
    const ImGuiID tools_id = ImGui::DockBuilderSplitNode(
        center_id,
        ImGuiDir_Up,
        tools_ratio,
        nullptr,
        &view_area_id
    );
    if (ImGuiDockNode* tools_node = ImGui::DockBuilderGetNode(tools_id)) {
        tools_node->LocalFlags |=
            ImGuiDockNodeFlags_NoTabBar |
            ImGuiDockNodeFlags_NoWindowMenuButton;
    }

    ImGui::DockBuilderDockWindow(
        workspace_tools_window_name(workspace.id).c_str(),
        tools_id
    );
    ImGui::DockBuilderDockWindow(
        workspace_dataset_window_name(workspace.id).c_str(),
        left_top_id
    );
    ImGui::DockBuilderDockWindow(
        workspace_navigation_window_name(workspace.id).c_str(),
        left_bottom_id
    );
    ImGui::DockBuilderDockWindow(
        workspace_render_settings_window_name(workspace.id).c_str(),
        right_top_id
    );
    ImGui::DockBuilderDockWindow(
        workspace_measurement_window_name(workspace.id).c_str(),
        right_bottom_id
    );
    for (const int view_index : workspace.viewport_indices) {
        if (view_index < 0 ||
            view_index >= static_cast<int>(state.render_views.size())) {
            continue;
        }
        const auto& view =
            state.render_views[static_cast<std::size_t>(view_index)];
        if (view.visible && !view.detached) {
            ImGui::DockBuilderDockWindow(
                render_view_window_name(view.viewport_index).c_str(),
                view_area_id
            );
        }
    }

    ImGui::DockBuilderFinish(dockspace_id);
    workspace.dock_layout_initialized = true;
}

void build_main_dock_layout(
    const gs3d::app::AppState& state,
    DockLayoutPersistState& dock_layout,
    float& last_layout_work_w,
    float& last_layout_work_h,
    bool& focus_workbench_dataset
) {
    const std::uint32_t signature = visible_view_signature(state);
    ImVec2 work_size = ImGui::GetMainViewport()->WorkSize;
    // 窗口最小化时 WorkSize 可能为 (0,0)，ImGui 断言要求正尺寸。
    work_size.x = std::max(1.0f, work_size.x);
    work_size.y = std::max(1.0f, work_size.y);

    const bool size_changed_significantly =
        last_layout_work_w > 0.0f && last_layout_work_h > 0.0f &&
        (std::abs(work_size.x - last_layout_work_w) >
             0.25f * last_layout_work_w ||
         std::abs(work_size.y - last_layout_work_h) >
             0.25f * last_layout_work_h);

    const ImGuiID dockspace_id = ImGui::GetID("GeoScatter3D.DockSpace");
    if (keep_current_dock_layout(
            dock_layout, dock_layout.signature == signature,
            size_changed_significantly,
            ImGui::DockBuilderGetNode(dockspace_id) != nullptr,
            signature)) {
        return;
    }
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(
        dockspace_id,
        ImGuiDockNodeFlags_DockSpace
    );
    ImGui::DockBuilderSetNodeSize(
        dockspace_id,
        work_size
    );
    const float work_width = std::max(1.0f, work_size.x);
    const float default_left_width = std::clamp(
        work_width * LayoutMetrics::kDockLeftRatio,
        LayoutMetrics::kDockLeftMinPx,
        LayoutMetrics::kDockLeftMaxPx
    );
    const float default_right_width = std::clamp(
        work_width * LayoutMetrics::kDockRightRatio,
        LayoutMetrics::kDockRightMinPx,
        LayoutMetrics::kDockRightMaxPx
    );
    const float left_ratio = default_left_width / work_width;

    const auto active_layout = LayoutRegistry::instance().active_layout_id();
    const bool is_floating = (active_layout == "floating-dock" || state.ui_layout_mode == gs3d::app::UiLayoutMode::kFloatingDock);
    const bool is_rail = (active_layout == "analysis-rail" || state.ui_layout_mode == gs3d::app::UiLayoutMode::kAnalysisRail);
    const bool is_workbench = (!is_floating && !is_rail);

    const bool has_left_panels =
        state.panels.dataset ||
        state.panels.tile_inspector ||
        state.panels.lod_view ||
        state.panels.navigation_map ||
        state.panels.measurement ||
        state.panels.region_stats;
    const bool has_right_panels =
        state.panels.render_settings ||
        state.panels.performance;
    const float right_split_width = std::max(
        1.0f,
        work_width - (has_left_panels ? default_left_width : 0.0f)
    );
    const float right_ratio = default_right_width / right_split_width;

    ImGuiID center_id = dockspace_id;
    ImGuiID left_id = 0;
    ImGuiID right_id = 0;
    if (has_left_panels) {
        left_id = ImGui::DockBuilderSplitNode(
            center_id,
            ImGuiDir_Left,
            left_ratio,
            nullptr,
            &center_id
        );
    }
    if (has_right_panels) {
        right_id = ImGui::DockBuilderSplitNode(
            center_id,
            ImGuiDir_Right,
            right_ratio,
            nullptr,
            &center_id
        );
    }

    // 左侧面板上下切分：上半放项目/瓦片/LOD，下半放导航图。
    // 参照用户手动拖出的布局 (config/imgui_layout.ini)。
    ImGuiID left_top_id = left_id;
    ImGuiID left_bottom_id = 0;
    if (left_id != 0) {
        left_bottom_id = ImGui::DockBuilderSplitNode(
            left_id,
            ImGuiDir_Down,
            0.18f,               // 方案 A：底部导航图约占工作区 18%
            nullptr,
            &left_top_id
        );

        if (state.panels.dataset) {
            ImGui::DockBuilderDockWindow(kDatasetWindowName, left_top_id);
        }
        if (state.panels.tile_inspector) {
            ImGui::DockBuilderDockWindow(kTileInspectorWindowName, left_top_id);
        }
        if (state.panels.lod_view) {
            ImGui::DockBuilderDockWindow(kLodViewWindowName, left_top_id);
        }
        if (state.panels.measurement) {
            ImGui::DockBuilderDockWindow(kMeasurementWindowName, left_top_id);
        }
        if (state.panels.region_stats) {
            ImGui::DockBuilderDockWindow(kRegionStatsWindowName, left_top_id);
        }
        if (ImGuiDockNode* node = ImGui::DockBuilderGetNode(left_top_id)) {
            node->SelectedTabId = ImGui::GetID(kDatasetWindowName);
        }
    }
    if (left_bottom_id != 0) {
        ImGui::DockBuilderDockWindow(kNavigationMapWindowName, left_bottom_id);
    }
    if (right_id != 0) {
        if (state.panels.render_settings) {
            ImGui::DockBuilderDockWindow(kRenderSettingsWindowName, right_id);
        }
        if (state.panels.performance) {
            ImGui::DockBuilderDockWindow(kPerformanceWindowName, right_id);
        }
        if (ImGuiDockNode* node = ImGui::DockBuilderGetNode(right_id)) {
            node->SelectedTabId = ImGui::GetID(kRenderSettingsWindowName);
        }
    }

    for (const auto& view : state.render_views) {
        if (view.visible &&
            !view.detached &&
            !view_is_owned_by_workspace(state, view.viewport_index)) {
            ImGui::DockBuilderDockWindow(
                render_view_window_name(view.viewport_index).c_str(),
                center_id
            );
        }
    }

    ImGui::DockBuilderFinish(dockspace_id);
    dock_layout.initialized = true;
    dock_layout.signature = signature;
    last_layout_work_w = work_size.x;
    last_layout_work_h = work_size.y;
    focus_workbench_dataset = true;
}

} // namespace gs3d::ui
