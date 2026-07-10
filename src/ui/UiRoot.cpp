#include "ui/UiRoot.hpp"
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

#include "render/AxisGrid.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace gs3d::ui {

namespace {

constexpr const char* kHostWindowName =
    "GeoScatter3D 工作台###GeoScatter3DWorkspace";
constexpr const char* kToolsWindowName =
    "工具###ToolsPanel";
constexpr const char* kDatasetWindowName =
    "项目###DatasetPanel";
constexpr const char* kRenderSettingsWindowName =
    "属性###RenderSettings";
constexpr const char* kDebugLogWindowName =
    "日志###DebugLog";
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

bool workspace_contains_view(
    const gs3d::app::WorkspaceWindowState& workspace,
    int viewport_index
) {
    return std::find(
        workspace.viewport_indices.begin(),
        workspace.viewport_indices.end(),
        viewport_index
    ) != workspace.viewport_indices.end();
}

bool view_is_owned_by_workspace(
    const gs3d::app::AppState& state,
    int viewport_index
) {
    return std::any_of(
        state.workspace_windows.begin(),
        state.workspace_windows.end(),
        [viewport_index](const auto& workspace) {
            return workspace.visible &&
                   workspace_contains_view(workspace, viewport_index);
        }
    );
}

gs3d::app::MeasurementManager& measurement_for_workspace_id(
    gs3d::app::AppState& state,
    int workspace_id
) {
    if (workspace_id > 0) {
        const auto found = std::find_if(
            state.workspace_windows.begin(),
            state.workspace_windows.end(),
            [workspace_id](const auto& workspace) {
                return workspace.id == workspace_id;
            }
        );
        if (found != state.workspace_windows.end()) {
            return found->components.measurement;
        }
    }
    return state.measurement;
}

int active_view_for_indices(
    const gs3d::app::AppState& state,
    const std::vector<int>& indices,
    int fallback
) {
    if (std::find(
            indices.begin(),
            indices.end(),
            state.active_viewport_index
        ) != indices.end()) {
        return state.active_viewport_index;
    }
    if (!indices.empty()) {
        return indices.front();
    }
    return fallback;
}

std::vector<int> main_workspace_viewports(
    const gs3d::app::AppState& state
) {
    std::vector<int> indices;
    for (const auto& view : state.render_views) {
        if (view.visible &&
            !view_is_owned_by_workspace(state, view.viewport_index)) {
            indices.push_back(view.viewport_index);
        }
    }
    return indices;
}

std::uint32_t visible_view_signature(
    const gs3d::app::AppState& state
) {
    std::uint32_t signature = 0;
    for (const auto& view : state.render_views) {
        if (view.visible &&
            !view_is_owned_by_workspace(state, view.viewport_index) &&
            view.viewport_index >= 0 &&
            view.viewport_index < 24) {
            signature |=
                1u << static_cast<std::uint32_t>(view.viewport_index);
        }
    }
    return signature;
}

bool has_hidden_view(const gs3d::app::AppState& state)
{
    return std::any_of(
        state.render_views.begin(),
        state.render_views.end(),
        [](const auto& view) {
            return !view.visible;
        }
    );
}

int show_first_hidden_view(
    gs3d::app::AppState& state,
    bool force_undock = false
)
{
    const auto hidden = std::find_if(
        state.render_views.begin(),
        state.render_views.end(),
        [](const auto& view) {
            return !view.visible;
        }
    );
    if (hidden == state.render_views.end()) {
        return -1;
    }

    hidden->visible = true;
    hidden->force_undock_next_frame = force_undock;
    hidden->render_requested = false;
    return hidden->viewport_index;
}

int next_workspace_id(const gs3d::app::AppState& state)
{
    int next_id = 1;
    for (const auto& workspace : state.workspace_windows) {
        next_id = std::max(next_id, workspace.id + 1);
    }
    return next_id;
}

bool add_view_to_workspace(
    gs3d::app::AppState& state,
    gs3d::app::WorkspaceWindowState& workspace
) {
    const int view_index = show_first_hidden_view(state);
    if (view_index < 0) {
        return false;
    }
    auto& view = state.render_views[static_cast<std::size_t>(view_index)];
    view.detached = false;
    view.force_undock_next_frame = false;
    workspace.viewport_indices.push_back(view_index);
    workspace.dock_layout_initialized = false;
    return true;
}

bool create_workspace_window(gs3d::app::AppState& state)
{
    gs3d::app::WorkspaceWindowState workspace;
    workspace.id = next_workspace_id(state);
    workspace.components.dataset = state.dataset;
    workspace.components.render_settings = state.render_settings;
    workspace.components.navigation_map = state.navigation_map;
    workspace.components.navigation_map.view_rect_valid = false;
    if (!add_view_to_workspace(state, workspace)) {
        return false;
    }
    workspace.visible = true;
    workspace.dock_layout_initialized = false;
    state.workspace_windows.push_back(std::move(workspace));
    return true;
}

void prune_workspace_windows(gs3d::app::AppState& state)
{
    for (auto& workspace : state.workspace_windows) {
        if (!workspace.visible) {
            for (const int view_index : workspace.viewport_indices) {
                if (view_index >= 0 &&
                    view_index < static_cast<int>(state.render_views.size())) {
                    auto& view =
                        state.render_views[static_cast<std::size_t>(view_index)];
                    view.visible = false;
                    view.detached = false;
                    view.force_undock_next_frame = false;
                    view.render_requested = false;
                }
            }
            workspace.viewport_indices.clear();
            continue;
        }

        workspace.viewport_indices.erase(
            std::remove_if(
                workspace.viewport_indices.begin(),
                workspace.viewport_indices.end(),
                [&](int view_index) {
                    return view_index < 0 ||
                           view_index >=
                               static_cast<int>(state.render_views.size()) ||
                           !state.render_views[
                                static_cast<std::size_t>(view_index)
                            ].visible;
                }
            ),
            workspace.viewport_indices.end()
        );
        if (workspace.viewport_indices.empty()) {
            workspace.visible = false;
        }
    }

    state.workspace_windows.erase(
        std::remove_if(
            state.workspace_windows.begin(),
            state.workspace_windows.end(),
            [](const auto& workspace) {
                return !workspace.visible ||
                       workspace.viewport_indices.empty();
            }
        ),
        state.workspace_windows.end()
    );
}

// ── 布局与样式常量（GIS / 地图软件风格）──────────────────────────────
namespace LayoutMetrics {
    // Dock side-bar widths: ratio + min/max of work width, instead of a
    // fixed pixel value. Derived from the previously hand-tuned baseline
    // (kDockLeftWidth=220, kDockRightWidth=240 at a ~1280px work width, which
    // read as ~0.17 / ~0.19). A ratio keeps the side bars proportional when
    // the window is maximized, while the min/max band prevents them from
    // collapsing on tiny windows or swallowing the center on huge ones.
    constexpr float kDockLeftRatio  = 0.175f;
    constexpr float kDockLeftMinPx  = 210.0f;
    constexpr float kDockLeftMaxPx  = 320.0f;
    constexpr float kDockRightRatio = 0.205f;
    constexpr float kDockRightMinPx = 260.0f;
    constexpr float kDockRightMaxPx = 360.0f;
    constexpr float kToolsBarHeightBase = 44.0f;
    // Status bar: base pixel size, scaled by ui_scale at the call site.
    constexpr float kStatusBarHeightBase  = 22.0f;
    constexpr float kViewportToolbarGap = 6.0f;
    constexpr float kViewportToolbarFramePadX = 5.0f;
    constexpr float kViewportToolbarFramePadY = 2.0f;
    constexpr float kPanelHeaderGap = 8.0f;
    constexpr float kPanelSectionGap = 8.0f;
    constexpr float kPanelInsetX = 10.0f;
    constexpr float kStatusInsetX = 9.0f;
    // 地图轴边距（必须与 UiRoot.hpp compute_plot_rect 一致）
    constexpr float kAxisTopH  = 26.0f;
    constexpr float kAxisLeftW = 46.0f;
    constexpr float kAxisOuterPadding = 6.0f;
    // 信息 badge
    constexpr float kBadgePadX   = 8.0f;
    constexpr float kBadgePadY   = 6.0f;
    constexpr float kBadgeRound  = 4.0f;
    // 方向指示器 — 右下角，缩小并向内移
    constexpr float kGizmoRadius      = 24.0f;
    constexpr float kGizmoInsetRight  = 26.0f;
    constexpr float kGizmoInsetBottom = 26.0f;
    // 比例尺 — 左下角，放在 plot 内部
    constexpr float kScaleBarLen        = 80.0f;
    constexpr float kScaleBarInsetLeft  = 14.0f;
    constexpr float kScaleBarInsetBottom = 14.0f;
} // namespace LayoutMetrics

namespace AxisStyle {
    // 轻量坐标尺风格 — 简洁、克制、低对比。
    // 统一取自 UiPalette（运算符灰/弱文字），已做 sRGB→linear 预转换。
    inline const ImU32 kAxisLine    = to_u32(palette::kGray, 190);
    inline const ImU32 kMajorTick   = to_u32(palette::kGray, 210);
    inline const ImU32 kMinorTick   = to_u32(palette::kTextDim, 160);
    inline const ImU32 kLabel       = to_u32(palette::kGray, 220);
    inline const ImU32 kGrid        = to_u32(palette::kTextDim, 35);
    inline const ImU32 kFrame       = to_u32(palette::kBorder, 80);
    inline const ImU32 kScaleLine   = to_u32(palette::kGray, 200);
    inline const ImU32 kScaleLabel  = to_u32(palette::kGray, 215);
    // 信息 badge
    inline const ImU32 kBadgeBg     = to_u32(palette::kMenuBg, 185);
    inline const ImU32 kBadgeText   = to_u32(palette::kText, 245);
    // 方向指示器
    inline const ImU32 kGizmoBg     = to_u32(palette::kMenuBg, 200);

    // 线宽
    constexpr float kAxisLineWidth   = 1.0f;
    constexpr float kFrameWidth      = 1.0f;
    constexpr float kGridWidth       = 0.5f;
    constexpr float kMajorTickWidth  = 1.0f;
    constexpr float kMinorTickWidth  = 1.0f;

    // 两级刻度
    constexpr float kMajorTickLen   = 8.0f;
    constexpr float kMinorTickLen   = 4.0f;
    constexpr int   kMinorPerMajor  = 4;
    constexpr int   kMajorCountMin  = 4;
    constexpr int   kMajorCountMax  = 6;

    // 标签与刻度线的间距
    constexpr float kXTickToLabel = 4.0f;
    constexpr float kYTickToLabel = 4.0f;
} // namespace AxisStyle

} // namespace

namespace {

struct BadgeOverlay {
    char text[128];
    ImVec2 text_size;
    ImVec2 box_min;
    ImVec2 box_max;
};

bool rects_overlap(const ImVec2& a_min,
                   const ImVec2& a_max,
                   const ImVec2& b_min,
                   const ImVec2& b_max)
{
    return a_min.x < b_max.x &&
           a_max.x > b_min.x &&
           a_min.y < b_max.y &&
           a_max.y > b_min.y;
}

BadgeOverlay make_badge_overlay(
    const gs3d::app::RenderViewState& view,
    const ImVec2& plot_min,
    float ui_scale)
{
    BadgeOverlay badge{};
    std::snprintf(badge.text, sizeof(badge.text), "%llu 点  |  %.2f ms",
        static_cast<unsigned long long>(view.points_visible),
        static_cast<double>(view.frame_time_ms));
    badge.text_size = ImGui::CalcTextSize(badge.text);
    const float bx0 = plot_min.x + LayoutMetrics::kBadgePadX * ui_scale;
    const float by0 = plot_min.y + LayoutMetrics::kBadgePadY * ui_scale;
    badge.box_min = {bx0, by0};
    badge.box_max = {
        bx0 + badge.text_size.x + 16.0f * ui_scale,
        by0 + badge.text_size.y + 10.0f * ui_scale
    };
    return badge;
}

ImFont* regular_font()
{
    return gs3d::gui::ui_fonts().regular;
}

ImFont* small_font()
{
    return gs3d::gui::ui_fonts().small;
}

ImFont* axis_font()
{
    return gs3d::gui::ui_fonts().axis;
}

ImFont* status_font()
{
    return gs3d::gui::ui_fonts().status;
}

void push_application_menu_style(float ui_scale)
{
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(10.0f * ui_scale, 8.0f * ui_scale)
    );
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        ImVec2(10.0f * ui_scale, 5.0f * ui_scale)
    );
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(8.0f * ui_scale, 5.0f * ui_scale)
    );
    ImGui::PushStyleVar(
        ImGuiStyleVar_PopupRounding,
        8.0f * ui_scale
    );
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleVar(
        ImGuiStyleVar_SeparatorTextPadding,
        ImVec2(7.0f * ui_scale, 5.0f * ui_scale)
    );

    ImGui::PushStyleColor(
        ImGuiCol_MenuBarBg,
        to_u32(palette::kMenuBg, 255)
    );
    ImGui::PushStyleColor(
        ImGuiCol_PopupBg,
        to_u32(palette::kSurface, 255)
    );
    ImGui::PushStyleColor(
        ImGuiCol_Header,
        to_u32(palette::kAccent, 18)
    );
    ImGui::PushStyleColor(
        ImGuiCol_HeaderHovered,
        to_u32(palette::kAccent, 32)
    );
    ImGui::PushStyleColor(
        ImGuiCol_HeaderActive,
        to_u32(palette::kAccent, 52)
    );
    ImGui::PushStyleColor(
        ImGuiCol_Border,
        to_u32(palette::kBorder, 180)
    );
    ImGui::PushStyleColor(
        ImGuiCol_Separator,
        to_u32(palette::kBorder, 110)
    );
    ImGui::PushStyleColor(
        ImGuiCol_CheckMark,
        to_u32(palette::kAccent, 255)
    );
}

void pop_application_menu_style()
{
    ImGui::PopStyleColor(8);
    ImGui::PopStyleVar(6);
}

void draw_menu_section_label(const char* label)
{
    if (small_font() != nullptr) {
        ImGui::PushFont(small_font());
    }
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        to_u32(palette::kTextFaint, 210)
    );
    ImGui::SeparatorText(label);
    ImGui::PopStyleColor();
    if (small_font() != nullptr) {
        ImGui::PopFont();
    }
}

void draw_menu_hint(const char* text)
{
    if (small_font() != nullptr) {
        ImGui::PushFont(small_font());
    }
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        to_u32(palette::kTextDim, 190)
    );
    ImGui::BulletText("%s", text);
    ImGui::PopStyleColor();
    if (small_font() != nullptr) {
        ImGui::PopFont();
    }
}

void draw_mock_viewport(const ImVec2& min, const ImVec2& max)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(
        min,
        max,
        to_u32(palette::kViewportBg, 255)
    );

    const float width = max.x - min.x;
    const float height = max.y - min.y;
    for (int i = 0; i < 120; ++i) {
        const float x =
            static_cast<float>((i * 37) % 100) / 100.0f;
        const float y =
            static_cast<float>((i * 53) % 100) / 100.0f;
        const float depth =
            static_cast<float>((i * 17) % 100) / 100.0f;
        draw_list->AddCircleFilled(
            {
                min.x + width * (0.18f + x * 0.64f),
                min.y + height * (0.22f + y * 0.52f) -
                    depth * 28.0f
            },
            1.2f + depth * 1.4f,
            IM_COL32(
                72 + static_cast<int>(depth * 100.0f),
                130 + static_cast<int>(depth * 70.0f),
                215,
                220
            )
        );
    }
}

/*
 * 右下角方向指示器：缩小并内缩到 plot_rect 右下角内部，方向仍由
 * ViewerApp 每帧基于相机 view matrix 计算，所以旋转主视图时 gizmo
 * 同步旋转。
 */
void draw_orientation_gizmo(const gs3d::app::RenderViewState& view,
                            const ImVec2& plot_max,
                            float ui_scale)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const float r = LayoutMetrics::kGizmoRadius * ui_scale;
    const ImVec2 origin{
        plot_max.x - r - LayoutMetrics::kGizmoInsetRight * ui_scale,
        plot_max.y - r - LayoutMetrics::kGizmoInsetBottom * ui_scale
    };

    dl->AddCircleFilled(origin, r, AxisStyle::kGizmoBg);

    if (view.gizmo_axes_valid) {
        const float len = r - 2.0f * ui_scale;
        const auto draw_axis = [&](const gs3d::app::RenderViewState::GizmoAxisEnd& end,
                                   ImU32 color) {
            const float mag = std::sqrt(end.dx * end.dx + end.dy * end.dy);
            if (mag < 1.0e-6f) return;
            const float s = len / mag;
            dl->AddLine(origin,
                        ImVec2(origin.x + end.dx * s,
                               origin.y + end.dy * s),
                        color, 1.5f * ui_scale);
        };
        draw_axis(view.gizmo_x_axis, to_u32(palette::kRed, 220));
        draw_axis(view.gizmo_y_axis, to_u32(palette::kGreen, 220));
        draw_axis(view.gizmo_z_axis, to_u32(palette::kBlue, 220));
    }
}

/*
 * 左下角比例尺：锚定在 plot_rect 内部左下角，与外侧地图轴分层。
 */
void draw_scale_bar(const ImVec2& plot_min,
                    const ImVec2& plot_max,
                    const char* label,
                    float ui_scale)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const float bar_w = LayoutMetrics::kScaleBarLen * ui_scale;
    const float x0 = plot_min.x + LayoutMetrics::kScaleBarInsetLeft * ui_scale;
    const float y  = plot_max.y - LayoutMetrics::kScaleBarInsetBottom * ui_scale;
    const float x1 = x0 + bar_w;
    const float tick_h = 4.0f * ui_scale;
    const float thick = 1.5f * ui_scale;

    dl->AddLine({x0, y}, {x1, y}, AxisStyle::kScaleLine, thick);
    dl->AddLine({x0, y - tick_h}, {x0, y + 1.0f * ui_scale}, AxisStyle::kScaleLine, thick);
    dl->AddLine({x1, y - tick_h}, {x1, y + 1.0f * ui_scale}, AxisStyle::kScaleLine, thick);
    if (axis_font() != nullptr) {
        ImGui::PushFont(axis_font());
    }
    dl->AddText({x0, y - 18.0f * ui_scale}, AxisStyle::kScaleLabel, label);
    if (axis_font() != nullptr) {
        ImGui::PopFont();
    }
}

/*
 * 视图叠加层：信息 badge + 比例尺 + 方向指示器，全部相对 plot_rect 布局。
 */
void draw_viewport_overlay(
    const gs3d::app::RenderViewState& view,
    const ImVec2& plot_min,
    const ImVec2& plot_max,
    float ui_scale)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const BadgeOverlay badge = make_badge_overlay(view, plot_min, ui_scale);
    dl->AddRectFilled(
        badge.box_min, badge.box_max,
        AxisStyle::kBadgeBg, LayoutMetrics::kBadgeRound * ui_scale);
    if (small_font() != nullptr) {
        ImGui::PushFont(small_font());
    }
    dl->AddText(
        {badge.box_min.x + 8.0f * ui_scale, badge.box_min.y + 5.0f * ui_scale},
        AxisStyle::kBadgeText,
        badge.text);
    if (small_font() != nullptr) {
        ImGui::PopFont();
    }

    draw_scale_bar(plot_min, plot_max, view.scale.c_str(), ui_scale);
    draw_orientation_gizmo(view, plot_max, ui_scale);
}

void draw_tools_window(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    float ui_scale,
    const char* window_name = kToolsWindowName,
    gs3d::app::WorkspaceWindowState* workspace = nullptr
) {
    const bool use_default_window = workspace == nullptr;
    if (use_default_window && !state.panels.tools) {
        return;
    }

    ImGui::SetNextWindowSize(
        ImVec2(420.0f, 68.0f * ui_scale),
        ImGuiCond_FirstUseEver
    );
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;
    bool* open = use_default_window ? &state.panels.tools : nullptr;
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

        const bool can_add_view = has_hidden_view(state);
        ImGui::BeginDisabled(!can_add_view);
        if (widgets::Chip("+ 视图")) {
            if (workspace != nullptr) {
                add_view_to_workspace(state, *workspace);
            } else {
                show_first_hidden_view(state);
            }
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
        if (ImGui::GetContentRegionAvail().x >
            200.0f * ui_scale) {
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                to_u32(palette::kTextDim, 150)
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
    gs3d::app::RenderViewState& view,
    gs3d::app::UiActions& actions,
    int workspace_id = 0
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

    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        ImVec2(LayoutMetrics::kViewportToolbarFramePadX,
               LayoutMetrics::kViewportToolbarFramePadY)
    );
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(LayoutMetrics::kViewportToolbarGap, 4.0f)
    );
    ImGui::TextDisabled(
        view.detached ? "独立窗口" : "工作区"
    );
    ImGui::SameLine();
    if (widgets::Chip("复位视角")) {
        actions.reset_camera_index = view.viewport_index;
    }
    ImGui::SameLine();
    widgets::Checkbox("联动相机", &view.camera_linked);
    ImGui::SameLine();
    if (widgets::Checkbox("地图轴", &view.show_map_axis)) {
        if (view.show_map_axis) {
            view.show_world_axis = false;
        }
    }
    ImGui::SameLine();
    if (widgets::Checkbox("世界轴", &view.show_world_axis)) {
        if (view.show_world_axis) {
            view.show_map_axis = false;
        }
    }
    ImGui::SameLine();
    if (widgets::Checkbox("十字准线", &view.show_crosshair)) {
        if (view.show_crosshair && !view.show_map_axis) {
            view.show_map_axis = true;
            view.show_world_axis = false;
        }
    }
    ImGui::SameLine();
    {
        ImVec4 ch = ImGui::ColorConvertU32ToFloat4(view.crosshair_color);
        float ch_arr[4] = {ch.x, ch.y, ch.z, ch.w};
        ImGui::SetNextItemWidth(22.0f);
        if (ImGui::ColorEdit4("##CrosshairColor", ch_arr,
                ImGuiColorEditFlags_NoInputs |
                ImGuiColorEditFlags_NoLabel)) {
            view.crosshair_color = ImGui::ColorConvertFloat4ToU32(
                ImVec4(ch_arr[0], ch_arr[1], ch_arr[2], ch_arr[3]));
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("十字准线颜色");
    }
    ImGui::SameLine();
    if (widgets::Chip("十字颜色")) {
        view.crosshair_color = IM_COL32(0xF1, 0xC2, 0x1B, 0xFF);
    }
    ImGui::SameLine();
    {
        ImVec4 rt = ImGui::ColorConvertU32ToFloat4(view.reticle_color);
        float rt_arr[4] = {rt.x, rt.y, rt.z, rt.w};
        ImGui::SetNextItemWidth(22.0f);
        if (ImGui::ColorEdit4("##ReticleColor", rt_arr,
                ImGuiColorEditFlags_NoInputs |
                ImGuiColorEditFlags_NoLabel)) {
            view.reticle_color = ImGui::ColorConvertFloat4ToU32(
                ImVec4(rt_arr[0], rt_arr[1], rt_arr[2], rt_arr[3]));
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("拾取准星颜色");
    }
    ImGui::SameLine();
    if (widgets::Chip("准星颜色")) {
        view.reticle_color = IM_COL32(0xF1, 0xC2, 0x1B, 0xFF);
    }
    const char* long_hint =
        "左键旋转  右键平移  滚轮光标缩放  "
        "双击定轴  F聚焦  Ctrl+左键框选";
    const char* short_hint = "左键旋转  右键平移  滚轮缩放  F聚焦";
    ImGui::SameLine();
    const float hint_space = ImGui::GetContentRegionAvail().x;
    const char* hint = nullptr;
    if (hint_space >= ImGui::CalcTextSize(long_hint).x) {
        hint = long_hint;
    } else if (hint_space >= ImGui::CalcTextSize(short_hint).x) {
        hint = short_hint;
    }
    if (hint != nullptr) {
        ImGui::PushStyleColor(ImGuiCol_Text, to_u32(palette::kTextDim, 125));
        ImGui::TextUnformatted(hint);
        ImGui::PopStyleColor();
    } else {
        ImGui::NewLine();
    }
    ImGui::PopStyleVar(2);
    ImGui::Separator();

    ImVec2 available = ImGui::GetContentRegionAvail();
    available.x = std::max(1.0f, available.x);
    available.y = std::max(1.0f, available.y);

    ImGui::InvisibleButton(
        "##ViewportCanvas",
        available,
        ImGuiButtonFlags_MouseButtonLeft |
        ImGuiButtonFlags_MouseButtonRight |
        ImGuiButtonFlags_MouseButtonMiddle
    );
    const ImVec2 canvas_min = ImGui::GetItemRectMin();
    const ImVec2 canvas_max = ImGui::GetItemRectMax();
    const ViewportScreenRect canvas_rect{
        canvas_min.x,
        canvas_min.y,
        canvas_max.x,
        canvas_max.y
    };
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    // UI scale for high-DPI: use font size relative to default 13 px.
    const float ui_scale = ImGui::GetFontSize() / 13.0f;

    // ── 地图轴模式：点在缩小的 plot_rect 内显示，轴在边距中绘制 ──
    const auto plot_rect = compute_plot_rect(
        view.show_map_axis,
        canvas_rect,
        ui_scale
    );
    ImVec2 plot_min{plot_rect.min_x, plot_rect.min_y};
    ImVec2 plot_max{plot_rect.max_x, plot_rect.max_y};
    ImDrawList* canvas_dl = ImGui::GetWindowDrawList();
    canvas_dl->AddRectFilled(
        canvas_min,
        canvas_max,
        ImGui::GetColorU32(ImGuiCol_WindowBg)
    );
    canvas_dl->AddRectFilled(
        plot_min,
        plot_max,
        to_u32(palette::kViewportBg, 255)
    );

    // ── 测量模式视口边框提示 ──
    if (view.measure_mode_active) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 kMeasureBorder = to_u32(palette::kYellow, 180);
        const float kBorderWidth = 3.5f * ui_scale;
        dl->AddRect(plot_min, plot_max, kMeasureBorder, 0.0f, 0, kBorderWidth);

        const float kBadgePadX = 8.0f * ui_scale;
        const float kBadgePadY = 5.0f * ui_scale;
        const ImU32 kBadgeBg = to_u32(palette::kYellow, 230);
        const ImU32 kBadgeText = to_u32(palette::kText, 255);
        const char* badge_label = "测量模式  中键量距  Shift框选统计";
        const ImVec2 ts = ImGui::CalcTextSize(badge_label);
        const ImVec2 badge_min{
            plot_min.x + kBadgePadX,
            plot_min.y + kBadgePadY};
        const ImVec2 badge_max{
            badge_min.x + ts.x + kBadgePadX * 2.0f,
            badge_min.y + ts.y + kBadgePadY * 2.0f};
        dl->AddRectFilled(badge_min, badge_max, kBadgeBg, 4.0f * ui_scale);
        dl->AddText(
            ImVec2(badge_min.x + kBadgePadX, badge_min.y + kBadgePadY),
            kBadgeText, badge_label);
    }

    if (view.show_live_image &&
        view.descriptor != VK_NULL_HANDLE) {
        ImGui::GetWindowDrawList()->AddImage(
            static_cast<ImTextureID>(
                reinterpret_cast<ImU64>(view.descriptor)
            ),
            plot_min,
            plot_max
        );
    } else {
        draw_mock_viewport(plot_min, plot_max);
    }

    // ── 地图式坐标轴 — 轻量 overlay ────────────
    if (view.show_map_axis) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(canvas_min, canvas_max, true);
        const BadgeOverlay badge = make_badge_overlay(view, plot_min, ui_scale);
        const float axis_outer_pad = LayoutMetrics::kAxisOuterPadding * ui_scale;

        // 轴线：顶部 X 轴 + 左侧 Y 轴，全部保持在 plot 外侧科学绘图风格
        dl->AddLine(ImVec2(plot_min.x, plot_min.y),
                    ImVec2(plot_max.x, plot_min.y),
                    AxisStyle::kAxisLine, AxisStyle::kAxisLineWidth);
        dl->AddLine(ImVec2(plot_min.x, plot_min.y),
                    ImVec2(plot_min.x, plot_max.y),
                    AxisStyle::kAxisLine, AxisStyle::kAxisLineWidth);

        const float x_range = view.map_axis_x_max - view.map_axis_x_min;
        const float y_range = view.map_axis_y_max - view.map_axis_y_min;

        // 两级刻度：major（长刻度+标签+网格），minor（短刻度，无标签无网格）。
        // 每个 major interval 细分为 kMinorPerMajor 个 minor step，
        // 保证坐标轴读数更细，而背景网格线仍稀疏。
        const int major_cnt = std::clamp(
            static_cast<int>(std::ceil(
                std::max(x_range, y_range) > 0.0f ? 5.0f : 4.0f)),
            AxisStyle::kMajorCountMin, AxisStyle::kMajorCountMax);

        // ---- 辅助 lambda：根据刻度步长选择小数位数 ----
        const auto fmt_label = [](char* buf, std::size_t buf_size,
                                  float tick, double origin_offset,
                                  float major_step) {
            // Choose precision based on step magnitude so that
            // zoomed-in views show decimals and zoomed-out views
            // stay compact.
            int prec = 0;
            if (major_step < 1.0f) {
                prec = static_cast<int>(
                    std::ceil(-std::log10(std::max(major_step, 1.0e-6f))));
                if (prec < 0) prec = 0;
                if (prec > 6) prec = 6;
            }
            std::snprintf(buf, buf_size, "%.*f", prec,
                static_cast<double>(tick) + origin_offset);
        };

        // ---- 辅助 lambda：生成 minor ticks ----
        //
        // 必须用 double + 整数索引步进：之前用 `v += minor_step` 的
        // float 累加，在缩放视野下（如 first_major≈-14544、
        // minor_step≈0.002）float 的精度间隙已经 >= step，循环永不
        // 终止 → 主线程死循环 → UI 卡死。这里把迭代改成 `i * step`
        // 索引步进，强制上限，并处理退化（range 极小而 step 极小）。
        const auto make_minors = [](float major_step, float first_major,
                                     float range_min, float range_max)
            -> std::vector<float>
        {
            std::vector<float> minors;
            if (major_step <= 0.0f || !std::isfinite(major_step)) {
                return minors;
            }
            const float minor_step =
                major_step / static_cast<float>(AxisStyle::kMinorPerMajor);
            if (minor_step <= 0.0f || !std::isfinite(minor_step)) {
                return minors;
            }

            constexpr int kMaxMinorsPerSide = 200;

            const double minor_step_d = static_cast<double>(minor_step);
            const double first_major_d = static_cast<double>(first_major);
            const double range_min_d = static_cast<double>(range_min);
            const double range_max_d = static_cast<double>(range_max);
            // 阈值用 double 算，避免在边界上漏一根 / 多一根
            const double end_threshold =
                minor_step_d * 0.5;

            // 向左（向 range_min）
            int left_count = static_cast<int>(
                std::floor((first_major_d - range_min_d) / minor_step_d));
            if (left_count < 0) left_count = 0;
            if (left_count > kMaxMinorsPerSide) left_count = kMaxMinorsPerSide;
            minors.reserve(
                static_cast<std::size_t>(left_count + kMaxMinorsPerSide));
            for (int i = 1; i <= left_count; ++i) {
                const double v = first_major_d -
                                 static_cast<double>(i) * minor_step_d;
                if (v <= range_min_d + end_threshold) {
                    break;
                }
                minors.push_back(static_cast<float>(v));
            }

            // 向右（向 range_max）
            int right_count = static_cast<int>(
                std::floor((range_max_d - first_major_d) / minor_step_d));
            if (right_count < 0) right_count = 0;
            if (right_count > kMaxMinorsPerSide) {
                right_count = kMaxMinorsPerSide;
            }
            for (int i = 1; i <= right_count; ++i) {
                const double v = first_major_d +
                                 static_cast<double>(i) * minor_step_d;
                if (v >= range_max_d - end_threshold) {
                    break;
                }
                minors.push_back(static_cast<float>(v));
            }

            // 退化降级：上面两侧迭代都已经有迭代上限 kMaxMinorsPerSide
            // 作为死循环安全网。若 range_min/range_max 与 first_major
            // 都几乎重合（极度病态视野），仍保证输出最多 2*kMaxMinorsPerSide
            // 个 tick，且都不会让 v += step 的累加逻辑出现。
            if (minors.size() >
                static_cast<std::size_t>(2 * kMaxMinorsPerSide)) {
                minors.resize(2 * kMaxMinorsPerSide);
            }
            return minors;
        };

        // ---- X 轴（顶部）----
        if (x_range > 0.0f) {
            const auto x_major = gs3d::render::compute_axis_ticks(
                view.map_axis_x_min, view.map_axis_x_max, major_cnt);
            const float x_major_step = (x_major.size() >= 2)
                ? (x_major[1] - x_major[0]) : 1.0f;

            // 弱网格线（仅 major 位置）
            for (const float tick : x_major) {
                const float t = (tick - view.map_axis_x_min) / x_range;
                const float px = plot_min.x + t * (plot_max.x - plot_min.x);
                dl->AddLine(ImVec2(px, plot_min.y), ImVec2(px, plot_max.y),
                            AxisStyle::kGrid, AxisStyle::kGridWidth);
            }

            const float tick_len_major = AxisStyle::kMajorTickLen * ui_scale;
            const float tick_len_minor = AxisStyle::kMinorTickLen * ui_scale;
            const float label_gap = AxisStyle::kXTickToLabel * ui_scale;

            // Major ticks 向上 + 标签在顶部外侧
            for (const float tick : x_major) {
                const float t = (tick - view.map_axis_x_min) / x_range;
                const float px = plot_min.x + t * (plot_max.x - plot_min.x);

                dl->AddLine(
                    ImVec2(px, plot_min.y),
                    ImVec2(px, plot_min.y - tick_len_major),
                    AxisStyle::kMajorTick, AxisStyle::kMajorTickWidth);

                char label[32];
                fmt_label(label, sizeof(label), tick,
                    view.map_axis_origin_x, x_major_step);
                if (axis_font() != nullptr) {
                    ImGui::PushFont(axis_font());
                }
                const ImVec2 ts = ImGui::CalcTextSize(label);
                const ImVec2 label_min{
                    px - ts.x * 0.5f,
                    std::max(canvas_min.y + axis_outer_pad,
                             plot_min.y - tick_len_major - label_gap - ts.y)
                };
                const ImVec2 label_max{
                    label_min.x + ts.x,
                    label_min.y + ts.y
                };
                if (!rects_overlap(label_min, label_max,
                                   badge.box_min, badge.box_max)) {
                    dl->AddText(label_min, AxisStyle::kLabel, label);
                }
                if (axis_font() != nullptr) {
                    ImGui::PopFont();
                }
            }

            // Minor ticks 向上
            if (x_major.size() >= 2) {
                const auto x_minors = make_minors(
                    x_major_step, x_major.front(),
                    view.map_axis_x_min, view.map_axis_x_max);
                for (const float tick : x_minors) {
                    const float t = (tick - view.map_axis_x_min) / x_range;
                    const float px = plot_min.x + t * (plot_max.x - plot_min.x);
                    dl->AddLine(
                        ImVec2(px, plot_min.y),
                        ImVec2(px, plot_min.y - tick_len_minor),
                        AxisStyle::kMinorTick, AxisStyle::kMinorTickWidth);
                }
            }
        }

        // ---- Y 轴（左侧）----
        // --- diagnostic: Y-axis visibility trace (off by default) ---
        constexpr bool kYAxisDiag = false;  // set true to enable
        if (kYAxisDiag) {
            static int ydiag_count = 0;
            if (++ydiag_count % 30 == 0) {
                std::fprintf(stderr,
                    "[YAXIS] frame=%d y_range=%.6f "
                    "y_min=%.2f y_max=%.2f "
                    "plot_y=[%.1f,%.1f] major_cnt=%d\n",
                    ydiag_count,
                    static_cast<double>(y_range),
                    static_cast<double>(view.map_axis_y_min),
                    static_cast<double>(view.map_axis_y_max),
                    static_cast<double>(plot_min.y),
                    static_cast<double>(plot_max.y),
                    major_cnt);
            }
        }
        if (y_range > 0.0f) {
            const auto y_major = gs3d::render::compute_axis_ticks(
                view.map_axis_y_min, view.map_axis_y_max, major_cnt);
            if (kYAxisDiag) {
                static int ydiag_count2 = 0;
                if (++ydiag_count2 % 30 == 0) {
                    std::fprintf(stderr,
                        "[YAXIS] y_major cnt=%zu step=%.2f "
                        "first=%.2f last=%.2f\n",
                        y_major.size(),
                        y_major.size() >= 2
                            ? static_cast<double>(y_major[1] - y_major[0])
                            : -1.0,
                        y_major.empty()
                            ? 0.0
                            : static_cast<double>(y_major.front()),
                        y_major.empty()
                            ? 0.0
                            : static_cast<double>(y_major.back()));
                    for (size_t ti = 0; ti < std::min(y_major.size(), size_t{4}); ++ti) {
                        const float t = (y_major[ti] - view.map_axis_y_min) / y_range;
                        const float py = plot_max.y - t * (plot_max.y - plot_min.y);
                        std::fprintf(stderr,
                            "[YAXIS]   tick[%zu]=%.2f t=%.4f py=%.1f "
                            "in_plot=%s\n",
                            ti,
                            static_cast<double>(y_major[ti]),
                            static_cast<double>(t),
                            static_cast<double>(py),
                            (py >= plot_min.y && py <= plot_max.y) ? "YES" : "NO");
                    }
                }
            }
            const float y_major_step = (y_major.size() >= 2)
                ? (y_major[1] - y_major[0]) : 1.0f;

            // 网格线：只在 major 位置画
            for (const float tick : y_major) {
                const float t = (tick - view.map_axis_y_min) / y_range;
                const float py = plot_max.y - t * (plot_max.y - plot_min.y);
                dl->AddLine(ImVec2(plot_min.x, py), ImVec2(plot_max.x, py),
                            AxisStyle::kGrid, AxisStyle::kGridWidth);
            }

            const float tick_len_major = AxisStyle::kMajorTickLen * ui_scale;
            const float tick_len_minor = AxisStyle::kMinorTickLen * ui_scale;
            const float label_gap = AxisStyle::kYTickToLabel * ui_scale;

            // Major ticks 向左 + 标签在左侧外部
            for (const float tick : y_major) {
                const float t = (tick - view.map_axis_y_min) / y_range;
                const float py = plot_max.y - t * (plot_max.y - plot_min.y);

                dl->AddLine(
                    ImVec2(plot_min.x, py),
                    ImVec2(plot_min.x - tick_len_major, py),
                    AxisStyle::kMajorTick, AxisStyle::kMajorTickWidth);

                char label[32];
                fmt_label(label, sizeof(label), tick,
                    view.map_axis_origin_y, y_major_step);
                if (axis_font() != nullptr) {
                    ImGui::PushFont(axis_font());
                }
                const ImVec2 ts = ImGui::CalcTextSize(label);
                dl->AddText(
                    ImVec2(std::max(canvas_min.x + axis_outer_pad,
                                    plot_min.x - tick_len_major -
                                    label_gap - ts.x),
                           py - ts.y * 0.5f),
                    AxisStyle::kLabel, label);
                if (axis_font() != nullptr) {
                    ImGui::PopFont();
                }
            }

            // Minor ticks 向左
            if (y_major.size() >= 2) {
                const auto y_minors = make_minors(
                    y_major_step, y_major.front(),
                    view.map_axis_y_min, view.map_axis_y_max);
                for (const float tick : y_minors) {
                    const float t = (tick - view.map_axis_y_min) / y_range;
                    const float py = plot_max.y - t * (plot_max.y - plot_min.y);
                    dl->AddLine(
                        ImVec2(plot_min.x, py),
                        ImVec2(plot_min.x - tick_len_minor, py),
                        AxisStyle::kMinorTick, AxisStyle::kMinorTickWidth);
                }
            }
        }

        // ── 悬停十字准线 ──
        // Crosshair spanning the full plot area, with coordinate
        // readout at the axis intersection points.
        // Only active when both show_crosshair and show_map_axis are on.
        if (view.show_crosshair &&
            view.hover_tooltip_visible &&
            view.hover_screen_x >= 0.0f &&
            view.hover_screen_y >= 0.0f &&
            view.image_width > 0 && view.image_height > 0) {
            const auto scr = framebuffer_to_plot_screen(
                view.hover_screen_x, view.hover_screen_y,
                canvas_rect, view.show_map_axis,
                view.image_width, view.image_height,
                ui_scale);
            const float cx = plot_min.x + scr.x;
            const float cy = plot_min.y + scr.y;

            const ImU32 kCrosshairLine  = srgb_u32_to_linear(view.crosshair_color) & 0x00FFFFFF | (80u << 24);
            const ImU32 kCrosshairBg    = to_u32(palette::kViewportBg, 200);
            const ImU32 kCrosshairText  = srgb_u32_to_linear(view.crosshair_color) & 0x00FFFFFF | (240u << 24);
            const float kCrosshairWidth = 1.0f * ui_scale;
            const float kLabelPad = 3.0f * ui_scale;
            const float kLabelAxisGap = 3.0f * ui_scale;

            dl->AddLine(ImVec2(plot_min.x, cy), ImVec2(plot_max.x, cy),
                        kCrosshairLine, kCrosshairWidth);
            dl->AddLine(ImVec2(cx, plot_min.y), ImVec2(cx, plot_max.y),
                        kCrosshairLine, kCrosshairWidth);
            dl->AddLine(ImVec2(cx, plot_min.y), ImVec2(cx, plot_max.y),
                        kCrosshairLine, kCrosshairWidth);

            // Precision from axis major-step estimate, same family as
            // fmt_label used by tick labels.
            const auto hover_prec = [](float step) -> int {
                if (step <= 0.0f || step >= 1.0f) return 0;
                int p = static_cast<int>(
                    std::ceil(-std::log10(
                        static_cast<double>(std::max(step, 1.0e-6f)))));
                return std::clamp(p, 0, 6);
            };

            const float x_step = (x_range > 0.0f)
                ? x_range / static_cast<float>(std::max(major_cnt, 1))
                : 1.0f;
            const float y_step = (y_range > 0.0f)
                ? y_range / static_cast<float>(std::max(major_cnt, 1))
                : 1.0f;

            char label_x[32], label_y[32];
            std::snprintf(label_x, sizeof(label_x), "%.*f",
                hover_prec(x_step),
                static_cast<double>(view.hover_x));
            std::snprintf(label_y, sizeof(label_y), "%.*f",
                hover_prec(y_step),
                static_cast<double>(view.hover_y));

            if (axis_font() != nullptr) {
                ImGui::PushFont(axis_font());
            }
            const ImVec2 tx = ImGui::CalcTextSize(label_x);
            const ImVec2 ty = ImGui::CalcTextSize(label_y);

            // X value: at top-axis intersection (vertical line meets X axis).
            // Background box above axis line, outside plot.
            {
                const float bg_min_x = cx - tx.x * 0.5f - kLabelPad;
                const float bg_max_x = cx + tx.x * 0.5f + kLabelPad;
                const float bg_min_y = plot_min.y - tx.y - kLabelPad * 2.0f
                                       - kLabelAxisGap;
                const float bg_max_y = plot_min.y - kLabelAxisGap;
                dl->AddRectFilled(
                    ImVec2(bg_min_x, bg_min_y),
                    ImVec2(bg_max_x, bg_max_y),
                    kCrosshairBg, 3.0f * ui_scale);
                dl->AddText(
                    ImVec2(cx - tx.x * 0.5f, bg_min_y + kLabelPad),
                    kCrosshairText, label_x);
            }

            // Y value: at left-axis intersection (horizontal line meets Y axis).
            // Background box left of axis line, outside plot.
            {
                const float bg_min_x = plot_min.x - ty.x - kLabelPad * 2.0f
                                       - kLabelAxisGap;
                const float bg_max_x = plot_min.x - kLabelAxisGap;
                const float bg_min_y = cy - ty.y * 0.5f - kLabelPad;
                const float bg_max_y = cy + ty.y * 0.5f + kLabelPad;
                dl->AddRectFilled(
                    ImVec2(bg_min_x, bg_min_y),
                    ImVec2(bg_max_x, bg_max_y),
                    kCrosshairBg, 3.0f * ui_scale);
                dl->AddText(
                    ImVec2(bg_min_x + kLabelPad, cy - ty.y * 0.5f),
                    kCrosshairText, label_y);
            }

            if (axis_font() != nullptr) {
                ImGui::PopFont();
            }
        }

        dl->PopClipRect();
    }

    // ── 三维世界坐标轴（QGIS 包围盒，随相机旋转）──
    if (view.show_world_axis) {
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const ImU32 kFrameColor  = to_u32(palette::kGray, 180);
        const ImU32 kTickColor   = to_u32(palette::kGray, 140);
        const ImU32 kLabelColor  = to_u32(palette::kText, 200);

        draw_list->PushClipRect(canvas_min, canvas_max, true);

        for (std::size_t i = 0; i < view.axis_lines.size(); ++i) {
            const auto& seg = view.axis_lines[i];
            const ImU32 color = i < 12 ? kFrameColor : kTickColor;
            const float thickness = i < 12 ? 1.5f : 1.0f;
            draw_list->AddLine(
                ImVec2(canvas_min.x + seg.x0, canvas_min.y + seg.y0),
                ImVec2(canvas_min.x + seg.x1, canvas_min.y + seg.y1),
                color,
                thickness
            );
        }

        for (const auto& label : view.axis_tick_labels) {
            draw_list->AddText(
                ImVec2(canvas_min.x + label.x, canvas_min.y + label.y),
                kLabelColor,
                label.text.c_str()
            );
        }

        draw_list->PopClipRect();
    }

    draw_viewport_overlay(view, plot_min, plot_max, ui_scale);


    // ── 悬浮高亮标记 ──
    // Draws a crosshair+ring at the pick hit-point.  No cursor-movement
    // freshness gate — the pick result's has_hit is the single source of
    // truth.  The marker naturally clears when the next pick has no hit.
    if (view.hover_tooltip_visible &&
        view.hover_screen_x >= 0.0f &&
        view.hover_screen_y >= 0.0f &&
        view.image_width > 0 && view.image_height > 0) {
        const auto scr = framebuffer_to_plot_screen(
            view.hover_screen_x, view.hover_screen_y,
            canvas_rect, view.show_map_axis,
            view.image_width, view.image_height,
            ui_scale);
        const float cx = plot_min.x + scr.x;
        const float cy = plot_min.y + scr.y;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float kR = 8.0f * ui_scale;
        const ImU32 kColor = srgb_u32_to_linear(view.reticle_color) & 0x00FFFFFF | (220u << 24);
        const float kThick = 2.0f * ui_scale;
        dl->AddLine({cx - kR, cy}, {cx + kR, cy}, kColor, kThick);
        dl->AddLine({cx, cy - kR}, {cx, cy + kR}, kColor, kThick);
        dl->AddCircle({cx, cy}, kR + 2.0f * ui_scale, kColor, 0, kThick * 0.7f);

        // "已复制" feedback overlay
        if (view.copy_feedback_frames > 0) {
            const float kFeedbackPad = 4.0f * ui_scale;
            const ImU32 kFeedbackBg = to_u32(palette::kGreen, 220);
            const ImU32 kFeedbackText = to_u32(palette::kText, 255);
            const char* feedback = "已复制";
            const ImVec2 fs = ImGui::CalcTextSize(feedback);
            const float fb_x = cx - fs.x * 0.5f;
            const float fb_y = cy - kR - fs.y - kFeedbackPad * 2.0f - 8.0f * ui_scale;
            dl->AddRectFilled(
                ImVec2(fb_x - kFeedbackPad, fb_y - kFeedbackPad),
                ImVec2(fb_x + fs.x + kFeedbackPad, fb_y + fs.y + kFeedbackPad),
                kFeedbackBg, 3.0f * ui_scale);
            dl->AddText(ImVec2(fb_x, fb_y), kFeedbackText, feedback);
        }
    }

    if (view.selected_point_visible &&
        view.selected_screen_x >= 0.0f &&
        view.selected_screen_y >= 0.0f &&
        view.image_width > 0 && view.image_height > 0) {
        const auto scr = framebuffer_to_plot_screen(
            view.selected_screen_x, view.selected_screen_y,
            canvas_rect, view.show_map_axis,
            view.image_width, view.image_height,
            ui_scale);
        const float cx = plot_min.x + scr.x;
        const float cy = plot_min.y + scr.y;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 kSelectedColor = to_u32(palette::kVarBlue, 240);
        dl->AddCircle({cx, cy}, 12.0f * ui_scale, kSelectedColor, 0, 2.5f * ui_scale);
        dl->AddCircleFilled({cx, cy}, 3.0f * ui_scale, kSelectedColor);
    }

    // ── 测量线绘制 ──
    // Pre-projected by ViewerApp each frame from world coords via the
    // same MouseRay::world_to_screen pipeline the crosshair uses.
    if (!view.measurement_overlays.empty() &&
        view.image_width > 0 && view.image_height > 0) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(plot_min, plot_max, true);
        constexpr float kMeasureLineWidth = 2.0f;
        constexpr float kMeasureLabelPad = 3.0f;
        const ImU32 kMeasureLabelBg = to_u32(palette::kViewportBg, 200);

        for (const auto& overlay : view.measurement_overlays) {
            if (!overlay.visible) continue;

            const auto sa = framebuffer_to_plot_screen(
                overlay.a_screen_x, overlay.a_screen_y,
                canvas_rect, view.show_map_axis,
                view.image_width, view.image_height,
                ui_scale);
            const auto sb = framebuffer_to_plot_screen(
                overlay.b_screen_x, overlay.b_screen_y,
                canvas_rect, view.show_map_axis,
                view.image_width, view.image_height,
                ui_scale);

            const ImVec2 pa{plot_min.x + sa.x, plot_min.y + sa.y};
            const ImVec2 pb{plot_min.x + sb.x, plot_min.y + sb.y};

            // overlay.color 按 sRGB 值存储（颜色选择器所见），绘制前线性化
            dl->AddLine(pa, pb, srgb_u32_to_linear(overlay.color),
                        kMeasureLineWidth * ui_scale);

            // Distance label at midpoint.
            if (!overlay.label.empty()) {
                const ImVec2 pmid{
                    (pa.x + pb.x) * 0.5f,
                    (pa.y + pb.y) * 0.5f};
                const ImVec2 ts = ImGui::CalcTextSize(
                    overlay.label.c_str());
                dl->AddRectFilled(
                    ImVec2(pmid.x - ts.x * 0.5f - kMeasureLabelPad * ui_scale,
                           pmid.y - ts.y * 0.5f - kMeasureLabelPad * ui_scale),
                    ImVec2(pmid.x + ts.x * 0.5f + kMeasureLabelPad * ui_scale,
                           pmid.y + ts.y * 0.5f + kMeasureLabelPad * ui_scale),
                    kMeasureLabelBg, 3.0f * ui_scale);
                // Label color follows the line color for readability.
                const ImU32 kLabelColor = srgb_u32_to_linear(overlay.color)
                                          & 0x00FFFFFF | (240u << 24);
                dl->AddText(
                    ImVec2(pmid.x - ts.x * 0.5f,
                           pmid.y - ts.y * 0.5f),
                    kLabelColor,
                    overlay.label.c_str());
            }
        }
        dl->PopClipRect();
    }

    // ── 待定测量点标记 + 预览线 ──
    if (view.pending_point_visible &&
        view.pending_point_screen_x >= 0.0f &&
        view.pending_point_screen_y >= 0.0f &&
        view.image_width > 0 && view.image_height > 0) {
        const auto scr = framebuffer_to_plot_screen(
            view.pending_point_screen_x, view.pending_point_screen_y,
            canvas_rect, view.show_map_axis,
            view.image_width, view.image_height,
            ui_scale);
        const float px = plot_min.x + scr.x;
        const float py = plot_min.y + scr.y;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(plot_min, plot_max, true);

        // Filled circle + outer ring in measurement-yellow.
        const float kMarkerR = 7.0f * ui_scale;
        const ImU32 kMarkerFill = to_u32(palette::kYellow, 200);
        const ImU32 kMarkerRing = to_u32(palette::kYellow, 255);
        dl->AddCircleFilled({px, py}, kMarkerR, kMarkerFill);
        dl->AddCircle({px, py}, kMarkerR + 2.0f * ui_scale, kMarkerRing, 0, 2.5f * ui_scale);

        // Preview line to mouse cursor while the cursor is on the plot area.
        const ImGuiIO& io = ImGui::GetIO();
        const float mx = io.MousePos.x;
        const float my = io.MousePos.y;
        if (mx >= plot_min.x && mx < plot_max.x &&
            my >= plot_min.y && my < plot_max.y) {
            const ImU32 kPreviewLine = to_u32(palette::kYellow, 100);
            dl->AddLine({px, py}, {mx, my}, kPreviewLine, 1.5f * ui_scale);
        }

        dl->PopClipRect();
    }

    const ImGuiIO& io = ImGui::GetIO();
    gs3d::app::ViewportFrameCmd frame;
    frame.index = view.viewport_index;
    frame.workspace_id = workspace_id;
    frame.width = static_cast<std::uint32_t>(available.x);
    frame.height = static_cast<std::uint32_t>(available.y);
    // Use camera viewport (= GPU framebuffer) dimensions, not ImGui
    // content region, so the mouse→fb mapping stays pixel-accurate.
    const auto mouse_mapping = map_screen_mouse_to_framebuffer(
        io.MousePos.x, io.MousePos.y,
        canvas_rect, view.show_map_axis,
        view.image_width, view.image_height,
        ui_scale);

    frame.hovered = hovered && mouse_mapping.mouse_on_image;
    frame.active =
        active &&
        (mouse_mapping.mouse_on_image || view.box_select_dragging);
    frame.mouse_delta_x = active ? io.MouseDelta.x : 0.0f;
    frame.mouse_delta_y = active ? io.MouseDelta.y : 0.0f;
    frame.mouse_wheel = frame.hovered ? io.MouseWheel : 0.0f;
    frame.mouse_local_x = mouse_mapping.framebuffer_x;
    frame.mouse_local_y = mouse_mapping.framebuffer_y;
    frame.mouse_on_image = mouse_mapping.mouse_on_image;

    if (frame.hovered || frame.active) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }

    // Tooltip: shown whenever we have valid hover data and the cursor is on the image.
    if (frame.mouse_on_image && view.hover_tooltip_visible) {
        ImGui::SetTooltip(
            "x: %.6f\ny: %.6f\n%s: %.6f\n%s: %.6f",
            static_cast<double>(view.hover_x),
            static_cast<double>(view.hover_y),
            view.hover_primary_value_label.c_str(),
            static_cast<double>(view.hover_fold),
            view.hover_z_label.c_str(),
            static_cast<double>(view.hover_elevation)
        );
    }

    // ── C key: copy hovered point values to clipboard ──
    if (view.copy_feedback_frames > 0) {
        --view.copy_feedback_frames;
    }
    if (frame.mouse_on_image && view.hover_tooltip_visible &&
        !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
        char clip_buf[256];
        std::snprintf(clip_buf, sizeof(clip_buf),
            "%.6f,%.6f,%.6f,%.6f",
            static_cast<double>(view.hover_x),
            static_cast<double>(view.hover_y),
            static_cast<double>(view.hover_elevation),
            static_cast<double>(view.hover_fold));
        ImGui::SetClipboardText(clip_buf);
        view.copy_feedback_frames = 90; // ~1.5 s at 60 fps
    }

    // Ctrl+左键 = 框选放大，普通左键 = 轨道旋转；两者互斥，框选时不旋转。
    // 测量模式下 Shift+左键 = 框选统计，同样不旋转。
    const bool box_select_button_down =
        active &&
        frame.mouse_on_image &&
        io.KeyCtrl &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left);

    const bool stats_select_button_down =
        active &&
        frame.mouse_on_image &&
        view.measure_mode_active &&
        io.KeyShift &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left);

    frame.rotate =
        active &&
        frame.mouse_on_image &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
        !io.KeyCtrl &&
        !io.KeyShift;
    frame.pan =
        active &&
        frame.mouse_on_image &&
        ImGui::IsMouseDown(ImGuiMouseButton_Right);
    frame.point_double_clicked =
        active &&
        frame.mouse_on_image &&
        !io.KeyCtrl &&
        !io.KeyShift &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    frame.measure_pick_requested =
        active &&
        frame.mouse_on_image &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Middle);

    if (box_select_button_down && !view.box_select_dragging) {
        view.box_select_dragging = true;
        view.box_select_start_x = frame.mouse_local_x;
        view.box_select_start_y = frame.mouse_local_y;
    }

    if (view.box_select_dragging) {
        // Both corners through framebuffer_to_plot_screen — the same
        // inverse transform the crosshair uses.  Keeps the drawn rect
        // and the box→world unprojection in the same coordinate space.
        const auto scr_start = framebuffer_to_plot_screen(
            view.box_select_start_x, view.box_select_start_y,
            canvas_rect, view.show_map_axis,
            view.image_width, view.image_height,
            ui_scale);
        const auto scr_curr = framebuffer_to_plot_screen(
            frame.mouse_local_x, frame.mouse_local_y,
            canvas_rect, view.show_map_axis,
            view.image_width, view.image_height,
            ui_scale);
        const ImVec2 rect_a(plot_min.x + scr_start.x,
                            plot_min.y + scr_start.y);
        const ImVec2 rect_b(plot_min.x + scr_curr.x,
                            plot_min.y + scr_curr.y);
        ImGui::GetWindowDrawList()->AddRect(
            rect_a, rect_b, to_u32(palette::kAccent, 255),
            0.0f, 0, 1.5f * ui_scale
        );
        ImGui::GetWindowDrawList()->AddRectFilled(
            rect_a, rect_b, to_u32(palette::kAccent, 40)
        );

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            view.box_select_dragging = false;

            const float min_x = std::min(view.box_select_start_x, frame.mouse_local_x);
            const float min_y = std::min(view.box_select_start_y, frame.mouse_local_y);
            const float max_x = std::max(view.box_select_start_x, frame.mouse_local_x);
            const float max_y = std::max(view.box_select_start_y, frame.mouse_local_y);

            // Ignore accidental clicks/tiny drags (< 4px on either axis).
            if (frame.mouse_on_image &&
                max_x - min_x >= 4.0f &&
                max_y - min_y >= 4.0f) {
                frame.box_select_completed = true;
                frame.box_select_min_x = min_x;
                frame.box_select_min_y = min_y;
                frame.box_select_max_x = max_x;
                frame.box_select_max_y = max_y;
            }
        }
    }

    // ── Shift+左键框选统计（测量模式下）──
    if (stats_select_button_down && !view.stats_select_dragging) {
        view.stats_select_dragging = true;
        view.stats_select_start_x = frame.mouse_local_x;
        view.stats_select_start_y = frame.mouse_local_y;
    }

    if (view.stats_select_dragging) {
        const auto scr_start = framebuffer_to_plot_screen(
            view.stats_select_start_x, view.stats_select_start_y,
            canvas_rect, view.show_map_axis,
            view.image_width, view.image_height,
            ui_scale);
        const auto scr_curr = framebuffer_to_plot_screen(
            frame.mouse_local_x, frame.mouse_local_y,
            canvas_rect, view.show_map_axis,
            view.image_width, view.image_height,
            ui_scale);
        const ImVec2 rect_a(plot_min.x + scr_start.x,
                            plot_min.y + scr_start.y);
        const ImVec2 rect_b(plot_min.x + scr_curr.x,
                            plot_min.y + scr_curr.y);
        ImGui::GetWindowDrawList()->AddRect(
            rect_a, rect_b, to_u32(palette::kTeal, 255),
            0.0f, 0, 1.5f * ui_scale
        );
        ImGui::GetWindowDrawList()->AddRectFilled(
            rect_a, rect_b, to_u32(palette::kTeal, 40)
        );

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            view.stats_select_dragging = false;

            const float min_x = std::min(view.stats_select_start_x, frame.mouse_local_x);
            const float min_y = std::min(view.stats_select_start_y, frame.mouse_local_y);
            const float max_x = std::max(view.stats_select_start_x, frame.mouse_local_x);
            const float max_y = std::max(view.stats_select_start_y, frame.mouse_local_y);

            if (frame.mouse_on_image &&
                max_x - min_x >= 4.0f &&
                max_y - min_y >= 4.0f) {
                frame.stats_select_completed = true;
                frame.stats_select_min_x = min_x;
                frame.stats_select_min_y = min_y;
                frame.stats_select_max_x = max_x;
                frame.stats_select_max_y = max_y;
            }
        }
    }

    actions.viewport_frames.push_back(frame);
    view.render_requested = view.visible;

    // ponytail: store canvas rect for screenshot coordinate mapping.
    // GetItemRectMin/Max are in ImGui screen-space (absolute) coordinates.
    view.canvas_rect_min_x = canvas_rect.min_x;
    view.canvas_rect_min_y = canvas_rect.min_y;
    view.canvas_rect_max_x = canvas_rect.max_x;
    view.canvas_rect_max_y = canvas_rect.max_y;

    ImGui::End();
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
            draw_viewport_window(view, actions, workspace.id);
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
    const ImVec2 work_size = ImGui::GetMainViewport()->WorkSize;

    // Re-apply the ratio-based default layout on a large relative work-size
    // change (maximize/restore), even if the visible-view signature is
    // unchanged. Threshold: relative change in either dimension exceeds 25%
    // vs the size at which we last built the layout. Small resizes leave
    // any user-dragged dock splitters untouched.
    const bool size_changed_significantly =
        last_layout_work_w_ > 0.0f && last_layout_work_h_ > 0.0f &&
        (std::abs(work_size.x - last_layout_work_w_) >
             0.25f * last_layout_work_w_ ||
         std::abs(work_size.y - last_layout_work_h_) >
             0.25f * last_layout_work_h_);

    if (dock_layout_initialized_ &&
        dock_layout_signature_ == signature &&
        !size_changed_significantly) {
        return;
    }

    const ImGuiID dockspace_id =
        ImGui::GetID("GeoScatter3D.DockSpace");
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
    // Side-bar widths: ratio of work width, clamped to [min,max] px.
    // Ratio derived from the prior hand-tuned baseline (220px / 240px at a
    // ~1280px work width → ~0.17 / ~0.19). The clamp keeps tiny windows
    // readable and huge windows from letting the bars swallow the center.
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

    const bool has_left_panels =
        state.panels.dataset ||
        state.panels.tile_inspector ||
        state.panels.lod_view ||
        state.panels.navigation_map ||
        state.panels.measurement ||
        state.panels.region_stats;
    const bool has_right_panels =
        state.panels.render_settings ||
        state.panels.performance ||
        state.panels.debug_log;
    // The right split happens after the optional left split, so its ratio
    // must be relative to the remaining center node rather than the original
    // work width. Using work_width here made a requested 260px panel land at
    // roughly 225px on a 1280px window.
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
            0.33f,               // 导航图占左侧 33%
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
        if (state.panels.debug_log) {
            ImGui::DockBuilderDockWindow(kDebugLogWindowName, right_id);
        }
    }

    ImGuiID view_area_id = center_id;
    if (state.panels.tools) {
        const float tools_ratio = std::clamp(
            LayoutMetrics::kToolsBarHeightBase /
                std::max(1.0f, work_size.y),
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
        if (ImGuiDockNode* tools_node =
                ImGui::DockBuilderGetNode(tools_id)) {
            tools_node->LocalFlags |=
                ImGuiDockNodeFlags_NoTabBar |
                ImGuiDockNodeFlags_NoWindowMenuButton;
        }
        ImGui::DockBuilderDockWindow(kToolsWindowName, tools_id);
    }

    for (const auto& view : state.render_views) {
        if (view.visible &&
            !view.detached &&
            !view_is_owned_by_workspace(state, view.viewport_index)) {
            ImGui::DockBuilderDockWindow(
                render_view_window_name(view.viewport_index).c_str(),
                view_area_id
            );
        }
    }

    ImGui::DockBuilderFinish(dockspace_id);
    dock_layout_initialized_ = true;
    dock_layout_signature_ = signature;
    last_layout_work_w_ = work_size.x;
    last_layout_work_h_ = work_size.y;
}

gs3d::app::UiActions UiRoot::draw(gs3d::app::AppState& state)
{
    gs3d::app::UiActions actions;
    // UI scale for high-DPI chrome (toolbar / status bar heights). Same
    // definition as the per-viewport scale in draw_render_view: font size
    // relative to the 13px baseline. Does NOT feed the render-size chain.
    const float ui_scale = ImGui::GetFontSize() / 13.0f;
    prune_workspace_windows(state);
    if (ImGui::GetIO().KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_N, false)) {
        show_first_hidden_view(state);
    }
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

    constexpr bool render_workspace = true;
    if (ImGui::Begin(kHostWindowName, nullptr, host_flags)) {
        push_application_menu_style(ui_scale);
        ImFont* menu_font = gs3d::gui::ui_fonts().medium;
        if (menu_font != nullptr) {
            ImGui::PushFont(menu_font);
        }
        const bool menu_bar_visible = ImGui::BeginMenuBar();
        if (menu_bar_visible) {
            if (ImGui::BeginMenu("文件")) {
                draw_menu_section_label("文件操作");
                if (ImGui::MenuItem("打开数据")) {
                    actions.open_requested = true;
                }
                if (ImGui::MenuItem("截图")) {
                    actions.screenshot_requested = true;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("视图")) {
                draw_menu_section_label("工作区");
                const bool has_hidden = has_hidden_view(state);
                if (ImGui::MenuItem(
                        "新建视图",
                        "Ctrl+N",
                        false,
                        has_hidden
                )) {
                    show_first_hidden_view(state);
                }
                if (ImGui::MenuItem("恢复默认工作区")) {
                    for (auto& view : state.render_views) {
                        view.detached = false;
                        view.force_undock_next_frame = false;
                    }
                    state.workspace_windows.clear();
                    dock_layout_initialized_ = false;
                }
                draw_menu_section_label("外观");
                if (ImGui::BeginMenu("主题")) {
                    for (int i = 0; i < kThemeCount; ++i) {
                        const auto id = static_cast<ThemeId>(i);
                        const bool selected = active_theme() == id;
                        if (ImGui::MenuItem(
                                theme_tokens(id).name,
                                nullptr,
                                selected
                            ) &&
                            !selected) {
                            // 立即重写 ImGuiStyle 与 palette::，本帧剩余
                            // 控件即以新主题绘制，无需等下一帧。圆角基准
                            // 用字体系统的权威 ui_scale，与 init 时一致。
                            apply_theme(
                                id,
                                gs3d::gui::ui_fonts().ui_scale
                            );
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("窗口")) {
                draw_menu_section_label("工作窗口");
                const bool can_create_workspace = has_hidden_view(state);
                if (ImGui::MenuItem(
                        "新建工作窗口",
                        nullptr,
                        false,
                        can_create_workspace
                )) {
                    create_workspace_window(state);
                }
                draw_menu_section_label("面板显示");
                ImGui::MenuItem(
                    "工具",
                    nullptr,
                    &state.panels.tools
                );
                ImGui::MenuItem(
                    "项目",
                    nullptr,
                    &state.panels.dataset
                );
                ImGui::MenuItem(
                    "属性",
                    nullptr,
                    &state.panels.render_settings
                );
                ImGui::MenuItem(
                    "性能",
                    nullptr,
                    &state.panels.performance
                );
                ImGui::MenuItem(
                    "瓦片",
                    nullptr,
                    &state.panels.tile_inspector
                );
                ImGui::MenuItem(
                    "细节层级",
                    nullptr,
                    &state.panels.lod_view
                );
                ImGui::MenuItem(
                    "日志",
                    nullptr,
                    &state.panels.debug_log
                );
                ImGui::MenuItem(
                    "导航图",
                    nullptr,
                    &state.panels.navigation_map
                );
                ImGui::MenuItem(
                    "测量",
                    nullptr,
                    &state.panels.measurement
                );
                ImGui::MenuItem(
                    "区域统计",
                    nullptr,
                    &state.panels.region_stats
                );
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("帮助")) {
                draw_menu_section_label("帮助与引导");
                if (ImGui::MenuItem("欢迎页")) {
                    actions.show_welcome_requested = true;
                }
                draw_menu_section_label("使用提示");
                draw_menu_hint(
                    "视图可作为标签页使用，也可拖到其他显示器。"
                );
                draw_menu_hint(
                    "默认相机相互独立，可在视图工具条启用联动。"
                );
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
        if (menu_font != nullptr) {
            ImGui::PopFont();
        }
        if (menu_bar_visible) {
            const ImRect menu_rect =
                ImGui::GetCurrentWindow()->MenuBarRect();
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(menu_rect.Min.x, menu_rect.Max.y - 1.0f),
                ImVec2(menu_rect.Max.x, menu_rect.Max.y - 1.0f),
                to_u32(palette::kBorder, 110),
                1.0f
            );
        }
        pop_application_menu_style();

        {
            ImDrawList* host_dl = ImGui::GetWindowDrawList();
            const float content_avail_y =
                ImGui::GetContentRegionAvail().y;
            const float status_h = std::min(
                LayoutMetrics::kStatusBarHeightBase * ui_scale,
                std::max(0.0f, content_avail_y)
            );
            const float dock_h =
                std::max(0.0f, content_avail_y - status_h);
            build_default_layout(state);
            ImGui::DockSpace(
                ImGui::GetID("GeoScatter3D.DockSpace"),
                ImVec2(0.0f, dock_h),
                ImGuiDockNodeFlags_None
            );

            ImGui::BeginChild(
                "##StatusBar",
                ImVec2(0.0f, status_h),
                false,
                ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoScrollWithMouse
            );
            const ImVec2 status_min = ImGui::GetWindowPos();
            const ImVec2 status_max{
                status_min.x + ImGui::GetWindowSize().x,
                status_min.y + ImGui::GetWindowSize().y
            };
            host_dl->AddLine(
                ImVec2(status_min.x, status_min.y),
                ImVec2(status_max.x, status_min.y),
                to_u32(palette::kBorder, 56),
                1.0f
            );
            ImGui::SetCursorPosX(LayoutMetrics::kStatusInsetX);
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                to_u32(palette::kTextDim, 158)
            );
            if (status_font() != nullptr) {
                ImGui::PushFont(status_font());
            }
            ImGui::Text(
                "%.1f FPS    %.2f ms    %llu 点    GPU %.1f MB    %s",
                state.status_bar.fps,
                state.performance.frame_time_ms,
                static_cast<unsigned long long>(
                    state.status_bar.visible_points
                ),
                bytes_to_mb(state.status_bar.gpu_memory_bytes),
                state.status_bar.ready_state.c_str()
            );
            if (status_font() != nullptr) {
                ImGui::PopFont();
            }
            ImGui::PopStyleColor();
            ImGui::EndChild();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);

    if (render_workspace) {
        draw_tools_window(state, actions, ui_scale);
        draw_dataset_panel(state);
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
                draw_viewport_window(view, actions);
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

        draw_auxiliary_panels(state);
    } else {
        for (auto& view : state.render_views) {
            view.render_requested = false;
        }
    }
    if (!ImGui::GetIO().WantTextInput &&
        !ImGui::GetIO().KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_M, false)) {
        int target_viewport_index = state.active_viewport_index;
        for (const auto& frame : actions.viewport_frames) {
            if (frame.active || frame.hovered) {
                target_viewport_index = frame.index;
            }
        }
        auto& measurement =
            gs3d::app::measurement_for_view(state, target_viewport_index);
        measurement.toggle_measure_mode();
        if (!measurement.measure_mode_active()) {
            measurement.clear_pending();
        }
    }
    for (const auto& frame : actions.viewport_frames) {
        if (frame.active || frame.hovered) {
            state.active_viewport_index = frame.index;
        }
    }
    return actions;
}

} // namespace gs3d::ui
