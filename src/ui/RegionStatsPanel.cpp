#include "ui/RegionStatsPanel.hpp"

#include "gui/UiFonts.hpp"
#include "imgui.h"

#include <sstream>
#include <string>

namespace gs3d::ui {

namespace {
constexpr const char* kRegionStatsWindowName = "区域统计###RegionStats";

std::string format_clipboard_text(const gs3d::app::RegionStatsResult& stats)
{
    std::ostringstream oss;
    oss.precision(6);
    oss << std::fixed;

    oss << "X范围: [" << stats.world_x_min << ", " << stats.world_x_max << "]\n";
    oss << "Y范围: [" << stats.world_y_min << ", " << stats.world_y_max << "]\n";
    oss << "框内点数: " << stats.point_count << '\n';
    oss << "Fold 最小值/最大值/平均值: "
        << stats.fold_min << " / " << stats.fold_max << " / " << stats.fold_avg << '\n';
    oss << "Elevation 最小值/最大值/平均值: "
        << stats.elev_min << " / " << stats.elev_max << " / " << stats.elev_avg;

    return oss.str();
}
} // namespace

void draw_region_stats_panel(gs3d::app::AppState& state)
{
    if (!ImGui::Begin(kRegionStatsWindowName, &state.panels.region_stats)) {
        ImGui::End();
        return;
    }

    const auto& stats = state.region_stats;

    if (stats.computing) {
        ImGui::TextUnformatted("统计中...");
        ImGui::End();
        return;
    }

    if (!stats.valid) {
        ImGui::TextDisabled("在测量模式下 Shift+左键框选区域");
        ImGui::End();
        return;
    }

    ImGui::Text("框内点数  %llu",
        static_cast<unsigned long long>(stats.point_count));

    ImGui::Spacing();

    // ── 框选范围（绝对坐标）──
    ImGui::Text("X 范围  [%.6f, %.6f]", stats.world_x_min, stats.world_x_max);
    ImGui::Text("Y 范围  [%.6f, %.6f]", stats.world_y_min, stats.world_y_max);

    ImGui::Spacing();

    // ── 复制按钮 ──
    if (ImGui::Button("复制")) {
        ImGui::SetClipboardText(format_clipboard_text(stats).c_str());
    }

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
