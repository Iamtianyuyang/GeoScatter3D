#include "ui/AppChrome.hpp"
#include "ui/UiRoot.hpp"
#include "ui/AnalysisRailUi.hpp"
#include "ui/FloatingDockUi.hpp"
#include "ui/ViewportCanvas.hpp"
#include "ui/WorkbenchUi.hpp"
#include "ui/WorkspaceManager.hpp"
#include "ui/Theme.hpp"
#include "ui/Widgets.hpp"
#include "ui/UiPalette.hpp"
#include "ui/RegionStatsPanel.hpp"
#include "ui/DatasetPanel.hpp"
#include "ui/NavigationMapPanel.hpp"
#include "ui/MeasurementPanel.hpp"
#include "ui/AuxiliaryPanels.hpp"
#include "ui/RenderSettingsPanel.hpp"

#include "gui/UiFonts.hpp"
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

// ── 布局与样式常量（GIS / 地图软件风格）──────────────────────────────
namespace LayoutMetrics {
    constexpr float kDockLeftRatio  = 276.0f / 1360.0f;
    constexpr float kDockLeftMinPx  = 240.0f;
    constexpr float kDockLeftMaxPx  = 336.0f;
    constexpr float kDockRightRatio = 280.0f / 1360.0f;
    constexpr float kDockRightMinPx = 260.0f;
    constexpr float kDockRightMaxPx = 336.0f;
    constexpr float kToolsBarHeightBase = 52.0f;
    constexpr float kStatusBarHeightBase  = 26.0f;
    constexpr float kPanelHeaderGap = 8.0f;
    constexpr float kPanelSectionGap = 8.0f;
    constexpr float kPanelInsetX = 10.0f;
    } // namespace LayoutMetrics

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
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;
    // TIA-159 方向 B：方向 B 模式下隐藏视口标题栏和关闭按钮
    flags |= ImGuiWindowFlags_NoTitleBar;

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
        // TIA-159 方向 B：方向 B 模式下隐藏标题栏和关闭按钮
        ImGui::Begin(window_name.c_str(), nullptr, flags);
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
        0.34f,
        nullptr,
        &left_top_id
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
        workspace_measurement_window_name(workspace.id).c_str(),
        left_top_id
    );
    ImGui::DockBuilderDockWindow(
        workspace_navigation_window_name(workspace.id).c_str(),
        left_bottom_id
    );
    ImGui::DockBuilderDockWindow(
        workspace_render_settings_window_name(workspace.id).c_str(),
        right_id
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

void UiRoot::draw_mode_tabs(gs3d::app::AppState& state, float ui_scale)
{
    // TIA-159 方向 B：左侧模式标签栏
    constexpr float kBtnH = 56.0f;

    struct ModeInfo {
        gs3d::app::UiMode id;
        const char* label;
    };
    static constexpr ModeInfo kModes[] = {
        {gs3d::app::UiMode::kData,        "数据"},
        {gs3d::app::UiMode::kAppearance,  "外观"},
        {gs3d::app::UiMode::kPerformance, "性能"},
        {gs3d::app::UiMode::kTools,       "工具"},
    };

    ImGui::SetCursorScreenPos(ImGui::GetCursorScreenPos());
    ImGui::BeginChild("##ModeTabs", ImVec2(0.0f, 0.0f), false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground);

    const float avail_w = ImGui::GetContentRegionAvail().x;

    for (const auto& m : kModes) {
        const bool is_active = (state.ui_chrome.ui_mode == m.id);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f * ui_scale);
        if (is_active) {
            ImGui::PushStyleColor(ImGuiCol_Button, to_u32(palette::kAccent, 40));
            ImGui::PushStyleColor(ImGuiCol_Text, to_u32(palette::kAccent, 255));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32_DISABLE);
            ImGui::PushStyleColor(ImGuiCol_Text, to_u32(palette::kTextDim, 200));
        }

        char label_buf[32];
        std::snprintf(label_buf, sizeof(label_buf), "%s##mode_%d", m.label, static_cast<int>(m.id));
        if (ImGui::Button(label_buf, ImVec2(avail_w, kBtnH * ui_scale))) {
            state.ui_chrome.ui_mode = m.id;
            dock_layout_.initialized = false;
        }

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
        ImGui::Spacing();
    }

    ImGui::EndChild();
}

void UiRoot::draw_mode_panel_content(gs3d::app::AppState& state, gs3d::app::UiActions& actions, float ui_scale)
{
    // TIA-159 方向 B：根据当前模式渲染对应面板内容
    switch (state.ui_chrome.ui_mode) {
    case gs3d::app::UiMode::kData:
        // 看数据：直接绘制数据集信息（不依赖 dock 窗口）
        draw_dataset_content(state);
        break;
    case gs3d::app::UiMode::kAppearance:
        // 调外观：主题色块 + 完整渲染设置
        draw_theme_selector(state, ui_scale);
        ImGui::Spacing();
        {
            auto& rs = gs3d::app::render_settings_for_view(
                state, state.active_viewport_index);
            draw_panel_section_label("着色");
            // 颜色属性
            ImGui::Text("颜色属性");
            if (!rs.color_by_options.empty()) {
                int ci = rs.color_attr_index;
                if (ci < 0 || ci >= static_cast<int>(rs.color_by_options.size())) ci = 0;
                if (ImGui::BeginCombo("##ColorBy", rs.color_by_options[ci].c_str())) {
                    for (int i = 0; i < static_cast<int>(rs.color_by_options.size()); ++i) {
                        bool selected = (i == ci);
                        if (ImGui::Selectable(rs.color_by_options[i].c_str(), selected))
                            rs.color_attr_index = i;
                    }
                    ImGui::EndCombo();
                }
            }
            // 高度来源
            ImGui::Text("高度来源");
            if (!rs.height_by_options.empty()) {
                int hi = rs.height_attr_index;
                if (hi < 0 || hi >= static_cast<int>(rs.height_by_options.size())) hi = 0;
                if (ImGui::BeginCombo("##HeightSource", rs.height_by_options[hi].c_str())) {
                    for (int i = 0; i < static_cast<int>(rs.height_by_options.size()); ++i) {
                        bool selected = (i == hi);
                        if (ImGui::Selectable(rs.height_by_options[i].c_str(), selected))
                            rs.height_attr_index = i;
                    }
                    ImGui::EndCombo();
                }
            }
            // 点形状
            ImGui::Text("点形状");
            const char* shape_names[] = {"方形", "圆形", "菱形", "三角形"};
            int si = rs.point_shape;
            if (si < 0 || si > 3) si = 0;
            if (ImGui::BeginCombo("##PointShape", shape_names[si])) {
                for (int i = 0; i < 4; ++i) {
                    bool selected = (i == si);
                    if (ImGui::Selectable(shape_names[i], selected))
                        rs.point_shape = i;
                }
                ImGui::EndCombo();
            }
            ImGui::Spacing();
            draw_panel_section_label("大小与透明");
            // 点大小
            ImGui::Text("点大小");
            ImGui::SliderFloat("##PointSize", &rs.point_size, 0.5f, 5.0f, "%.1f");
            // 不透明度
            ImGui::Text("不透明度");
            ImGui::SliderFloat("##Opacity", &rs.opacity, 0.0f, 1.0f, "%.0f%%");
            ImGui::Spacing();
            draw_panel_section_label("色调映射");
            // 色标
            ImGui::Text("色标");
            const char* cmap_names[] = {"Rainbow", "Viridis", "Plasma", "Inferno", "Magma", "Cividis", "Turbo", "Jet", "Rainbow256"};
            int ci2 = rs.colormap_index;
            if (ci2 < 0 || ci2 > 8) ci2 = 0;
            if (ImGui::BeginCombo("##Colormap", cmap_names[ci2])) {
                for (int i = 0; i < 9; ++i) {
                    bool selected = (i == ci2);
                    if (ImGui::Selectable(cmap_names[i], selected))
                        rs.colormap_index = i;
                }
                ImGui::EndCombo();
            }
        }
        break;
    case gs3d::app::UiMode::kPerformance:
        // TIA-159 方向 B：查性能模式 — 性能面板 + 瓦片详情 + LOD 设置
        draw_performance_content(state, actions);
        ImGui::Spacing();
        draw_tile_detail_collapsible(state);
        ImGui::Spacing();
        draw_lod_settings_collapsible(state);
        break;
    case gs3d::app::UiMode::kTools:
        // TIA-159 方向 B：用工具模式 — 测量 + 区域统计
        // 注意：draw_measurement_panel 和 draw_region_stats_panel 会创建自己的 docked 窗口，
        // 在 Direction B 模式下只显示简化的工具信息
        draw_panel_section_label("测量");
        ImGui::Text("在测量模式下 Shift+左键框选区域");
        ImGui::Spacing();
        ImGui::Text("距离显示:");
        ImGui::SameLine();
        if (ImGui::RadioButton("三维", false)) {}
        ImGui::SameLine();
        if (ImGui::RadioButton("平面", true)) {}
        ImGui::SameLine();
        if (ImGui::RadioButton("都显示", false)) {}
        ImGui::Spacing();
        if (widgets::Button("全部删除（保留固定）")) {}
        ImGui::Spacing();
        ImGui::TextDisabled("暂无测量线");
        ImGui::Spacing();
        draw_panel_section_label("区域统计");
        ImGui::TextDisabled("在测量模式下 Shift+左键框选区域");
        break;
    case gs3d::app::UiMode::kCount:
        break;
    }
}

void UiRoot::draw_minimap_embedded(gs3d::app::AppState& state, float ui_scale)
{
    // TIA-159 方向 B：导航图嵌入视口右下角
    // 用 GetWindowPos/GetWindowSize 算坐标，不硬编码像素
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    constexpr float kMinimapW = 130.0f;
    constexpr float kMinimapH = 100.0f;
    constexpr float kMargin = 12.0f;

    const ImVec2 minimap_pos{
        vp->Pos.x + vp->Size.x - kMinimapW - kMargin,
        vp->Pos.y + vp->Size.y - kMinimapH - kMargin - 26.0f * ui_scale // 减去状态栏高度
    };

    ImGui::SetNextWindowPos(minimap_pos);
    ImGui::SetNextWindowSize(ImVec2(kMinimapW, kMinimapH));
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, to_u32(palette::kBorder, 150));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, to_u32(palette::kFrame, 255));

    ImGui::Begin("##MinimapEmbedded", nullptr,
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoNavFocus);

    auto& nm = gs3d::app::navigation_map_for_view(
        state, state.active_viewport_index);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 img_min = ImGui::GetCursorScreenPos();
    const ImVec2 img_max{img_min.x + kMinimapW, img_min.y + kMinimapH};

    if (nm.valid && nm.texture_descriptor != VK_NULL_HANDLE) {
        dl->AddImage(
            static_cast<ImTextureID>(reinterpret_cast<ImU64>(nm.texture_descriptor)),
            img_min, img_max);
    } else {
        dl->AddRectFilled(img_min, img_max, to_u32(palette::kFrame, 255));
    }

    // 视野框
    if (nm.view_rect_valid) {
        const float sx = kMinimapW / nm.tex_w;
        const float sy = kMinimapH / nm.tex_h;
        dl->AddRect(
            {img_min.x + nm.view_rect_min_x * sx, img_min.y + nm.view_rect_min_y * sy},
            {img_min.x + nm.view_rect_max_x * sx, img_min.y + nm.view_rect_max_y * sy},
            to_u32(palette::kRed, 220), 0.0f, 0, 2.0f);
    }

    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

void UiRoot::draw_theme_selector(gs3d::app::AppState& state, float ui_scale)
{
    // TIA-159 方向 B：主题色块选择器（5 个色块横排）
    // 用 InvisibleButton + AddRectFilled 自绘，当前主题用边框高亮
    struct ThemePreview {
        gs3d::ui::ThemeId id;
        const char* label;
        ImU32 bg_color;
        bool dark;
    };

    // 从 ThemeTokens 获取每套主题的背景色
    const ThemePreview themes[] = {
        {gs3d::ui::ThemeId::kCarbonBlue,       "碳蓝·浅",  to_u32(gs3d::ui::theme_tokens(gs3d::ui::ThemeId::kCarbonBlue).bg, 255),       false},
        {gs3d::ui::ThemeId::kCarbonBlueDark,   "碳蓝·深",  to_u32(gs3d::ui::theme_tokens(gs3d::ui::ThemeId::kCarbonBlueDark).bg, 255),   true},
        {gs3d::ui::ThemeId::kDeepGraphite,     "石墨·深",  to_u32(gs3d::ui::theme_tokens(gs3d::ui::ThemeId::kDeepGraphite).bg, 255),     true},
        {gs3d::ui::ThemeId::kInstrumentAmber,  "仪器·琥珀", to_u32(gs3d::ui::theme_tokens(gs3d::ui::ThemeId::kInstrumentAmber).bg, 255),  true},
        {gs3d::ui::ThemeId::kHighContrastLight, "高对比·浅", to_u32(gs3d::ui::theme_tokens(gs3d::ui::ThemeId::kHighContrastLight).bg, 255), false},
    };

    draw_panel_section_label("主题");

    const float avail_w = ImGui::GetContentRegionAvail().x;
    const float block_w = (avail_w - 8.0f) / 2.0f; // 2 列
    const float block_h = 40.0f * ui_scale;
    const float label_h = 14.0f * ui_scale;

    const gs3d::ui::ThemeId current = gs3d::ui::active_theme();

    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 2; ++col) {
            int i = row * 2 + col;
            if (i >= 5) break;
            if (col > 0) ImGui::SameLine(0.0f, 8.0f);

            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const ImVec2 size(block_w, block_h);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(pos, {pos.x + size.x, pos.y + size.y}, themes[i].bg_color, 3.0f * ui_scale);

            if (themes[i].id == current) {
                dl->AddRect({pos.x - 2.0f, pos.y - 2.0f},
                            {pos.x + size.x + 2.0f, pos.y + size.y + 2.0f},
                            to_u32(palette::kAccent, 255), 3.0f * ui_scale, 0, 2.0f);
            }

            ImGui::InvisibleButton(themes[i].label, size);
            if (ImGui::IsItemHovered()) {
                dl->AddRect({pos.x - 1.0f, pos.y - 1.0f},
                            {pos.x + size.x + 1.0f, pos.y + size.y + 1.0f},
                            to_u32(palette::kTextDim, 150), 3.0f * ui_scale, 0, 1.0f);
            }
            if (ImGui::IsItemClicked()) {
                gs3d::ui::apply_theme(themes[i].id, gs3d::gui::ui_fonts().ui_scale);
            }

            // 标签在色块下方完整显示
            const ImVec2 text_pos{pos.x, pos.y + size.y + 3.0f};
            dl->AddText(gs3d::gui::ui_fonts().small, 11.0f * ui_scale,
                        text_pos, to_u32(palette::kText, 220), themes[i].label);
        }
        ImGui::Dummy(ImVec2(0.0f, block_h + label_h + 8.0f));
    }
}

void UiRoot::draw_dataset_content(gs3d::app::AppState& state)
{
    // TIA-159 方向 B：数据模式下直接绘制数据集信息
    const auto& ds = state.dataset;
    draw_panel_section_label("数据集");
    ImGui::Text("名称  %s", ds.active_dataset.c_str());
    ImGui::Text("格式  %s", ds.format.c_str());
    ImGui::Text("点数  %llu", static_cast<unsigned long long>(ds.point_count));
    ImGui::Text("文件  %s", ds.file_size.c_str());
    ImGui::Spacing();
    draw_panel_section_label("空间范围");
    ImGui::Text("X  %s", ds.bounding_box.c_str());
    ImGui::Spacing();
    draw_panel_section_label("属性列表");
    for (const auto& attr : ds.dataset_tree) {
        ImGui::Text("  %s", attr.c_str());
    }
    ImGui::Spacing();
    draw_panel_section_label("快速操作");
    if (widgets::Button("打开文件")) { /* 打开文件对话框 */ }
    ImGui::SameLine();
    if (widgets::Button("截图")) { /* 截图 */ }
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

    // TIA-159 方向 B：三栏布局 — 左侧模式标签(48px) + 右侧面板(300px) + 中间视口
    constexpr float kModeTabsWidth = 48.0f * 1.25f; // 60px at 1.25 scale
    constexpr float kRightPanelWidth = 300.0f * 1.25f; // 375px at 1.25 scale

    const float mode_tabs_ratio = kModeTabsWidth / work_width;
    const float remaining_after_tabs = std::max(1.0f, work_width - kModeTabsWidth);
    const float right_panel_ratio = std::min(
        kRightPanelWidth / remaining_after_tabs,
        0.5f // 最多占剩余空间的一半
    );

    ImGuiID center_id = dockspace_id;

    // 左侧：模式标签栏（不 dock 窗口，只占位）
    const ImGuiID mode_tabs_id = ImGui::DockBuilderSplitNode(
        center_id,
        ImGuiDir_Left,
        mode_tabs_ratio,
        nullptr,
        &center_id
    );
    if (ImGuiDockNode* mode_tabs_node = ImGui::DockBuilderGetNode(mode_tabs_id)) {
        mode_tabs_node->LocalFlags |=
            ImGuiDockNodeFlags_NoTabBar |
            ImGuiDockNodeFlags_NoWindowMenuButton;
    }

    // 右侧：面板内容区
    const ImGuiID right_id = ImGui::DockBuilderSplitNode(
        center_id,
        ImGuiDir_Right,
        right_panel_ratio,
        nullptr,
        &center_id
    );
    if (ImGuiDockNode* right_node = ImGui::DockBuilderGetNode(right_id)) {
        right_node->LocalFlags |=
            ImGuiDockNodeFlags_NoTabBar |
            ImGuiDockNodeFlags_NoWindowMenuButton;
    }

    // 右侧面板 dock 到一个占位窗口
    ImGui::DockBuilderDockWindow("##ModePanel", right_id);

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
        if (chrome_result.theme_change_requested) {
            requested_theme = chrome_result.requested_theme;
            theme_change_requested = true;
        }
        if (chrome_result.restore_default_workspace_requested) {
            restore_default_workspace(state);
            dock_layout_.initialized = false;
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

        // TIA-159 方向 B：绘制模式标签栏（左侧边缘覆盖层）
        {
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            const float mode_tabs_w = 48.0f * ui_scale;
            ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + 40.0f * ui_scale));
            ImGui::SetNextWindowSize(ImVec2(mode_tabs_w, vp->Size.y - 40.0f * ui_scale - 26.0f * ui_scale));
            ImGui::SetNextWindowViewport(vp->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2.0f * ui_scale, 4.0f * ui_scale));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, to_u32(palette::kMenuBg, 255));
            ImGui::Begin("##ModeTabs", nullptr,
                ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoNavFocus);
            draw_mode_tabs(state, ui_scale);
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(3);
        }

        // 视口窗口（中间区域）
        for (auto& view : state.render_views) {
            if (view.visible &&
                !view_is_owned_by_workspace(state, view.viewport_index)) {
                // TIA-159 方向 B：方向 B 模式下不显示旧的 workbench 工具栏
                draw_viewport_window(state, view, actions, 0, false);
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
        // TIA-159 方向 B：方向 B 模式下不绘制旧的 workspace 窗口
        // （旧 workspace 工具栏/面板与新的模式标签栏/右侧面板重叠）
        // for (auto& workspace : state.workspace_windows) {
        //     draw_workspace_window(state, actions, workspace, ui_scale);
        // }
        // prune_workspace_windows(state);

        // TIA-159 方向 B：绘制右侧面板覆盖层（在 viewport 之后，确保在最上层）
        {
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            constexpr float kRightPanelW = 375.0f;
            const float panel_x = vp->Pos.x + vp->Size.x - kRightPanelW;
            ImGui::SetNextWindowPos(ImVec2(panel_x, vp->Pos.y + 40.0f * ui_scale));
            ImGui::SetNextWindowSize(ImVec2(kRightPanelW, vp->Size.y - 40.0f * ui_scale - 26.0f * ui_scale));
            ImGui::SetNextWindowViewport(vp->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * ui_scale, 4.0f * ui_scale));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, to_u32(palette::kSurface, 255));
            ImGui::Begin("##ModePanel", nullptr,
                ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoNavFocus);
            draw_mode_panel_content(state, actions, ui_scale);
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(3);
        }

        // TIA-159 方向 B：面板内容已由 draw_mode_panel_content 绘制
        // draw_auxiliary_panels 和 draw_dataset_panel 不再单独调用
        if (focus_workbench_dataset_) {
            ImGui::SetNextWindowFocus();
            focus_workbench_dataset_ = false;
        }
        // TIA-159 方向 B：所有模式下嵌入导航图到视口右下角
        draw_minimap_embedded(state, ui_scale);
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
