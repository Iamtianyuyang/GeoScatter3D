#include "ui/WorkbenchUi.hpp"

#include "ui/UiFonts.hpp"
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

bool toolbar_button(
    const char* id,
    const icons::Glyph* glyph,
    const char* text,
    bool active,
    float scale,
    const char* tooltip = nullptr
) {
    char icon_buf[5] = {0};
    if (glyph) {
        (void)icons::utf8(glyph->codepoint, icon_buf);
    }

    ImFont* ifont = icon_font();
    const float icon_w = (glyph && ifont) ? ifont->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0, icon_buf).x : 0.0f;
    const ImVec2 text_sz = text && text[0] != '\0' ? ImGui::CalcTextSize(text, nullptr, true) : ImVec2(0.0f, 0.0f);
    const float gap = (glyph && text && text[0] != '\0') ? 5.0f * scale : 0.0f;
    const float pad_x = (text && text[0] != '\0') ? 10.0f * scale : 8.0f * scale;
    const ImVec2 sz{
        icon_w + gap + text_sz.x + pad_x * 2.0f,
        26.0f * scale
    };

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, sz);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 max{pos.x + sz.x, pos.y + sz.y};
    const float r = 4.0f * scale;

    if (active) {
        ImVec4 bg = palette::kAccent;
        bg.w = held ? 0.30f : (hovered ? 0.22f : 0.15f);
        dl->AddRectFilled(pos, max, ImGui::ColorConvertFloat4ToU32(bg), r);
        dl->AddRect(pos, max, to_u32(palette::kAccent, 200), r, 0, 1.0f * scale);
    } else {
        if (held) {
            dl->AddRectFilled(pos, max, to_u32(palette::kFrameHover, 160), r);
            dl->AddRect(pos, max, to_u32(palette::kBorder, 120), r, 0, 1.0f * scale);
        } else if (hovered) {
            dl->AddRectFilled(pos, max, to_u32(palette::kSurfaceHover, 140), r);
            dl->AddRect(pos, max, to_u32(palette::kBorder, 90), r, 0, 1.0f * scale);
        } else {
            dl->AddRect(pos, max, to_u32(palette::kBorder, 35), r, 0, 1.0f * scale);
        }
    }

    const ImU32 text_col = active
        ? to_u32(palette::kAccent, 255)
        : to_u32(hovered ? palette::kText : palette::kTextDim, 255);

    float draw_x = pos.x + pad_x;
    const float draw_y = pos.y + (sz.y - text_sz.y) * 0.5f;

    if (glyph && ifont) {
        ImGui::PushFont(ifont);
        dl->AddText({draw_x, pos.y + (sz.y - ImGui::GetFontSize()) * 0.5f}, text_col, icon_buf);
        ImGui::PopFont();
        draw_x += icon_w + gap;
    }
    if (text && text[0] != '\0') {
        dl->AddText({draw_x, draw_y}, text_col, text);
    }

    if (tooltip && hovered) {
        ImGui::SetTooltip("%s", tooltip);
    }
    return pressed;
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
        ImVec2(10.0f * scale, 4.0f * scale)
    );
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(6.0f * scale, 4.0f * scale)
    );
    ImGui::PushStyleColor(
        ImGuiCol_ChildBg,
        to_u32(palette::kRaised, 240)
    );
    ImGui::BeginChild(
        "##WorkbenchViewControls",
        ImVec2(0.0f, 34.0f * scale),
        false,
        ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse
    );
    if (toolbar_button("##VCR", &icons::kRefresh, "复位视角 R", false, scale, "复位相机视角到全景 (快捷键 R)")) {
        actions.reset_camera_index = view.viewport_index;
    }
    ImGui::SameLine();
    if (toolbar_button("##VCL", &icons::kLink, "联动相机", view.camera_linked, scale, "与其他视口同步相机平移与旋转")) {
        view.camera_linked = !view.camera_linked;
    }
    ImGui::SameLine();
    if (toolbar_button("##VCM", &icons::kExplore, "地图轴", view.show_map_axis, scale, "显示二维地理投影坐标轴与刻度")) {
        toggle_map_axis(view);
    }
    ImGui::SameLine();
    if (toolbar_button("##VCW", &icons::kPublic, "世界轴", view.show_world_axis, scale, "显示三维空间世界坐标轴")) {
        toggle_world_axis(view);
    }
    ImGui::SameLine();
    if (toolbar_button("##VCX", &icons::kSelectAll, "十字准线", view.show_crosshair, scale, "显示视口中心十字准线")) {
        toggle_crosshair(view);
    }
    ImGui::SameLine();
    if (toolbar_button("##VCS", &icons::kTune, "准星样式 ▾", false, scale, "配置十字准线与拾取光标颜色")) {
        ImGui::OpenPopup("##WorkbenchReticleStyle");
    }
    draw_reticle_popup(view);

    ImGui::SameLine();
    bool can_add = gs3d::ui::has_hidden_view(state);
    ImGui::BeginDisabled(!can_add);
    if (toolbar_button("##VCA", &icons::kAdd, "新建视图", false, scale, "新建多视口分屏 (Ctrl+N)")) {
        if (can_add) gs3d::ui::show_first_hidden_view(state);
    }
    ImGui::EndDisabled();

    const ImVec2 min = ImGui::GetWindowPos();
    const ImVec2 max{
        min.x + ImGui::GetWindowSize().x,
        min.y + ImGui::GetWindowSize().y
    };
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(min.x, max.y - 1.0f),
        ImVec2(max.x, max.y - 1.0f),
        to_u32(palette::kBorder, 60)
    );
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::PopID();
}

} // namespace gs3d::ui
