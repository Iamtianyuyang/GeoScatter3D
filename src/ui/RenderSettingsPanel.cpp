#include "ui/RenderSettingsPanel.hpp"
#include "ui/UiPalette.hpp"
#include "ui/Widgets.hpp"
#include "ui/UiRoot.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace gs3d::ui {

namespace {
constexpr const char* kRenderSettingsWindowName = "属性###RenderSettings";
} // namespace

namespace LayoutMetrics {
constexpr float kPanelLabelWidth = 82.0f;
} // namespace LayoutMetrics

namespace {

bool begin_labeled_property_table(const char* id)
{
    return ImGui::BeginTable(
        id,
        2,
        ImGuiTableFlags_SizingStretchProp |
            ImGuiTableFlags_NoPadOuterX |
            ImGuiTableFlags_NoPadInnerX
    );
}

// Wide enough for a 2-column [label | value] layout? Threshold scales with
// font size: need at least ~12 chars of width (4 CJK label + 8 value).
bool panel_supports_two_column()
{
    return ImGui::GetContentRegionAvail().x >= 12.0f * ImGui::GetFontSize();
}

void setup_labeled_property_table()
{
    // Proportional label width — adapts to panel width instead of a fixed
    // pixel value. Clamped so labels don't collapse on tiny panels or eat
    // the entire value column on wide ones.
    const float avail = ImGui::GetContentRegionAvail().x;
    const float label_w = std::clamp(avail * 0.40f, 70.0f, 140.0f);
    ImGui::TableSetupColumn(
        "label",
        ImGuiTableColumnFlags_WidthFixed,
        label_w
    );
    ImGui::TableSetupColumn(
        "value",
        ImGuiTableColumnFlags_WidthStretch,
        1.0f
    );
}

// Label + position cursor for the control that follows.
// In 2-column mode: label in column 0, control in column 1 (full column width).
// In compact mode: label on its own line, control on the next line (full
// available width) — prevents the value column from being squeezed to nothing
// when the dock is narrow.
void property_label(const char* label, bool two_col)
{
    if (two_col) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", label);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
    } else {
        ImGui::TextDisabled("%s", label);
        ImGui::SetNextItemWidth(-1.0f);
    }
}

gs3d::app::RenderSettingsCommand& add_render_settings_command(
    gs3d::app::UiActions& actions,
    const std::vector<int>* target_viewports
) {
    actions.render_settings_commands.emplace_back();
    auto& command = actions.render_settings_commands.back();
    if (target_viewports != nullptr) {
        command.has_viewport_scope = true;
        command.viewport_indices = *target_viewports;
    }
    return command;
}

} // namespace

void draw_render_settings(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    const char* window_name,
    bool* open,
    gs3d::app::RenderSettingsState* render_settings,
    const std::vector<int>* target_viewports
) {
    const bool use_default_window = window_name == nullptr;
    if (use_default_window && !state.panels.render_settings) {
        return;
    }
    if (window_name == nullptr) {
        window_name = kRenderSettingsWindowName;
    }
    if (open == nullptr && use_default_window) {
        open = &state.panels.render_settings;
    }
    auto& settings =
        render_settings != nullptr ? *render_settings : state.render_settings;

    ImGui::SetNextWindowSize(ImVec2(280.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(window_name, open)) {
        const float scale = ImGui::GetFontSize() / 13.0f;
        ImGui::PushStyleVar(
            ImGuiStyleVar_FramePadding,
            ImVec2(8.0f * scale, 4.5f * scale)
        );
        ImGui::PushStyleVar(
            ImGuiStyleVar_ItemSpacing,
            ImVec2(8.0f * scale, 7.0f * scale)
        );
        ImGui::PushStyleVar(
            ImGuiStyleVar_CellPadding,
            ImVec2(0.0f, 5.0f * scale)
        );

        draw_panel_section_label("点云外观");

        float point_size = settings.point_size;
        const bool two_col = panel_supports_two_column();
        if (two_col ? begin_labeled_property_table("##RenderAppearanceTable") : true) {
            if (two_col) setup_labeled_property_table();

            property_label("点大小", two_col);
            if (widgets::SliderFloat("##PointSize", &point_size, 1.0f, 10.0f, "%.1f")) {
                settings.point_size = point_size;
                auto& command =
                    add_render_settings_command(actions, target_viewports);
                command.point_size_changed = true;
                command.point_size = point_size;
            }

            property_label("形状", two_col);
            const char* shape_names[] = {"方形", "圆形", "菱形", "三角形"};
            int shape = settings.point_shape;
            if (shape < 0 || shape > 3) shape = 0;
            if (widgets::BeginCombo("##PointShape", shape_names[shape])) {
                for (int i = 0; i < 4; ++i) {
                    if (ImGui::Selectable(shape_names[i], i == shape)) {
                        settings.point_shape = i;
                        auto& command =
                            add_render_settings_command(actions, target_viewports);
                        command.point_shape_changed = true;
                        command.point_shape = i;
                    }
                }
                widgets::EndCombo();
            }

            const auto& height_options = settings.height_by_options;
            const char* h_preview = "无";
            if (!height_options.empty()) {
                const int h_idx = std::clamp(
                    settings.height_attr_index,
                    0,
                    static_cast<int>(height_options.size()) - 1
                );
                h_preview = height_options[static_cast<std::size_t>(h_idx)].c_str();
            }
            property_label("高度来源", two_col);
            if (widgets::BeginCombo("##HeightSource", h_preview)) {
                for (std::size_t i = 0; i < height_options.size(); ++i) {
                    const bool selected =
                        static_cast<int>(i) == settings.height_attr_index;
                    if (ImGui::Selectable(height_options[i].c_str(), selected)) {
                        settings.height_attr_index = static_cast<int>(i);
                        auto& command =
                            add_render_settings_command(actions, target_viewports);
                        command.height_by_changed = true;
                        command.height_by_index = static_cast<int>(i);
                    }
                }
                widgets::EndCombo();
            }

            float exag = settings.height_exaggeration;
            property_label("高度缩放", two_col);
            if (two_col) {
                ImGui::SetNextItemWidth(ImGui::CalcTextSize("000.00x").x + 24.0f);
            }
            if (widgets::DragFloat("##HeightExaggeration", &exag, 0.1f,
                    0.01f, 100.0f, "%.2fx")) {
                settings.height_exaggeration = exag;
                auto& command =
                    add_render_settings_command(actions, target_viewports);
                command.height_exag_changed = true;
                command.height_exag = exag;
            }

            const auto& color_options = settings.color_by_options;
            const char* preview = "无";
            if (!color_options.empty()) {
                const int preview_index = std::clamp(
                    settings.color_attr_index,
                    0,
                    static_cast<int>(color_options.size()) - 1
                );
                preview = color_options[static_cast<std::size_t>(preview_index)].c_str();
            }
            property_label("着色", two_col);
            if (widgets::BeginCombo("##ColorBy", preview)) {
                for (std::size_t i = 0; i < color_options.size(); ++i) {
                    const bool selected =
                        static_cast<int>(i) == settings.color_attr_index;
                    if (ImGui::Selectable(color_options[i].c_str(), selected)) {
                        settings.color_attr_index = static_cast<int>(i);
                        auto& command =
                            add_render_settings_command(actions, target_viewports);
                        command.color_by_changed = true;
                        command.color_by_index = static_cast<int>(i);
                    }
                }
                widgets::EndCombo();
            }

            if (two_col) ImGui::EndTable();
        }

        ImGui::Spacing();
        draw_panel_section_label("色调映射");

        // ── 色标选择 ──
        {
            const char* colormap_names[] = {
                "地震（蓝-白-红）",
                "Viridis",
                "Jet",
                "Grayscale",
                "Thermal",
                "Coolwarm",
                "Turbo",
                "Plasma",
                "Rainbow256"
            };
            int cmap = settings.colormap_index;
            if (cmap < 0 || cmap > 8) cmap = 0;
            ImGui::TextUnformatted("色标");
            if (widgets::BeginCombo("##Colormap", colormap_names[cmap])) {
                for (int i = 0; i < 9; ++i) {
                    if (ImGui::Selectable(colormap_names[i], i == cmap)) {
                        settings.colormap_index = i;
                        auto& command =
                            add_render_settings_command(actions, target_viewports);
                        command.colormap_changed = true;
                        command.colormap_index = i;
                    }
                }
                widgets::EndCombo();
            }

            // 色标预览条 — 根据当前选中的色标切换颜色
            const ImVec2 start = ImGui::GetCursorScreenPos();
            const float bar_width = ImGui::GetContentRegionAvail().x;
            const float bar_height = 14.0f * scale;
            ImGui::InvisibleButton("##ColorMapPreview",
                ImVec2(bar_width, bar_height));
            ImDrawList* dl = ImGui::GetWindowDrawList();

            // Five control colours per map, matching the shader. Drawing the
            // preview as adjacent horizontal gradients keeps the legend free
            // of the diagonal interpolation artefacts caused by a single
            // four-corner rectangle.
            struct CmapColors { ImU32 colors[5]; };
            const CmapColors cmap_colors[9] = {
                // Seismic: navy → blue → ivory → red → maroon
                { { IM_COL32(13, 41, 87, 255), IM_COL32(51, 125, 184, 255),
                    IM_COL32(240, 237, 224, 255), IM_COL32(196, 69, 51, 255),
                    IM_COL32(110, 13, 20, 255) } },
                { { IM_COL32(68, 1, 84, 255), IM_COL32(72, 36, 117, 255),
                    IM_COL32(33, 145, 140, 255), IM_COL32(94, 201, 98, 255),
                    IM_COL32(253, 231, 37, 255) } },
                { { IM_COL32(0, 0, 128, 255), IM_COL32(0, 128, 255, 255),
                    IM_COL32(0, 255, 255, 255), IM_COL32(255, 255, 0, 255),
                    IM_COL32(255, 0, 0, 255) } },
                { { IM_COL32(0, 0, 0, 255), IM_COL32(64, 64, 64, 255),
                    IM_COL32(128, 128, 128, 255), IM_COL32(192, 192, 192, 255),
                    IM_COL32(255, 255, 255, 255) } },
                { { IM_COL32(0, 0, 0, 255), IM_COL32(128, 0, 0, 255),
                    IM_COL32(220, 48, 0, 255), IM_COL32(255, 160, 0, 255),
                    IM_COL32(255, 255, 180, 255) } },
                { { IM_COL32(59, 76, 192, 255), IM_COL32(120, 150, 230, 255),
                    IM_COL32(245, 245, 245, 255), IM_COL32(230, 120, 120, 255),
                    IM_COL32(180, 4, 38, 255) } },
                { { IM_COL32(48, 18, 59, 255), IM_COL32(24, 104, 184, 255),
                    IM_COL32(38, 188, 135, 255), IM_COL32(235, 206, 47, 255),
                    IM_COL32(122, 4, 3, 255) } },
                { { IM_COL32(13, 8, 135, 255), IM_COL32(84, 3, 160, 255),
                    IM_COL32(182, 55, 121, 255), IM_COL32(237, 121, 33, 255),
                    IM_COL32(240, 249, 33, 255) } },
                { { IM_COL32(0, 0, 128, 255), IM_COL32(0, 128, 255, 255),
                    IM_COL32(0, 255, 128, 255), IM_COL32(255, 255, 0, 255),
                    IM_COL32(128, 0, 0, 255) } },
            };
            int ci = settings.colormap_index;
            if (ci < 0 || ci > 8) ci = 0;
            const auto& cc = cmap_colors[ci];
            for (int i = 0; i < 4; ++i) {
                const float x0 = start.x + bar_width * i / 4.0f;
                const float x1 = start.x + bar_width * (i + 1) / 4.0f;
                dl->AddRectFilledMultiColor(
                    ImVec2(x0, start.y),
                    ImVec2(x1, start.y + bar_height),
                    cc.colors[i],
                    cc.colors[i + 1],
                    cc.colors[i + 1],
                    cc.colors[i]
                );
            }
        }

        // ── 数据范围显示 ──
        {
            ImGui::Spacing();
            char range_buf[64];
            std::snprintf(range_buf, sizeof(range_buf),
                "%.4g – %.4g",
                static_cast<double>(settings.data_value_min),
                static_cast<double>(settings.data_value_max));
            ImGui::TextUnformatted("数据范围:");
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, to_u32(palette::kTextDim, 180));
            ImGui::TextUnformatted(range_buf);
            ImGui::PopStyleColor();
        }

        // ── 数据范围裁切 ──
        {
            const float data_lo = settings.data_value_min;
            const float data_hi = settings.data_value_max;
            const float data_range = data_hi - data_lo;
            const float step = data_range > 0.0f ? data_range * 0.001f : 0.001f;

            bool clip_enabled = settings.value_clip_enabled;
            if (widgets::Checkbox("值域裁切", &clip_enabled)) {
                settings.value_clip_enabled = clip_enabled;
                if (clip_enabled) {
                    // 首次启用时初始化为当前属性的完整数据范围
                    settings.value_clip_min = data_lo;
                    settings.value_clip_max = data_hi;
                }
                auto& command =
                    add_render_settings_command(actions, target_viewports);
                command.value_clip_changed = true;
                command.value_clip_enabled = clip_enabled;
                command.value_clip_min = settings.value_clip_min;
                command.value_clip_max = settings.value_clip_max;
            }
            if (clip_enabled) {
                ImGui::Indent(12.0f);
                float lo = settings.value_clip_min;
                float hi = settings.value_clip_max;
                // Clamp to data range if stale
                if (lo < data_lo) lo = data_lo;
                if (hi > data_hi) hi = data_hi;
                ImGui::SetNextItemWidth(
                    two_col ? (ImGui::CalcTextSize("0.0000").x + 48.0f) : -1.0f);
                if (widgets::DragFloat("下限", &lo, step, data_lo, hi, "%.4g")) {
                    settings.value_clip_min = lo;
                    auto& command =
                        add_render_settings_command(actions, target_viewports);
                    command.value_clip_changed = true;
                    command.value_clip_enabled = true;
                    command.value_clip_min = lo;
                    command.value_clip_max = hi;
                }
                ImGui::SetNextItemWidth(
                    two_col ? (ImGui::CalcTextSize("0.0000").x + 48.0f) : -1.0f);
                if (widgets::DragFloat("上限", &hi, step, lo, data_hi, "%.4g")) {
                    settings.value_clip_max = hi;
                    auto& command =
                        add_render_settings_command(actions, target_viewports);
                    command.value_clip_changed = true;
                    command.value_clip_enabled = true;
                    command.value_clip_min = lo;
                    command.value_clip_max = hi;
                }
                ImGui::Unindent(12.0f);
            }
        }

        ImGui::PopStyleVar(3);
    }
    ImGui::End();
}

} // namespace gs3d::ui
