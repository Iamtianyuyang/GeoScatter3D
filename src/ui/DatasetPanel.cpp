#include "ui/DatasetPanel.hpp"
#include "ui/UiPalette.hpp"
#include "ui/UiRoot.hpp"
#include "ui/Widgets.hpp"

#include "gui/UiFonts.hpp"
#include "imgui.h"

namespace gs3d::ui {

namespace {
constexpr const char* kDatasetWindowName = "项目###DatasetPanel";
}

namespace LayoutMetrics {
constexpr float kPanelInsetX = 10.0f;
} // namespace LayoutMetrics

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
    auto& dataset_state = dataset != nullptr ? *dataset : state.dataset;

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
        ImGui::PushStyleVar(
            ImGuiStyleVar_FramePadding,
            ImVec2(6.0f * scale, 3.0f * scale)
        );
        ImGui::PushStyleVar(
            ImGuiStyleVar_ItemSpacing,
            ImVec2(6.0f * scale, 5.0f * scale)
        );
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            ImVec2(
                                LayoutMetrics::kPanelInsetX * scale,
                                8.0f * scale
                            ));
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
                for (const auto& item : dataset_state.dataset_tree) {
                    ImGui::TextWrapped("%s", item.c_str());
                }
                ImGui::TreePop();
            }
            ImGui::Spacing();
            // 命名与右侧「属性」(渲染设置) 面板区分开
            draw_panel_section_label("数据属性");
            for (const auto& attribute : dataset_state.attributes) {
                ImGui::Bullet();
                ImGui::SameLine(0.0f, 6.0f * scale);
                ImGui::TextWrapped("%s", attribute.c_str());
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
    ImGui::End();
}

} // namespace gs3d::ui
