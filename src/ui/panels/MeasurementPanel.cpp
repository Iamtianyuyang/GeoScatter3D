#include "ui/MeasurementPanel.hpp"
#include "ui/Theme.hpp"
#include "ui/UiPalette.hpp"
#include "ui/Widgets.hpp"

#include "imgui.h"

namespace gs3d::ui {

namespace {
constexpr const char* kMeasurementWindowName = "测量###Measurement";
} // namespace

void draw_measurement_panel(
    gs3d::app::AppState& state,
    const char* window_name,
    bool* open,
    gs3d::app::MeasurementManager* measurement
)
{
    const bool use_default_window = window_name == nullptr;
    if (use_default_window && !state.panels.measurement) {
        return;
    }
    if (window_name == nullptr) {
        window_name = kMeasurementWindowName;
    }
    if (open == nullptr && use_default_window) {
        open = &state.panels.measurement;
    }

    if (!ImGui::Begin(window_name, open)) {
        ImGui::End();
        return;
    }

    auto& mgr = measurement != nullptr
        ? *measurement
        : gs3d::app::measurement_for_view(
            state,
            state.active_viewport_index
        );

    // ── 测量模式开关 ──
    bool measure_active = mgr.measure_mode_active();
    if (widgets::Checkbox("测量模式", &measure_active)) {
        mgr.set_measure_mode(measure_active);
        if (!measure_active) {
            mgr.clear_pending();
        }
    }
    if (mgr.has_pending()) {
        ImGui::SameLine();
        // 亮主题的面板底近白，测量黄 0xF1C21B 压上去几乎不可读；
        // 亮主题降为深琥珀，暗主题保持测量黄。
        const ImVec4 pending_color =
            theme_tokens(active_theme()).dark
                ? palette::kYellow
                : ImVec4(0.72f, 0.51f, 0.02f, 1.0f);
        ImGui::TextColored(
            pending_color,
            "等待第二个点..."
        );
    }

    // ── 距离显示模式 ──
    // DistanceDisplayMode 枚举值即 0/1/2，与分段控件索引一一对应
    int display_mode = static_cast<int>(mgr.display_mode());
    ImGui::TextUnformatted("距离显示:");
    ImGui::SameLine();
    const char* const display_modes[] = {"三维", "平面", "都显示"};
    widgets::Segmented(
        "##DistanceDisplayMode", display_modes, 3, &display_mode);
    mgr.set_display_mode(
        static_cast<gs3d::app::DistanceDisplayMode>(display_mode));

    ImGui::Separator();

    // ── 全部删除（不删固定的）──
    if (widgets::Button(
            "全部删除（保留固定）",
            widgets::ButtonVariant::kDanger)) {
        mgr.remove_all_unfixed();
    }

    ImGui::Separator();

    // ── 线段列表 ──
    if (mgr.line_count() == 0) {
        ImGui::TextDisabled("暂无测量线");
    } else {
        // Use a child region for scrolling when there are many lines.
        ImGui::BeginChild("##MeasureLines",
            ImVec2(0.0f, ImGui::GetContentRegionAvail().y),
            false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

        for (std::size_t i = 0; i < mgr.line_count(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            auto& line = mgr.lines()[i];

            // Color swatch
            ImVec4 col = ImGui::ColorConvertU32ToFloat4(line.color);
            float col_arr[4] = {col.x, col.y, col.z, col.w};
            if (ImGui::ColorEdit4("##Color", col_arr,
                    ImGuiColorEditFlags_NoInputs |
                    ImGuiColorEditFlags_NoLabel |
                    ImGuiColorEditFlags_AlphaBar)) {
                mgr.set_line_color(i,
                    ImGui::ColorConvertFloat4ToU32(
                        ImVec4(col_arr[0], col_arr[1],
                               col_arr[2], col_arr[3])));
            }
            ImGui::SameLine();

            // Distance label
            ImGui::TextUnformatted(
                line.distance_label(mgr.display_mode()).c_str());
            ImGui::SameLine();

            // Fixed toggle
            bool fixed = line.fixed;
            if (widgets::Checkbox("固定", &fixed)) {
                mgr.toggle_fixed(i);
            }
            ImGui::SameLine();

            // Delete button
            if (widgets::Button(
                    "删除",
                    widgets::ButtonVariant::kDanger,
                    ImVec2(0.0f, 0.0f),
                    true)) {
                mgr.remove_line(i);
                ImGui::PopID();
                // Don't access `line` after removal.
                continue;
            }

            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    ImGui::End();
}

} // namespace gs3d::ui
