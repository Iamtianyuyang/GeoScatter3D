#include "ui/FloatingDockUi.hpp"
#include "ui/LayoutRegistry.hpp"
#include "ui/UiPalette.hpp"
#include "ui/Widgets.hpp"
#include "ui/UiFonts.hpp"
#include "imgui.h"

#include <algorithm>
#include <string>

namespace gs3d::ui {

namespace {

bool dock_capsule_button(const char* label, bool active, float ui_scale) {
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, palette::kAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, palette::kAccentActive);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, to_u32(palette::kSurfaceHover, 180));
        ImGui::PushStyleColor(ImGuiCol_Text, palette::kText);
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 14.0f * ui_scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f * ui_scale, 4.0f * ui_scale));

    const bool clicked = ImGui::Button(label);

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);

    return clicked;
}

} // namespace

void draw_floating_dock_overlay(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    float ui_scale
) {
    const auto active_layout = LayoutRegistry::instance().active_layout_id();
    if (active_layout != "floating-dock" &&
        state.ui_layout_mode != gs3d::app::UiLayoutMode::kFloatingDock) {
        return;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    if (vp == nullptr) {
        return;
    }

    const ImGuiWindowFlags overlay_flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoFocusOnAppearing;

    // 1. 左上方浮动状态 Chip
    const float chip_x = vp->Pos.x + 18.0f * ui_scale;
    const float chip_y = vp->Pos.y + 40.0f * ui_scale;
    ImGui::SetNextWindowPos(ImVec2(chip_x, chip_y), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0f * ui_scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * ui_scale, 5.0f * ui_scale));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(palette::kSurface.x, palette::kSurface.y, palette::kSurface.z, 0.88f));
    ImGui::PushStyleColor(ImGuiCol_Border, to_u32(palette::kBorder, 140));

    if (ImGui::Begin("###FloatingStatusChip", nullptr, overlay_flags)) {
        ImGui::TextColored(palette::kTextDim, "%s",
            state.dataset.active_dataset.empty() ? "未加载点云" : state.dataset.active_dataset.c_str());
        ImGui::SameLine(0.0f, 10.0f * ui_scale);
        ImGui::TextColored(palette::kAccent, "%.0f FPS", state.status_bar.fps);
        if (state.status_bar.visible_points > 0) {
            ImGui::SameLine(0.0f, 10.0f * ui_scale);
            ImGui::TextColored(palette::kTextFaint, "%llu 点",
                static_cast<unsigned long long>(state.status_bar.visible_points));
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    // 2. 底部居中悬浮胶囊 Dock 栏
    const float dock_margin_bottom = 22.0f * ui_scale;
    ImGui::SetNextWindowPos(
        ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y - dock_margin_bottom),
        ImGuiCond_Always,
        ImVec2(0.5f, 1.0f)
    );

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 22.0f * ui_scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f * ui_scale, 6.0f * ui_scale));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(palette::kSurface.x, palette::kSurface.y, palette::kSurface.z, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_Border, to_u32(palette::kBorder, 180));

    if (ImGui::Begin("###FloatingDockBar", nullptr, overlay_flags)) {
        if (dock_capsule_button("项目", state.panels.dataset, ui_scale)) {
            state.panels.dataset = !state.panels.dataset;
        }
        ImGui::SameLine(0.0f, 6.0f * ui_scale);

        if (dock_capsule_button("属性", state.panels.render_settings, ui_scale)) {
            state.panels.render_settings = !state.panels.render_settings;
        }
        ImGui::SameLine(0.0f, 6.0f * ui_scale);

        auto& meas = gs3d::app::measurement_for_view(state, state.active_viewport_index);
        if (dock_capsule_button("测量", meas.measure_mode_active() || state.panels.measurement, ui_scale)) {
            state.panels.measurement = !state.panels.measurement;
        }
        ImGui::SameLine(0.0f, 6.0f * ui_scale);

        if (dock_capsule_button("导航", state.panels.navigation_map, ui_scale)) {
            state.panels.navigation_map = !state.panels.navigation_map;
        }
        ImGui::SameLine(0.0f, 6.0f * ui_scale);

        if (dock_capsule_button("统计", state.panels.region_stats, ui_scale)) {
            state.panels.region_stats = !state.panels.region_stats;
        }
        ImGui::SameLine(0.0f, 8.0f * ui_scale);

        ImGui::TextColored(palette::kBorder, "|");
        ImGui::SameLine(0.0f, 8.0f * ui_scale);

        if (dock_capsule_button("截图", false, ui_scale)) {
            actions.screenshot_requested = true;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

} // namespace gs3d::ui
