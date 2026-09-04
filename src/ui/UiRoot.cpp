#include "ui/AppChrome.hpp"
#include "ui/UiRoot.hpp"
#include "ui/UiOverlays.hpp"
#include "ui/ViewportCanvas.hpp"
#include "ui/WorkbenchUi.hpp"
#include "ui/WorkspaceManager.hpp"
#include "ui/LayoutMetrics.hpp"
#include "ui/LayoutRegistry.hpp"
#include "ui/Theme.hpp"
#include "ui/Widgets.hpp"
#include "ui/UiPalette.hpp"
#include "ui/RegionStatsPanel.hpp"
#include "ui/DatasetPanel.hpp"
#include "ui/NavigationMapPanel.hpp"
#include "ui/MeasurementPanel.hpp"
#include "ui/AuxiliaryPanels.hpp"
#include "ui/RenderSettingsPanel.hpp"
#include "ui/FloatingDockUi.hpp"
#include "ui/AnalysisRailUi.hpp"

#include "ui/UiFonts.hpp"
#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace gs3d::ui {

namespace {

constexpr const char* kHostWindowName =
    "GeoScatter3D 工作台###GeoScatter3DWorkspace";
constexpr const char* kDatasetWindowName =
    "项目###DatasetPanel";
constexpr const char* kRenderSettingsWindowName =
    "属性###RenderSettings";
constexpr const char* kTileInspectorWindowName =
    "瓦片###TileInspector";
constexpr const char* kLodViewWindowName =
    "细节层级###LodView";
constexpr const char* kPerformanceWindowName =
    "性能###Performance";
constexpr const char* kNavigationMapWindowName =
    "导航图###NavigationMap";
constexpr const char* kMeasurementWindowName =
    "测量###Measurement";
constexpr const char* kRegionStatsWindowName =
    "区域统计###RegionStats";

std::string render_view_window_name(int index)
{
    return "视图 " + std::to_string(index + 1) +
        "###RenderView" + std::to_string(index);
}

std::string workspace_window_name(int id)
{
    return "工作窗口 " + std::to_string(id) +
        "###WorkspaceWindow" + std::to_string(id);
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

std::string workspace_dockspace_id_name(int id)
{
    return "GeoScatter3D.WorkspaceDockSpace." + std::to_string(id);
}

} // namespace

namespace {
ImFont* regular_font()
{
    return gs3d::gui::ui_fonts().regular;
}

ImFont* small_font()
{
    return gs3d::gui::ui_fonts().small;
}

ImFont* status_font()
{
    return gs3d::gui::ui_fonts().status;
}

void draw_tools_window(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    float ui_scale,
    const char* window_name = "工具###ToolsPanel",
    gs3d::app::WorkspaceWindowState* workspace = nullptr
) {
    const bool use_default_window = workspace == nullptr;
    if (use_default_window) {
        return;
    }

    ImGui::SetNextWindowSize(
        ImVec2(500.0f * ui_scale, 68.0f * ui_scale),
        ImGuiCond_FirstUseEver
    );
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;
    bool* open = use_default_window ? nullptr : nullptr;
    if (ImGui::Begin(window_name, open, flags)) {
        const auto target_viewports =
            workspace != nullptr
                ? workspace->viewport_indices
                : main_workspace_viewports(state);
        const int target_view = active_view_for_indices(
            state,
            target_viewports,
            state.active_viewport_index
        );
        auto& measurement =
            gs3d::app::measurement_for_view(state, target_view);
        auto& dataset =
            workspace != nullptr
                ? workspace->components.dataset
                : state.dataset;

        ImGui::PushStyleVar(
            ImGuiStyleVar_FramePadding,
            ImVec2(5.0f, 3.0f)
        );
        ImGui::PushStyleVar(
            ImGuiStyleVar_ItemSpacing,
            ImVec2(6.0f, 4.0f)
        );

        ImGui::TextDisabled("操作");
        ImGui::SameLine();
        const bool can_add_view = has_hidden_view(state);
        ImGui::BeginDisabled(!can_add_view);
        if (widgets::Chip("添加视图")) {
            show_first_hidden_view(state);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (widgets::Chip("截图")) {
            actions.screenshot_requested = true;
        }
        ImGui::SameLine();
        if (widgets::Chip("测量", measurement.measure_mode_active())) {
            measurement.toggle_measure_mode();
            if (!measurement.measure_mode_active()) {
                measurement.clear_pending();
            }
        }
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x > 190.0f * ui_scale) {
            ImGui::SetCursorPosX(std::max(
                ImGui::GetCursorPosX(),
                ImGui::GetWindowContentRegionMax().x - 270.0f * ui_scale
            ));
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                to_u32(palette::kTextDim, 190)
            );
            ImGui::TextUnformatted(
                dataset.active_dataset.empty()
                    ? "未加载数据"
                    : dataset.active_dataset.c_str()
            );
            ImGui::PopStyleColor();
        }

        ImGui::PopStyleVar(2);
    }
    ImGui::End();
}

void draw_viewport_window(
    gs3d::app::AppState& state,
    gs3d::app::RenderViewState& view,
    gs3d::app::UiActions& actions,
    int workspace_id = 0,
    bool show_workbench_controls = false
) {
    view.render_requested = false;
    const auto window_name =
        render_view_window_name(view.viewport_index);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;

    if (view.force_undock_next_frame) {
        const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
        const float offset =
            28.0f * static_cast<float>(view.viewport_index % 4);
        ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
        ImGui::SetNextWindowPos(
            ImVec2(
                main_viewport->WorkPos.x + 72.0f + offset,
                main_viewport->WorkPos.y + 64.0f + offset
            ),
            ImGuiCond_Always
        );
        ImGui::SetNextWindowSize(
            ImVec2(900.0f, 620.0f),
            ImGuiCond_Always
        );
    } else {
        ImGui::SetNextWindowSize(
            ImVec2(760.0f, 520.0f),
            ImGuiCond_FirstUseEver
        );
    }
    const bool content_visible =
        ImGui::Begin(window_name.c_str(), &view.visible, flags);
    view.force_undock_next_frame = false;
    if (!content_visible) {
        ImGui::End();
        return;
    }

    const auto* window_viewport = ImGui::GetWindowViewport();
    view.detached =
        !ImGui::IsWindowDocked() ||
        (window_viewport != nullptr &&
         window_viewport->ID != ImGui::GetMainViewport()->ID);
    const float toolbar_scale = ImGui::GetFontSize() / 13.0f;
    const bool embedded_controls =
        show_workbench_controls && !view.detached;
    if (embedded_controls) {
        draw_workbench_view_controls(state, view, actions, toolbar_scale);
    }

    ViewportCanvasOptions canvas_options;
    canvas_options.workspace_id = workspace_id;
    draw_viewport_canvas(view, actions, canvas_options);

    const unsigned int platform_viewport_id =
        window_viewport == nullptr
            ? ImGui::GetMainViewport()->ID
            : window_viewport->ID;
    const float canvas_top = view.canvas_rect_min_y;
    const float canvas_right = view.canvas_rect_max_x;
    ImGui::End();
    if (!embedded_controls) {
        draw_detached_view_camera_pill(
            view, actions, canvas_top, canvas_right,
            platform_viewport_id, toolbar_scale
        );
    }
}

void build_workspace_layout(
    const gs3d::app::AppState& state,
    gs3d::app::WorkspaceWindowState& workspace,
    ImGuiID dockspace_id,
    ImVec2 dock_size
) {
    if (workspace.dock_layout_initialized) {
        return;
    }

    dock_size.x = std::max(1.0f, dock_size.x);
    dock_size.y = std::max(1.0f, dock_size.y);

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

void draw_workspace_window(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    gs3d::app::WorkspaceWindowState& workspace,
    float ui_scale
) {
    if (!workspace.visible || workspace.viewport_indices.empty()) {
        return;
    }

    const auto host_name = workspace_window_name(workspace.id);
    const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    const float offset =
        26.0f * static_cast<float>((workspace.id - 1) % 6);
    ImGui::SetNextWindowPos(
        ImVec2(
            main_viewport->WorkPos.x + 88.0f + offset,
            main_viewport->WorkPos.y + 72.0f + offset
        ),
        ImGuiCond_FirstUseEver
    );
    ImGui::SetNextWindowSize(
        ImVec2(1180.0f, 720.0f),
        ImGuiCond_FirstUseEver
    );

    const ImVec4 host_bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, host_bg);
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, host_bg);
    ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, host_bg);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    const ImGuiWindowFlags host_flags =
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_MenuBar;
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
        build_workspace_layout(state, workspace, dockspace_id, dock_size);
        ImGui::DockSpace(
            dockspace_id,
            ImVec2(0.0f, 0.0f),
            ImGuiDockNodeFlags_None
        );
    }
    ImGui::End();
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(3);

    if (!workspace.visible) {
        return;
    }

    const auto tools_name = workspace_tools_window_name(workspace.id);
    draw_tools_window(state, actions, ui_scale, tools_name.c_str(), &workspace);

    const auto dataset_name = workspace_dataset_window_name(workspace.id);
    draw_dataset_panel(
        state,
        dataset_name.c_str(),
        nullptr,
        &workspace.components.dataset
    );

    const int workspace_active_view = active_view_for_indices(
        state,
        workspace.viewport_indices,
        workspace.viewport_indices.empty() ? 0 : workspace.viewport_indices.front()
    );

    const auto measurement_name =
        workspace_measurement_window_name(workspace.id);
    draw_measurement_panel(
        state,
        measurement_name.c_str(),
        nullptr,
        &gs3d::app::measurement_for_view(state, workspace_active_view)
    );

    const auto navigation_name =
        workspace_navigation_window_name(workspace.id);
    draw_navigation_map(
        state,
        navigation_name.c_str(),
        nullptr,
        &gs3d::app::navigation_map_for_view(state, workspace_active_view)
    );

    const auto render_settings_name =
        workspace_render_settings_window_name(workspace.id);
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
        if (view_index < 0 ||
            view_index >= static_cast<int>(state.render_views.size())) {
            continue;
        }
        auto& view = state.render_views[static_cast<std::size_t>(view_index)];
        if (view.visible) {
            draw_viewport_window(state, view, actions, workspace.id);
        }
    }
}

} // namespace
ImFont* panel_title_font()
{
    return gs3d::gui::ui_fonts().panel_title;
}

float bytes_to_mb(std::uint64_t bytes)
{
    return static_cast<float>(bytes) / (1024.0f * 1024.0f);
}

void draw_panel_section_label(const char* label)
{
    if (panel_title_font() != nullptr) {
        ImGui::PushFont(panel_title_font());
    }
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        to_u32(palette::kTextDim, 220)
    );
    ImGui::SeparatorText(label);
    ImGui::PopStyleColor();
    if (panel_title_font() != nullptr) {
        ImGui::PopFont();
    }
}

void UiRoot::build_default_layout(const gs3d::app::AppState& state)
{
    const std::uint32_t signature = visible_view_signature(state);
    ImVec2 work_size = ImGui::GetMainViewport()->WorkSize;
    // 窗口最小化时 WorkSize 可能为 (0,0)，ImGui 断言要求正尺寸。
    work_size.x = std::max(1.0f, work_size.x);
    work_size.y = std::max(1.0f, work_size.y);

    const bool size_changed_significantly =
        last_layout_work_w_ > 0.0f && last_layout_work_h_ > 0.0f &&
        (std::abs(work_size.x - last_layout_work_w_) >
             0.25f * last_layout_work_w_ ||
         std::abs(work_size.y - last_layout_work_h_) >
             0.25f * last_layout_work_h_);

    const ImGuiID dockspace_id = ImGui::GetID("GeoScatter3D.DockSpace");
    if (keep_current_dock_layout(
            dock_layout_, dock_layout_.signature == signature,
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

    // 工作台模式支持侧边栏与底部停靠，悬浮与导轨模式面板为浮动/抽屉
    const bool sidebar_visible = state.ui_chrome.sidebar_visible && is_workbench;
    const bool has_left_panels = sidebar_visible &&
        (state.panels.dataset ||
        state.panels.tile_inspector ||
        state.panels.lod_view ||
        state.panels.navigation_map);
    const bool has_right_panels = is_workbench &&
        (state.panels.render_settings ||
        state.panels.region_stats ||
        state.panels.measurement);
    const bool has_bottom_panels = is_workbench && state.panels.performance;

    ImGuiID center_id = dockspace_id;
    ImGuiID bottom_id = 0;
    ImGuiID left_id = 0;
    ImGuiID right_id = 0;

    // 1. 底部时序监控槽位（横向宽幅展示性能曲线与统计指标）
    if (has_bottom_panels) {
        constexpr float kBottomRatio = 0.20f;
        bottom_id = ImGui::DockBuilderSplitNode(
            center_id,
            ImGuiDir_Down,
            kBottomRatio,
            nullptr,
            &center_id
        );
    }

    const float right_split_width = std::max(
        1.0f,
        work_width - (has_left_panels ? default_left_width : 0.0f)
    );
    const float right_ratio = default_right_width / right_split_width;

    // 2. 切出左侧数据与资源中心
    if (has_left_panels) {
        left_id = ImGui::DockBuilderSplitNode(
            center_id,
            ImGuiDir_Left,
            left_ratio,
            nullptr,
            &center_id
        );
    }

    // 3. 切出右侧控制与分析中心
    if (has_right_panels) {
        right_id = ImGui::DockBuilderSplitNode(
            center_id,
            ImGuiDir_Right,
            right_ratio,
            nullptr,
            &center_id
        );
    }

    // 4. 左侧面板上下黄金分割（约 65% / 35%）：
    //    上半部：项目数据源拓扑（LOD -> 瓦片 -> 项目），项目默认激活
    //    下半部：空间鸟瞰导航图（拥有宽敞画布，视锥体与地形一目了然）
    ImGuiID left_top_id = left_id;
    ImGuiID left_bottom_id = 0;
    if (left_id != 0) {
        left_bottom_id = ImGui::DockBuilderSplitNode(
            left_id,
            ImGuiDir_Down,
            0.35f,
            nullptr,
            &left_top_id
        );

        if (state.panels.lod_view) {
            ImGui::DockBuilderDockWindow(kLodViewWindowName, left_top_id);
        }
        if (state.panels.tile_inspector) {
            ImGui::DockBuilderDockWindow(kTileInspectorWindowName, left_top_id);
        }
        if (state.panels.dataset) {
            ImGui::DockBuilderDockWindow(kDatasetWindowName, left_top_id);
        }
    }
    if (left_bottom_id != 0) {
        ImGui::DockBuilderDockWindow(kNavigationMapWindowName, left_bottom_id);
    }

    // 5. 右侧面板上下分割（约 60% / 40%）：
    //    上半部：渲染外观与数据分布（统计 -> 属性），属性默认激活
    //    下半部：空间工程量测（测量工具独立常驻，互不干扰）
    ImGuiID right_top_id = right_id;
    ImGuiID right_bottom_id = 0;
    if (right_id != 0) {
        right_bottom_id = ImGui::DockBuilderSplitNode(
            right_id,
            ImGuiDir_Down,
            0.40f,
            nullptr,
            &right_top_id
        );

        if (state.panels.region_stats) {
            ImGui::DockBuilderDockWindow(kRegionStatsWindowName, right_top_id);
        }
        if (state.panels.render_settings) {
            ImGui::DockBuilderDockWindow(kRenderSettingsWindowName, right_top_id);
        }
    }
    if (right_bottom_id != 0) {
        if (state.panels.measurement) {
            ImGui::DockBuilderDockWindow(kMeasurementWindowName, right_bottom_id);
        }
    }

    // 6. 底部监控槽停靠性能面板
    if (bottom_id != 0) {
        if (state.panels.performance) {
            ImGui::DockBuilderDockWindow(kPerformanceWindowName, bottom_id);
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
    dock_layout_.initialized = true;
    dock_layout_.signature = signature;
    last_layout_work_w_ = work_size.x;
    last_layout_work_h_ = work_size.y;
    focus_workbench_dataset_ = true;
}

gs3d::app::UiActions UiRoot::draw(gs3d::app::AppState& state)
{
    gs3d::app::UiActions actions;
    ThemeId requested_theme = active_theme();
    bool theme_change_requested = false;
    const float ui_scale = ImGui::GetFontSize() / 13.0f;
    prune_workspace_windows(state);
    begin_viewport_frame_shortcuts(state, actions);
    // 全量预加载门禁：数据就绪前只显示加载页（加载好了再进程序）。
    if (draw_preload_gate_if_active(state, ui_scale)) {
        return actions;
    }
    // TIA-111 方向 B：单一布局 + 可折叠侧边栏，移除三套并行布局模式。

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
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(0.0f, 0.0f)
    );
    // ImGui 在 Begin() 内绘制菜单栏底色；必须在 Begin() 前覆盖
    // MenuBarBg，深色主题才不会出现浅色菜单底配浅色文字的情况。
    ImGui::PushStyleColor(
        ImGuiCol_MenuBarBg,
        to_u32(palette::kMenuBg, 255)
    );

    constexpr bool render_workspace = true;
    if (ImGui::Begin(kHostWindowName, nullptr, host_flags)) {
        AppChromeResult chrome_result;
        const bool menu_bar_visible = ImGui::BeginMenuBar();
        if (menu_bar_visible) {
            draw_top_bar(state, actions, ui_scale, chrome_result);
        }
        if (menu_bar_visible) {
            const ImRect menu_rect = ImGui::GetCurrentWindow()->MenuBarRect();
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(menu_rect.Min.x, menu_rect.Max.y - 1.0f),
                ImVec2(menu_rect.Max.x, menu_rect.Max.y - 1.0f),
                to_u32(palette::kBorder, 110), 1.0f);
            ImGui::EndMenuBar();
        }
        if (chrome_result.layout_change_requested) {
            LayoutRegistry::instance().set_active_layout_id(chrome_result.requested_layout_id);
            state.ui_layout_mode = gs3d::app::ui_layout_from_string(chrome_result.requested_layout_id);
            restore_default_workspace(state);
            dock_layout_.initialized = false;
            ImGui::DockBuilderRemoveNode(ImGui::GetID("GeoScatter3D.DockSpace"));
            persist_ui_preferences(state, requested_theme);
        }
        if (chrome_result.theme_change_requested) {
            requested_theme = chrome_result.requested_theme;
            theme_change_requested = true;
        }
        if (chrome_result.restore_default_workspace_requested) {
            restore_default_workspace(state);
            dock_layout_.initialized = false;
            ImGui::DockBuilderRemoveNode(ImGui::GetID("GeoScatter3D.DockSpace"));
        }
        {
            const float content_avail_y = ImGui::GetContentRegionAvail().y;
            const float status_h = std::min(LayoutMetrics::kStatusBarHeightBase * ui_scale, std::max(0.0f, content_avail_y));
            const float dock_h = std::max(0.0f, content_avail_y - status_h);
            build_default_layout(state);
            ImGui::DockSpace(ImGui::GetID("GeoScatter3D.DockSpace"), ImVec2(0.0f, dock_h), ImGuiDockNodeFlags_None);
            ImGui::BeginChild("##StatusBar", ImVec2(0.0f, status_h), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            draw_status_bar(state, ui_scale);
            ImGui::EndChild();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    if (theme_change_requested) {
        apply_theme(requested_theme, gs3d::gui::ui_fonts().ui_scale);
        persist_ui_preferences(state, requested_theme);
    }

    if (render_workspace) {

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
        draw_navigation_map(state);

        for (auto& view : state.render_views) {
            if (view.visible &&
                !view_is_owned_by_workspace(state, view.viewport_index)) {
                draw_viewport_window(state, view, actions, 0, true);
            } else {
                if (!view.visible) {
                    view.detached = false;
                }
                if (!view.visible ||
                    !view_is_owned_by_workspace(
                        state,
                        view.viewport_index
                    )) {
                    view.render_requested = false;
                }
            }
        }
        for (auto& workspace : state.workspace_windows) {
            draw_workspace_window(state, actions, workspace, ui_scale);
        }
        prune_workspace_windows(state);

        draw_auxiliary_panels(state, actions);
        if (focus_workbench_dataset_) {
            ImGui::SetNextWindowFocus();
            focus_workbench_dataset_ = false;
        }
        draw_dataset_panel(state);
        draw_floating_dock_overlay(state, actions, ui_scale);
        draw_analysis_rail_overlay(state, actions, ui_scale);
        draw_screenshot_notice(state, ui_scale);
        draw_shortcut_overlay(state, ui_scale);
        draw_panel_command_palette(state, ui_scale);
    } else {
        for (auto& view : state.render_views) {
            view.render_requested = false;
        }
    }
    finalize_viewport_frame_shortcuts(state, actions);
    return actions;
}

} // namespace gs3d::ui
