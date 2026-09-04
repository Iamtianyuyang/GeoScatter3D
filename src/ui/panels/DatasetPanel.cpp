#include "ui/DatasetPanel.hpp"
#include "ui/UiPalette.hpp"
#include "ui/UiRoot.hpp"
#include "ui/Widgets.hpp"

#include "ui/UiFonts.hpp"
#include "imgui.h"

namespace gs3d::ui {

namespace {
constexpr const char* kDatasetWindowName = "项目###DatasetPanel";
}

namespace LayoutMetrics {
constexpr float kPanelInsetX = 10.0f;
} // namespace LayoutMetrics

void draw_dataset_panel_content(
    gs3d::app::AppState& state,
    gs3d::app::DatasetSummaryState* dataset
)
{
    auto& dataset_state = dataset != nullptr ? *dataset : state.dataset;

    const float scale = ImGui::GetFontSize() / 13.0f;
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        ImVec2(6.0f * scale, 3.0f * scale)
    );
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(6.0f * scale, 5.0f * scale)
    );
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(
            LayoutMetrics::kPanelInsetX * scale,
            8.0f * scale
        )
    );
    if (auto* font = gs3d::gui::ui_fonts().panel_title) {
        ImGui::PushFont(font);
    }
    ImGui::TextWrapped("%s", dataset_state.active_dataset.c_str());
    if (auto* font = gs3d::gui::ui_fonts().panel_title) {
        ImGui::PopFont();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, to_u32(palette::kTextDim, 190));
    ImGui::Text("%llu 点", static_cast<unsigned long long>(dataset_state.point_count));
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::TextUnformatted(dataset_state.file_size.c_str());
    ImGui::PopStyleColor();
    ImGui::Spacing();

    ImGui::SetNextItemWidth(-1.0f);
    widgets::InputTextWithHint(
        "##DatasetSearch",
        "筛选项目",
        dataset_state.search_text.data(),
        dataset_state.search_text.size()
    );
    ImGui::Spacing();

    draw_panel_section_label("场景");
    ImGui::BeginChild("##DatasetSceneList", ImVec2(0.0f, 0.0f), false);
    {
        if (ImGui::TreeNodeEx("当前数据集",
                              ImGuiTreeNodeFlags_DefaultOpen |
                                  ImGuiTreeNodeFlags_SpanAvailWidth)) {
            if (ImGui::TreeNodeEx(
                    "数据概览",
                    ImGuiTreeNodeFlags_DefaultOpen |
                        ImGuiTreeNodeFlags_SpanAvailWidth
                )) {
                ImGui::TextDisabled("名称");
                ImGui::SameLine();
                ImGui::TextWrapped(
                    "%s",
                    dataset_state.active_dataset.c_str()
                );
                ImGui::TextDisabled("点数");
                ImGui::SameLine();
                ImGui::Text(
                    "%llu",
                    static_cast<unsigned long long>(
                        dataset_state.point_count
                    )
                );
                ImGui::TextDisabled("格式");
                ImGui::SameLine();
                ImGui::TextUnformatted(dataset_state.format.c_str());
                ImGui::TextDisabled("路径");
                ImGui::TextWrapped(
                    "%s",
                    dataset_state.path.c_str()
                );
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx(
                    "瓦片",
                    ImGuiTreeNodeFlags_DefaultOpen |
                        ImGuiTreeNodeFlags_SpanAvailWidth
                )) {
                if (dataset_state.tile_details.empty()) {
                    ImGui::TextDisabled("当前数据集未启用瓦片流式加载");
                } else {
                    ImGui::Text(
                        "当前：%u 已加载 / %u 等待",
                        state.performance.loaded_tiles,
                        state.performance.pending_tiles
                    );
                    for (const auto& detail : dataset_state.tile_details) {
                        ImGui::TextWrapped("%s", detail.c_str());
                    }
                }
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx(
                    "细节层级",
                    ImGuiTreeNodeFlags_DefaultOpen |
                        ImGuiTreeNodeFlags_SpanAvailWidth
                )) {
                if (dataset_state.lod_details.empty()) {
                    ImGui::TextDisabled("当前数据集未启用细节层级");
                } else {
                    ImGui::TextUnformatted(state.performance.lod_mode.c_str());
                    for (const auto& detail : dataset_state.lod_details) {
                        ImGui::TextWrapped("%s", detail.c_str());
                    }
                }
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx(
                    "属性",
                    ImGuiTreeNodeFlags_DefaultOpen |
                        ImGuiTreeNodeFlags_SpanAvailWidth
                )) {
                ImGui::Text(
                    "共 %zu 个属性",
                    dataset_state.attributes.size()
                );
                for (const auto& attribute : dataset_state.attributes) {
                    ImGui::Bullet();
                    ImGui::SameLine(0.0f, 6.0f * scale);
                    ImGui::TextWrapped("%s", attribute.c_str());
                }
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }
        ImGui::Spacing();
        draw_panel_section_label("文件信息");
        if (ImGui::BeginTable(
                "##DatasetFileInfo",
                2,
                ImGuiTableFlags_SizingStretchProp |
                    ImGuiTableFlags_NoPadOuterX
            )) {
            ImGui::TableSetupColumn(
                "label",
                ImGuiTableColumnFlags_WidthFixed,
                4.0f * ImGui::GetFontSize()
            );
            ImGui::TableSetupColumn(
                "value",
                ImGuiTableColumnFlags_WidthStretch
            );
            const auto info_row = [](const char* label,
                                     const std::string& value) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("%s", label);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextWrapped("%s", value.c_str());
            };
            info_row("路径", dataset_state.path);
            info_row("格式", dataset_state.format);
            info_row("包围盒", dataset_state.bounding_box);
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
}

void draw_dataset_panel(
    gs3d::app::AppState& state,
    const char* window_name,
    bool* open,
    gs3d::app::DatasetSummaryState* dataset
)
{
    const bool use_default_window = window_name == nullptr;
    if (use_default_window && !state.panels.dataset) {
        return;
    }
    if (window_name == nullptr) {
        window_name = kDatasetWindowName;
    }
    if (open == nullptr && use_default_window) {
        open = &state.panels.dataset;
    }

    const float scale = ImGui::GetFontSize() / 13.0f;
    ImGui::SetNextWindowSize(
        ImVec2(360.0f * scale, 520.0f * scale),
        ImGuiCond_FirstUseEver
    );
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(280.0f * scale, 360.0f * scale),
        ImVec2(100000.0f, 100000.0f)
    );
    if (ImGui::Begin(window_name, open)) {
        draw_dataset_panel_content(state, dataset);
    }
    ImGui::End();
}

} // namespace gs3d::ui
