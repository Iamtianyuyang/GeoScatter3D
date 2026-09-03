#include "ui/AnalysisRailUi.hpp"
#include "ui/LayoutRegistry.hpp"
#include "ui/UiPalette.hpp"
#include "ui/Widgets.hpp"
#include "ui/UiFonts.hpp"
#include "imgui.h"

namespace gs3d::ui {

namespace {

enum class RailDrawer : int {
    kNone = -1,
    kDataset = 0,
    kRenderSettings = 1,
    kMeasurement = 2,
    kNavigation = 3,
    kRegionStats = 4,
};

static RailDrawer s_active_drawer = RailDrawer::kNone;

bool rail_icon_button(const char* label, bool active, float size) {
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, palette::kAccent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, palette::kAccentActive);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, to_u32(palette::kSurfaceHover, 180));
        ImGui::PushStyleColor(ImGuiCol_Text, palette::kText);
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    const bool clicked = ImGui::Button(label, ImVec2(size, size));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    return clicked;
}

} // namespace

void draw_analysis_rail_overlay(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    float ui_scale
) {
    const auto active_layout = LayoutRegistry::instance().active_layout_id();
    if (active_layout != "analysis-rail" &&
        state.ui_layout_mode != gs3d::app::UiLayoutMode::kAnalysisRail) {
        return;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    if (vp == nullptr) {
        return;
    }

    const float topbar_h = 32.0f * ui_scale;
    const float status_h = 26.0f * ui_scale;
    const float rail_w = 48.0f * ui_scale;
    const float rail_h = vp->Size.y - topbar_h - status_h;

    // 1. 左侧垂直导轨 (Vertical Rail)
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + topbar_h), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(rail_w, rail_h), ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f * ui_scale, 10.0f * ui_scale));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(palette::kSurface.x, palette::kSurface.y, palette::kSurface.z, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Border, to_u32(palette::kBorder, 140));

    const ImGuiWindowFlags rail_flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoFocusOnAppearing;

    if (ImGui::Begin("###AnalysisRailBar", nullptr, rail_flags)) {
        const float btn_size = 36.0f * ui_scale;

        if (rail_icon_button("项", s_active_drawer == RailDrawer::kDataset, btn_size)) {
            s_active_drawer = (s_active_drawer == RailDrawer::kDataset) ? RailDrawer::kNone : RailDrawer::kDataset;
            state.panels.dataset = (s_active_drawer == RailDrawer::kDataset);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("项目数据集");

        ImGui::Spacing();
        if (rail_icon_button("属", s_active_drawer == RailDrawer::kRenderSettings, btn_size)) {
            s_active_drawer = (s_active_drawer == RailDrawer::kRenderSettings) ? RailDrawer::kNone : RailDrawer::kRenderSettings;
            state.panels.render_settings = (s_active_drawer == RailDrawer::kRenderSettings);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("渲染与着色属性");

        ImGui::Spacing();
        if (rail_icon_button("量", s_active_drawer == RailDrawer::kMeasurement, btn_size)) {
            s_active_drawer = (s_active_drawer == RailDrawer::kMeasurement) ? RailDrawer::kNone : RailDrawer::kMeasurement;
            state.panels.measurement = (s_active_drawer == RailDrawer::kMeasurement);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("三维空间测量");

        ImGui::Spacing();
        if (rail_icon_button("图", s_active_drawer == RailDrawer::kNavigation, btn_size)) {
            s_active_drawer = (s_active_drawer == RailDrawer::kNavigation) ? RailDrawer::kNone : RailDrawer::kNavigation;
            state.panels.navigation_map = (s_active_drawer == RailDrawer::kNavigation);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("导航小地图");

        ImGui::Spacing();
        if (rail_icon_button("统", s_active_drawer == RailDrawer::kRegionStats, btn_size)) {
            s_active_drawer = (s_active_drawer == RailDrawer::kRegionStats) ? RailDrawer::kNone : RailDrawer::kRegionStats;
            state.panels.region_stats = (s_active_drawer == RailDrawer::kRegionStats);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("区域指标统计");
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

} // namespace gs3d::ui
