#include "ui/UiRoot.hpp"

#include "ui/AppChrome.hpp"
#include "ui/UiOverlays.hpp"
#include "ui/ViewportCanvas.hpp"
#include "ui/layouts/standard_workbench/StandardWorkbenchLayout.hpp"
#include "ui/layouts/standard_workbench/WorkbenchViewControls.hpp"
#include "ui/WorkspaceManager.hpp"
#include "ui/layouts/LayoutRegistry.hpp"
#include "ui/color/Theme.hpp"
#include "ui/color/UiPalette.hpp"
#include "ui/layouts/floating_dock/FloatingDockLayout.hpp"
#include "ui/layouts/analysis_rail/AnalysisRailLayout.hpp"
#include "ui/UiFonts.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <string>
#include <vector>

namespace gs3d::ui {

std::string render_view_window_name(int index)
{
    return "视图 " + std::to_string(index + 1) +
        "###RenderView" + std::to_string(index);
}

void draw_viewport_window(
    gs3d::app::AppState& state,
    gs3d::app::RenderViewState& view,
    gs3d::app::UiActions& actions,
    int workspace_id,
    bool show_workbench_controls
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

gs3d::app::UiActions UiRoot::draw(gs3d::app::AppState& state)
{
    gs3d::app::UiActions actions;
    const float ui_scale = ImGui::GetFontSize() / 13.0f;
    prune_workspace_windows(state);
    begin_viewport_frame_shortcuts(state, actions);

    // 全量预加载门禁：数据就绪前只显示加载页（加载好了再进程序）。
    if (draw_preload_gate_if_active(state, ui_scale)) {
        return actions;
    }

    // 3 套独立布局模式分发（方案 A：工作台，方案 B：悬浮 Dock，方案 C：暗色分析舱）
    if (state.ui_layout_mode == gs3d::app::UiLayoutMode::kFloatingDock) {
        const FloatingDockFrameResult dock_result =
            draw_floating_dock_layout(state, actions, ui_scale);
        if (dock_result.theme_change_requested) {
            apply_theme(dock_result.requested_theme, gs3d::gui::ui_fonts().ui_scale);
            persist_ui_preferences(state, dock_result.requested_theme);
        }
        for (auto& view : state.render_views) {
            if (view.visible &&
                (view.detached || view.force_undock_next_frame)) {
                draw_viewport_window(state, view, actions, 0, true);
            }
        }
        draw_screenshot_notice(state, ui_scale);
    } else if (state.ui_layout_mode == gs3d::app::UiLayoutMode::kAnalysisRail) {
        const auto rail_result = draw_analysis_rail_layout(
            state, actions, gs3d::gui::ui_fonts().ui_scale);
        if (rail_result.theme_change_requested) {
            apply_theme(rail_result.requested_theme, gs3d::gui::ui_fonts().ui_scale);
            persist_ui_preferences(state, rail_result.requested_theme);
        }
        for (auto& view : state.render_views) {
            if (view.visible &&
                (view.detached || view.force_undock_next_frame)) {
                draw_viewport_window(state, view, actions, 0, true);
            }
        }
        draw_screenshot_notice(state, ui_scale);
    } else {
        const auto wb_result =
            draw_standard_workbench_layout(state, actions, ui_scale);
        if (wb_result.theme_change_requested) {
            apply_theme(wb_result.requested_theme, gs3d::gui::ui_fonts().ui_scale);
            persist_ui_preferences(state, wb_result.requested_theme);
        }
        if (wb_result.layout_change_requested) {
            LayoutRegistry::instance().set_active_layout_id(wb_result.requested_layout_id);
            state.ui_layout_mode = gs3d::app::ui_layout_from_string(wb_result.requested_layout_id);
            restore_default_workspace(state);
            reset_standard_workbench_layout();
            persist_ui_preferences(state, active_theme());
        }
    }

    finalize_viewport_frame_shortcuts(state, actions);
    return actions;
}

} // namespace gs3d::ui
