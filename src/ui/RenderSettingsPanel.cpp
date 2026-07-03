#include "ui/RenderSettingsPanel.hpp"
#include "ui/UiRoot.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdio>

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

void setup_labeled_property_table()
{
    ImGui::TableSetupColumn(
        "label",
        ImGuiTableColumnFlags_WidthFixed,
        LayoutMetrics::kPanelLabelWidth
    );
    ImGui::TableSetupColumn(
        "value",
        ImGuiTableColumnFlags_WidthStretch,
        1.0f
    );
}

void property_table_label(const char* label)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", label);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-1.0f);
}

} // namespace

void draw_render_settings(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions
) {
    if (!state.panels.render_settings) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(240.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(
            kRenderSettingsWindowName,
            &state.panels.render_settings
        )) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0.0f, 4.0f));

        draw_panel_section_label("点云外观");

        float point_size = state.render_settings.point_size;
        if (begin_labeled_property_table("##RenderAppearanceTable")) {
            setup_labeled_property_table();

            property_table_label("点大小");
            if (ImGui::SliderFloat("##PointSize", &point_size, 1.0f, 10.0f, "%.1f")) {
                state.render_settings.point_size = point_size;
                actions.point_size_changed = true;
                actions.point_size = point_size;
            }

            property_table_label("形状");
            const char* shape_names[] = {"方形", "圆形", "菱形", "三角形"};
            int shape = state.render_settings.point_shape;
            if (shape < 0 || shape > 3) shape = 0;
            if (ImGui::BeginCombo("##PointShape", shape_names[shape])) {
                for (int i = 0; i < 4; ++i) {
                    if (ImGui::Selectable(shape_names[i], i == shape)) {
                        state.render_settings.point_shape = i;
                        actions.point_shape_changed = true;
                        actions.point_shape = i;
                    }
                }
                ImGui::EndCombo();
            }

            const auto& height_options = state.render_settings.height_by_options;
            const char* h_preview = "无";
            if (!height_options.empty()) {
                const int h_idx = std::clamp(
                    state.render_settings.height_attr_index,
                    0,
                    static_cast<int>(height_options.size()) - 1
                );
                h_preview = height_options[static_cast<std::size_t>(h_idx)].c_str();
            }
            property_table_label("高度来源");
            if (ImGui::BeginCombo("##HeightSource", h_preview)) {
                for (std::size_t i = 0; i < height_options.size(); ++i) {
                    const bool selected =
                        static_cast<int>(i) == state.render_settings.height_attr_index;
                    if (ImGui::Selectable(height_options[i].c_str(), selected)) {
                        state.render_settings.height_attr_index = static_cast<int>(i);
                        actions.height_by_changed = true;
                        actions.height_by_index = static_cast<int>(i);
                    }
                }
                ImGui::EndCombo();
            }

            float exag = state.render_settings.height_exaggeration;
            property_table_label("高度缩放");
            ImGui::SetNextItemWidth(ImGui::CalcTextSize("000.00x").x + 24.0f);
            if (ImGui::DragFloat("##HeightExaggeration", &exag, 0.1f,
                    0.01f, 100.0f, "%.2fx")) {
                state.render_settings.height_exaggeration = exag;
                actions.height_exag_changed = true;
                actions.height_exag = exag;
            }

            const auto& color_options = state.render_settings.color_by_options;
            const char* preview = "无";
            if (!color_options.empty()) {
                const int preview_index = std::clamp(
                    state.render_settings.color_attr_index,
                    0,
                    static_cast<int>(color_options.size()) - 1
                );
                preview = color_options[static_cast<std::size_t>(preview_index)].c_str();
            }
            property_table_label("着色");
            if (ImGui::BeginCombo("##ColorBy", preview)) {
                for (std::size_t i = 0; i < color_options.size(); ++i) {
                    const bool selected =
                        static_cast<int>(i) == state.render_settings.color_attr_index;
                    if (ImGui::Selectable(color_options[i].c_str(), selected)) {
                        state.render_settings.color_attr_index = static_cast<int>(i);
                        actions.color_by_changed = true;
                        actions.color_by_index = static_cast<int>(i);
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::EndTable();
        }

        ImGui::Spacing();
        draw_panel_section_label("色调映射");

        // ── 色标选择 ──
        {
            const char* colormap_names[] = {
                "Geo",
                "Viridis",
                "Jet",
                "Grayscale",
                "Thermal",
                "Coolwarm",
                "Turbo",
                "Plasma"
            };
            int cmap = state.render_settings.colormap_index;
            if (cmap < 0 || cmap > 7) cmap = 0;
            ImGui::TextUnformatted("色标");
            if (ImGui::BeginCombo("##Colormap", colormap_names[cmap])) {
                for (int i = 0; i < 8; ++i) {
                    if (ImGui::Selectable(colormap_names[i], i == cmap)) {
                        state.render_settings.colormap_index = i;
                        actions.colormap_changed = true;
                        actions.colormap_index = i;
                    }
                }
                ImGui::EndCombo();
            }

            // 色标预览条 — 根据当前选中的色标切换颜色
            const ImVec2 start = ImGui::GetCursorScreenPos();
            const float bar_width = ImGui::GetContentRegionAvail().x;
            ImGui::InvisibleButton("##ColorMapPreview",
                ImVec2(bar_width, 14.0f));
            ImDrawList* dl = ImGui::GetWindowDrawList();

            // Each colormap: {left, mid-left, mid-right, right} corner colours
            // matching the approximate endpoints used in the fragment shader.
            struct CmapColors { ImU32 c0, c1, c2, c3; };
            const CmapColors cmap_colors[8] = {
                // Geo: blue → cyan → green → yellow-red
                { IM_COL32(60,  105, 215, 255), IM_COL32(55,  190, 175, 255),
                  IM_COL32(235, 190, 75,  255), IM_COL32(218, 82,  76,  255) },
                // Viridis: deep purple → teal → green → yellow
                { IM_COL32(68,  1,   84,  255), IM_COL32(59,  82,  139, 255),
                  IM_COL32(33,  145, 140, 255), IM_COL32(94,  201, 98,  255) },
                // Jet: blue → cyan → yellow → red
                { IM_COL32(0,   0,   143, 255), IM_COL32(0,   191, 255, 255),
                  IM_COL32(255, 255, 0,   255), IM_COL32(255, 0,   0,   255) },
                // Grayscale: black → gray → light → white
                { IM_COL32(0,   0,   0,   255), IM_COL32(85,  85,  85,  255),
                  IM_COL32(170, 170, 170, 255), IM_COL32(255, 255, 255, 255) },
                // Thermal: black → red → orange → yellow-white
                { IM_COL32(0,   0,   0,   255), IM_COL32(153, 0,   0,   255),
                  IM_COL32(255, 128, 0,   255), IM_COL32(255, 255, 128, 255) },
                // Coolwarm: blue → light blue → light red → dark red
                { IM_COL32(59,  76,  192, 255), IM_COL32(144, 161, 255, 255),
                  IM_COL32(255, 128, 128, 255), IM_COL32(180, 4,   38,  255) },
                // Turbo: dark blue → teal → green-yellow → orange-red
                { IM_COL32(48,  18,  59,  255), IM_COL32(18,  145, 190, 255),
                  IM_COL32(162, 211, 55,  255), IM_COL32(122, 4,   3,   255) },
                // Plasma: dark purple → magenta → orange → yellow
                { IM_COL32(13,  8,   135, 255), IM_COL32(126, 3,   168, 255),
                  IM_COL32(224, 100, 40,  255), IM_COL32(240, 249, 33,  255) },
            };
            int ci = state.render_settings.colormap_index;
            if (ci < 0 || ci > 7) ci = 0;
            const auto& cc = cmap_colors[ci];
            dl->AddRectFilledMultiColor(
                start,
                {start.x + bar_width, start.y + 14.0f},
                cc.c0, cc.c1, cc.c2, cc.c3
            );
        }

        // ── 数据范围显示 ──
        {
            ImGui::Spacing();
            char range_buf[64];
            std::snprintf(range_buf, sizeof(range_buf),
                "%.4g – %.4g",
                static_cast<double>(state.render_settings.data_value_min),
                static_cast<double>(state.render_settings.data_value_max));
            ImGui::TextUnformatted("数据范围:");
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(176, 182, 192, 180));
            ImGui::TextUnformatted(range_buf);
            ImGui::PopStyleColor();
        }

        // ── 数据范围裁切 ──
        {
            const float data_lo = state.render_settings.data_value_min;
            const float data_hi = state.render_settings.data_value_max;
            const float data_range = data_hi - data_lo;
            const float step = data_range > 0.0f ? data_range * 0.001f : 0.001f;

            bool clip_enabled = state.render_settings.value_clip_enabled;
            if (ImGui::Checkbox("值域裁切", &clip_enabled)) {
                state.render_settings.value_clip_enabled = clip_enabled;
                if (clip_enabled) {
                    // 首次启用时初始化为当前属性的完整数据范围
                    state.render_settings.value_clip_min = data_lo;
                    state.render_settings.value_clip_max = data_hi;
                }
                actions.value_clip_changed = true;
                actions.value_clip_enabled = clip_enabled;
                actions.value_clip_min = state.render_settings.value_clip_min;
                actions.value_clip_max = state.render_settings.value_clip_max;
            }
            if (clip_enabled) {
                ImGui::Indent(12.0f);
                float lo = state.render_settings.value_clip_min;
                float hi = state.render_settings.value_clip_max;
                // Clamp to data range if stale
                if (lo < data_lo) lo = data_lo;
                if (hi > data_hi) hi = data_hi;
                ImGui::SetNextItemWidth(
                    ImGui::CalcTextSize("0.0000").x + 48.0f);
                if (ImGui::DragFloat("下限", &lo, step, data_lo, hi, "%.4g")) {
                    state.render_settings.value_clip_min = lo;
                    actions.value_clip_changed = true;
                    actions.value_clip_enabled = true;
                    actions.value_clip_min = lo;
                    actions.value_clip_max = hi;
                }
                ImGui::SetNextItemWidth(
                    ImGui::CalcTextSize("0.0000").x + 48.0f);
                if (ImGui::DragFloat("上限", &hi, step, lo, data_hi, "%.4g")) {
                    state.render_settings.value_clip_max = hi;
                    actions.value_clip_changed = true;
                    actions.value_clip_enabled = true;
                    actions.value_clip_min = lo;
                    actions.value_clip_max = hi;
                }
                ImGui::Unindent(12.0f);
            }
        }

        ImGui::Spacing();
        draw_panel_section_label("流式加载");
        if (begin_labeled_property_table("##StreamingInfoTable")) {
            setup_labeled_property_table();

            property_table_label("GPU 瓦片");
            ImGui::TextUnformatted(state.render_settings.cache_usage.c_str());

            property_table_label("CPU 缓存");
            ImGui::TextUnformatted(state.render_settings.cpu_cache_usage.c_str());

            property_table_label("缓存命中");
            ImGui::Text("%.1f%%", state.render_settings.cache_hit_rate);

            ImGui::EndTable();
        }
        if (ImGui::Button("清空缓存")) {
            actions.clear_cache_requested = true;
        }
        ImGui::PopStyleVar(3);
    }
    ImGui::End();
}

} // namespace gs3d::ui
