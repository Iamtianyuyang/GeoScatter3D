#include "ui/RegionStatsPanel.hpp"

#include "gui/UiFonts.hpp"
#include "ui/Widgets.hpp"

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
    oss << stats.primary_label << " 最小值/最大值/平均值: "
        << stats.fold_min << " / " << stats.fold_max << " / " << stats.fold_avg << '\n';
    oss << stats.secondary_label << " 最小值/最大值/平均值: "
        << stats.elev_min << " / " << stats.elev_max << " / " << stats.elev_avg;

    return oss.str();
}
} // namespace

void draw_region_stats_panel(
    gs3d::app::AppState& state,
    const char* window_name,
    bool* open,
    const gs3d::app::RegionStatsResult* region_stats
)
{
    const bool use_default_window = window_name == nullptr;
    if (use_default_window && !state.panels.region_stats) {
        return;
    }
    if (window_name == nullptr) {
        window_name = kRegionStatsWindowName;
    }
    if (open == nullptr && use_default_window) {
        open = &state.panels.region_stats;
    }

    if (!ImGui::Begin(window_name, open)) {
        ImGui::End();
        return;
    }

    const auto& stats = region_stats != nullptr
        ? *region_stats
        : gs3d::app::region_stats_for_view(
            state,
            state.active_viewport_index
        );

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
    if (widgets::Button("复制")) {
        ImGui::SetClipboardText(format_clipboard_text(stats).c_str());
    }

    ImGui::Separator();

    const char* primary_label = stats.primary_label.empty()
        ? "Fold" : stats.primary_label.c_str();
    const char* secondary_label = stats.secondary_label.empty()
        ? "Elevation" : stats.secondary_label.c_str();

    // ── 主属性 ──
    if (auto* font = gs3d::gui::ui_fonts().panel_title) {
        ImGui::PushFont(font);
    }
    ImGui::TextUnformatted(primary_label);
    if (auto* font = gs3d::gui::ui_fonts().panel_title) {
        ImGui::PopFont();
    }

    ImGui::Text("最小值  %.6f", static_cast<double>(stats.fold_min));
    ImGui::Text("最大值  %.6f", static_cast<double>(stats.fold_max));
    ImGui::Text("平均值  %.6f", static_cast<double>(stats.fold_avg));

    ImGui::Spacing();

    // ── 副属性 ──
    if (auto* font = gs3d::gui::ui_fonts().panel_title) {
        ImGui::PushFont(font);
    }
    ImGui::TextUnformatted(secondary_label);
    if (auto* font = gs3d::gui::ui_fonts().panel_title) {
        ImGui::PopFont();
    }

    ImGui::Text("最小值  %.6f", static_cast<double>(stats.elev_min));
    ImGui::Text("最大值  %.6f", static_cast<double>(stats.elev_max));
    ImGui::Text("平均值  %.6f", static_cast<double>(stats.elev_avg));

    ImGui::End();
}

} // namespace gs3d::ui
