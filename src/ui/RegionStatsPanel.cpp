#include "ui/RegionStatsPanel.hpp"

#include "gui/UiFonts.hpp"
#include "imgui.h"

namespace gs3d::ui {

namespace {
constexpr const char* kRegionStatsWindowName = "区域统计###RegionStats";
}

void draw_region_stats_panel(gs3d::app::AppState& state)
{
    if (!ImGui::Begin(kRegionStatsWindowName, &state.panels.region_stats)) {
        ImGui::End();
        return;
    }

    const auto& stats = state.region_stats;

    if (!stats.valid) {
        ImGui::TextDisabled("在测量模式下 Shift+左键框选区域");
        ImGui::End();
        return;
    }

    ImGui::Text("框内点数  %llu",
        static_cast<unsigned long long>(stats.point_count));

    ImGui::Separator();

    // ── Fold (场值) ──
    if (auto* font = gs3d::gui::ui_fonts().panel_title) {
        ImGui::PushFont(font);
    }
    ImGui::TextUnformatted("Fold (场值)");
    if (auto* font = gs3d::gui::ui_fonts().panel_title) {
        ImGui::PopFont();
    }

    ImGui::Text("最小值  %.3f", static_cast<double>(stats.fold_min));
    ImGui::Text("最大值  %.3f", static_cast<double>(stats.fold_max));
    ImGui::Text("平均值  %.3f", static_cast<double>(stats.fold_avg));

    ImGui::Spacing();

    // ── Elevation (高程) ──
    if (auto* font = gs3d::gui::ui_fonts().panel_title) {
        ImGui::PushFont(font);
    }
    ImGui::TextUnformatted("Elevation (高程)");
    if (auto* font = gs3d::gui::ui_fonts().panel_title) {
        ImGui::PopFont();
    }

    ImGui::Text("最小值  %.3f", static_cast<double>(stats.elev_min));
    ImGui::Text("最大值  %.3f", static_cast<double>(stats.elev_max));
    ImGui::Text("平均值  %.3f", static_cast<double>(stats.elev_avg));

    ImGui::End();
}

} // namespace gs3d::ui
