#include "ui/DatasetPanel.hpp"
#include "ui/UiRoot.hpp"

#include "gui/UiFonts.hpp"
#include "imgui.h"

namespace gs3d::ui {

namespace {
constexpr const char* kDatasetWindowName = "项目###DatasetPanel";
}

namespace LayoutMetrics {
constexpr float kPanelInsetX = 10.0f;
} // namespace LayoutMetrics

void draw_dataset_panel(gs3d::app::AppState& state)
{
    if (!state.panels.dataset) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(220.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(kDatasetWindowName, &state.panels.dataset)) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 5.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            ImVec2(LayoutMetrics::kPanelInsetX, 8.0f));
        if (auto* font = gs3d::gui::ui_fonts().panel_title) {
            ImGui::PushFont(font);
        }
        ImGui::TextUnformatted(state.dataset.active_dataset.c_str());
        if (auto* font = gs3d::gui::ui_fonts().panel_title) {
            ImGui::PopFont();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(178, 184, 194, 150));
        ImGui::Text("%llu 点", static_cast<unsigned long long>(state.dataset.point_count));
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::TextUnformatted(state.dataset.file_size.c_str());
        ImGui::PopStyleColor();
        ImGui::Spacing();

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint(
            "##DatasetSearch",
            "筛选项目",
            state.dataset.search_text.data(),
            state.dataset.search_text.size()
        );
        ImGui::Spacing();

        draw_panel_section_label("场景");
        if (ImGui::BeginChild("##DatasetSceneList", ImVec2(0.0f, 0.0f), false)) {
            if (ImGui::TreeNodeEx("当前数据集",
                                  ImGuiTreeNodeFlags_DefaultOpen |
                                      ImGuiTreeNodeFlags_SpanAvailWidth)) {
                for (const auto& item : state.dataset.dataset_tree) {
                    ImGui::Selectable(item.c_str(), false);
                }
                ImGui::TreePop();
            }
            ImGui::Spacing();
            draw_panel_section_label("属性");
            for (const auto& attribute : state.dataset.attributes) {
                ImGui::Bullet();
                ImGui::SameLine(0.0f, 6.0f);
                ImGui::TextUnformatted(attribute.c_str());
            }
            ImGui::Spacing();
            draw_panel_section_label("文件信息");
            ImGui::TextWrapped("路径：%s", state.dataset.path.c_str());
            ImGui::Text("格式：%s", state.dataset.format.c_str());
            ImGui::TextWrapped(
                "包围盒：%s",
                state.dataset.bounding_box.c_str()
            );
            ImGui::EndChild();
        }
        ImGui::PopStyleVar(3);
    }
    ImGui::End();
}

} // namespace gs3d::ui
