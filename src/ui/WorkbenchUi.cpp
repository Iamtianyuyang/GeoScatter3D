#include "ui/WorkbenchUi.hpp"

#include "gui/UiFonts.hpp"
#include "ui/IconFont.hpp"
#include "ui/UiPalette.hpp"
#include "ui/Widgets.hpp"
#include "ui/WorkspaceManager.hpp"

#include "imgui.h"

namespace gs3d::ui {

namespace {
ImFont* icon_font() { return gs3d::gui::ui_fonts().icons; }

void toggle_map_axis(gs3d::app::RenderViewState& view)
{
    view.show_map_axis = !view.show_map_axis;
    if (view.show_map_axis) {
        view.show_world_axis = false;
    }
}

void toggle_world_axis(gs3d::app::RenderViewState& view)
{
    view.show_world_axis = !view.show_world_axis;
    if (view.show_world_axis) {
        view.show_map_axis = false;
        view.show_crosshair = false;
    }
}

void toggle_crosshair(gs3d::app::RenderViewState& view)
{
    view.show_crosshair = !view.show_crosshair;
    if (view.show_crosshair) {
        view.show_map_axis = true;
        view.show_world_axis = false;
    }
}

void draw_reticle_popup(gs3d::app::RenderViewState& view)
{
    if (!ImGui::BeginPopup("##WorkbenchReticleStyle")) {
        return;
    }
    ImGui::TextUnformatted("十字准线");
    ImVec4 crosshair = ImGui::ColorConvertU32ToFloat4(view.crosshair_color);
    if (ImGui::ColorEdit4(
            "颜色##WorkbenchCrosshair",
            &crosshair.x,
            ImGuiColorEditFlags_NoInputs
        )) {
        view.crosshair_color = ImGui::ColorConvertFloat4ToU32(crosshair);
    }
    ImGui::TextUnformatted("拾取准星");
    ImVec4 reticle = ImGui::ColorConvertU32ToFloat4(view.reticle_color);
    if (ImGui::ColorEdit4(
            "颜色##WorkbenchReticle",
            &reticle.x,
            ImGuiColorEditFlags_NoInputs
        )) {
        view.reticle_color = ImGui::ColorConvertFloat4ToU32(reticle);
    }
    ImGui::EndPopup();
}

} // namespace

void draw_workbench_view_controls(
    gs3d::app::AppState& state,
    gs3d::app::RenderViewState& view,
    gs3d::app::UiActions& actions,
    const float ui_scale
) {
    const float scale = ui_scale > 0.0f ? ui_scale : 1.0f;
    ImGui::PushID(view.viewport_index);
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(10.0f * scale, 5.0f * scale)
    );
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(7.0f * scale, 4.0f * scale)
    );
    ImGui::PushStyleColor(
        ImGuiCol_ChildBg,
        to_u32(palette::kSurface, 255)
    );
    ImGui::BeginChild(
        "##WorkbenchViewControls",
        ImVec2(0.0f, 38.0f * scale),
        false,
        ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse
    );
    if (widgets::Chip("复位视角  R")) {
        actions.reset_camera_index = view.viewport_index;
    }
    ImGui::SameLine();
    if (widgets::Chip("联动相机", view.camera_linked)) {
        view.camera_linked = !view.camera_linked;
    }
    ImGui::SameLine();
    if (widgets::Chip("地图轴", view.show_map_axis)) {
        toggle_map_axis(view);
    }
    ImGui::SameLine();
    if (widgets::Chip("世界轴", view.show_world_axis)) {
        toggle_world_axis(view);
    }
    ImGui::SameLine();
    if (widgets::Chip("十字准线", view.show_crosshair)) {
        toggle_crosshair(view);
    }
    ImGui::SameLine();
    if (widgets::Chip("准星样式 ▾")) {
        ImGui::OpenPopup("##WorkbenchReticleStyle");
    }
    draw_reticle_popup(view);

    ImGui::SameLine();
    bool can_add = gs3d::ui::has_hidden_view(state);
    ImGui::BeginDisabled(!can_add);
    char ibuf[5]; icons::utf8(icons::kAdd.codepoint, ibuf);
    if (icon_font()) ImGui::PushFont(icon_font());
    bool clicked = ImGui::SmallButton(ibuf);
    if (icon_font()) ImGui::PopFont();
    ImGui::EndDisabled();
    if (clicked && can_add) gs3d::ui::show_first_hidden_view(state);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("新建视图 (Ctrl+N)");

    const ImVec2 min = ImGui::GetWindowPos();
    const ImVec2 max{
        min.x + ImGui::GetWindowSize().x,
        min.y + ImGui::GetWindowSize().y
    };
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(min.x, max.y - 1.0f),
        ImVec2(max.x, max.y - 1.0f),
        to_u32(palette::kBorder, 110)
    );
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::PopID();
}

} // namespace gs3d::ui
