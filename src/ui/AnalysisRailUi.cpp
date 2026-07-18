#include "ui/AnalysisRailUi.hpp"

#include "ui/AnalysisRailLayout.hpp"
#include "ui/ColormapPreview.hpp"
#include "ui/FloatingDockLayout.hpp"
#include "ui/Theme.hpp"
#include "ui/UiRoot.hpp"
#include "ui/ViewportCanvas.hpp"
#include "ui/WorkspaceManager.hpp"

#include "gui/UiFonts.hpp"
#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace gs3d::ui {

namespace {

constexpr ImU32 kBg = IM_COL32(15, 18, 24, 255);
constexpr ImU32 kPanel = IM_COL32(22, 26, 34, 255);
constexpr ImU32 kPanel2 = IM_COL32(28, 33, 43, 255);
constexpr ImU32 kLine = IM_COL32(38, 45, 58, 255);
constexpr ImU32 kText = IM_COL32(230, 235, 244, 255);
constexpr ImU32 kText2 = IM_COL32(154, 166, 184, 255);
constexpr ImU32 kText3 = IM_COL32(95, 107, 125, 255);
constexpr ImU32 kCyan = IM_COL32(63, 210, 255, 255);
constexpr ImU32 kGreen = IM_COL32(74, 222, 128, 255);
constexpr ImU32 kAmber = IM_COL32(251, 191, 36, 255);
constexpr ImU32 kTeal = IM_COL32(45, 212, 191, 255);
constexpr ImU32 kRed = IM_COL32(248, 113, 113, 255);

ImVec4 color4(const ImU32 color) {
    return ImGui::ColorConvertU32ToFloat4(color);
}

ImFont* bold_font() {
    return gs3d::gui::ui_fonts().bold;
}

ImFont* small_font() {
    return gs3d::gui::ui_fonts().small;
}

ImFont* status_font() {
    return gs3d::gui::ui_fonts().status;
}

struct ScopedFont {
    explicit ScopedFont(ImFont* font)
        : active(font != nullptr) {
        if (active) {
            ImGui::PushFont(font);
        }
    }
    ~ScopedFont() {
        if (active) {
            ImGui::PopFont();
        }
    }
    bool active = false;
};

void set_region_cursor(const float x, const float y) {
    ImGui::SetCursorPos(ImVec2(x, y));
}

bool begin_region(
    const char* id,
    const ImVec2& pos,
    const ImVec2& size,
    const ImU32 background,
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse
) {
    set_region_cursor(pos.x, pos.y);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, background);
    const bool visible = ImGui::BeginChild(
        id,
        size,
        ImGuiChildFlags_None,
        flags
    );
    ImGui::PopStyleColor();
    return visible;
}

void section_label(const char* text, const float scale) {
    ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));
    ScopedFont font(small_font());
    ImGui::PushStyleColor(ImGuiCol_Text, kText3);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0.0f, 1.0f * scale));
}

void muted_text(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, kText3);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void key_value(const char* key, const std::string& value) {
    const float value_x = std::max(
        72.0f,
        ImGui::GetContentRegionAvail().x * 0.43f
    );
    ImGui::PushStyleColor(ImGuiCol_Text, kText3);
    ImGui::TextUnformatted(key);
    ImGui::PopStyleColor();
    ImGui::SameLine(value_x);
    ImGui::PushStyleColor(ImGuiCol_Text, kText2);
    ImGui::TextWrapped("%s", value.c_str());
    ImGui::PopStyleColor();
}

void draw_rail_icon(
    ImDrawList* draw,
    const int icon,
    const ImVec2 center,
    const ImU32 color,
    const float scale
) {
    const float s = scale;
    if (icon == 0) {
        draw->AddRect(
            ImVec2(center.x - 7.0f * s, center.y - 7.0f * s),
            ImVec2(center.x + 7.0f * s, center.y + 7.0f * s),
            color, 4.0f * s, 0, 1.6f * s
        );
        draw->AddLine(
            ImVec2(center.x - 7.0f * s, center.y - 2.0f * s),
            ImVec2(center.x + 7.0f * s, center.y - 2.0f * s),
            color, 1.4f * s
        );
        draw->AddLine(
            ImVec2(center.x - 7.0f * s, center.y + 3.0f * s),
            ImVec2(center.x + 7.0f * s, center.y + 3.0f * s),
            color, 1.4f * s
        );
    } else if (icon == 1) {
        draw->AddTriangleFilled(
            ImVec2(center.x, center.y - 8.0f * s),
            ImVec2(center.x - 6.0f * s, center.y + 3.0f * s),
            ImVec2(center.x + 6.0f * s, center.y + 3.0f * s),
            color
        );
        draw->AddCircleFilled(
            ImVec2(center.x, center.y + 3.0f * s),
            6.0f * s, color
        );
    } else if (icon == 2) {
        draw->AddLine(
            ImVec2(center.x - 7.0f * s, center.y + 7.0f * s),
            ImVec2(center.x + 7.0f * s, center.y - 7.0f * s),
            color, 2.0f * s
        );
        draw->AddCircleFilled(
            ImVec2(center.x - 7.0f * s, center.y + 7.0f * s),
            2.2f * s, color
        );
        draw->AddCircleFilled(
            ImVec2(center.x + 7.0f * s, center.y - 7.0f * s),
            2.2f * s, color
        );
    } else if (icon == 3) {
        for (int y = -1; y <= 1; y += 2) {
            for (int x = -1; x <= 1; x += 2) {
                const ImVec2 cell{
                    center.x + static_cast<float>(x) * 4.5f * s,
                    center.y + static_cast<float>(y) * 4.5f * s
                };
                draw->AddRect(
                    ImVec2(cell.x - 3.0f * s, cell.y - 3.0f * s),
                    ImVec2(cell.x + 3.0f * s, cell.y + 3.0f * s),
                    color, 1.0f * s, 0, 1.4f * s
                );
            }
        }
    } else {
        draw->AddCircle(center, 7.0f * s, color, 16, 1.7f * s);
        draw->AddCircle(center, 2.5f * s, color, 12, 1.5f * s);
        for (int i = 0; i < 8; ++i) {
            const float angle =
                static_cast<float>(i) * 3.14159265f / 4.0f;
            const ImVec2 from{
                center.x + std::cos(angle) * 8.0f * s,
                center.y + std::sin(angle) * 8.0f * s
            };
            const ImVec2 to{
                center.x + std::cos(angle) * 10.0f * s,
                center.y + std::sin(angle) * 10.0f * s
            };
            draw->AddLine(from, to, color, 1.7f * s);
        }
    }
}

bool rail_button(
    const char* id,
    const int icon,
    const char* tooltip,
    const bool selected,
    const float scale
) {
    const float size = 40.0f * scale;
    const float rail_w = 54.0f * scale;
    ImGui::SetCursorPosX((rail_w - size) * 0.5f);
    ImGui::InvisibleButton(id, ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (selected || hovered) {
        draw->AddRectFilled(
            min,
            max,
            selected
                ? IM_COL32(63, 210, 255, 25)
                : kPanel2,
            11.0f * scale
        );
    }
    if (selected) {
        draw->AddRectFilled(
            ImVec2(min.x - 7.0f * scale, min.y + 9.0f * scale),
            ImVec2(min.x - 4.0f * scale, max.y - 9.0f * scale),
            kCyan,
            2.0f * scale
        );
    }
    draw_rail_icon(
        draw,
        icon,
        ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f),
        selected ? kCyan : (hovered ? kText : kText3),
        scale
    );
    if (hovered) {
        ImGui::SetTooltip("%s", tooltip);
    }
    return ImGui::IsItemClicked();
}

void toggle_drawer(
    gs3d::app::AnalysisRailUiState& rail,
    const gs3d::app::AnalysisDrawer drawer
) {
    rail.open_drawer =
        rail.open_drawer == drawer
            ? gs3d::app::AnalysisDrawer::kNone
            : drawer;
}

bool dark_button(
    const char* label,
    const ImVec2& size = ImVec2(0.0f, 0.0f),
    const bool emphasized = false
) {
    ImGui::PushStyleColor(
        ImGuiCol_Button,
        emphasized ? kCyan : kPanel2
    );
    ImGui::PushStyleColor(
        ImGuiCol_ButtonHovered,
        emphasized
            ? IM_COL32(92, 220, 255, 255)
            : IM_COL32(43, 51, 66, 255)
    );
    ImGui::PushStyleColor(
        ImGuiCol_ButtonActive,
        emphasized
            ? IM_COL32(35, 185, 226, 255)
            : IM_COL32(49, 59, 77, 255)
    );
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        emphasized ? IM_COL32(11, 14, 20, 255) : kText2
    );
    const bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return clicked;
}

bool switch_control(const char* id, bool* value, const float scale) {
    const float width = 30.0f * scale;
    const float height = 17.0f * scale;
    ImGui::InvisibleButton(id, ImVec2(width, height));
    if (ImGui::IsItemClicked()) {
        *value = !*value;
    }
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(
        min,
        max,
        *value ? kCyan : IM_COL32(42, 50, 66, 255),
        height * 0.5f
    );
    const float radius = height * 0.5f - 2.0f * scale;
    draw->AddCircleFilled(
        ImVec2(
            *value
                ? max.x - 2.0f * scale - radius
                : min.x + 2.0f * scale + radius,
            (min.y + max.y) * 0.5f
        ),
        radius,
        *value
            ? IM_COL32(11, 14, 20, 255)
            : IM_COL32(139, 150, 169, 255)
    );
    return ImGui::IsItemClicked();
}

bool switch_row(
    const char* id,
    const char* label,
    bool* value,
    const char* hint,
    const float scale
) {
    ImGui::TextUnformatted(label);
    const float hint_width = ImGui::CalcTextSize(hint).x;
    ImGui::SameLine(
        std::max(
            86.0f * scale,
            ImGui::GetWindowWidth() - hint_width - 76.0f * scale
        )
    );
    const bool changed = switch_control(id, value, scale);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, kText3);
    ImGui::TextUnformatted(hint);
    ImGui::PopStyleColor();
    return changed;
}

int active_view_index(const gs3d::app::AppState& state) {
    if (state.active_viewport_index >= 0 &&
        state.active_viewport_index <
            static_cast<int>(state.render_views.size())) {
        const auto& active = state.render_views[
            static_cast<std::size_t>(state.active_viewport_index)
        ];
        if (active.visible &&
            !active.detached &&
            !view_is_owned_by_workspace(state, active.viewport_index)) {
            return state.active_viewport_index;
        }
    }
    for (const auto& view : state.render_views) {
        if (view.visible &&
            !view.detached &&
            !view_is_owned_by_workspace(state, view.viewport_index)) {
            return view.viewport_index;
        }
    }
    return -1;
}

bool show_hidden_view(gs3d::app::AppState& state) {
    const auto hidden = std::find_if(
        state.render_views.begin(),
        state.render_views.end(),
        [](const auto& view) { return !view.visible; }
    );
    if (hidden == state.render_views.end()) {
        return false;
    }
    hidden->visible = true;
    hidden->detached = false;
    hidden->force_undock_next_frame = false;
    state.active_viewport_index = hidden->viewport_index;
    return true;
}

void queue_point_size(
    gs3d::app::UiActions& actions,
    const int viewport_index,
    const float value
) {
    gs3d::app::RenderSettingsCommand command;
    command.has_viewport_scope = true;
    command.viewport_indices = {viewport_index};
    command.point_size_changed = true;
    command.point_size = value;
    actions.render_settings_commands.push_back(std::move(command));
}

void queue_shape(
    gs3d::app::UiActions& actions,
    const int viewport_index,
    const int value
) {
    gs3d::app::RenderSettingsCommand command;
    command.has_viewport_scope = true;
    command.viewport_indices = {viewport_index};
    command.point_shape_changed = true;
    command.point_shape = value;
    actions.render_settings_commands.push_back(std::move(command));
}

void queue_height(
    gs3d::app::UiActions& actions,
    const int viewport_index,
    const int attr,
    const float exaggeration,
    const bool attr_changed,
    const bool exaggeration_changed
) {
    gs3d::app::RenderSettingsCommand command;
    command.has_viewport_scope = true;
    command.viewport_indices = {viewport_index};
    command.height_by_changed = attr_changed;
    command.height_by_index = attr;
    command.height_exag_changed = exaggeration_changed;
    command.height_exag = exaggeration;
    actions.render_settings_commands.push_back(std::move(command));
}

void queue_color(
    gs3d::app::UiActions& actions,
    const int viewport_index,
    const int attr,
    const int colormap,
    const bool attr_changed,
    const bool colormap_changed
) {
    gs3d::app::RenderSettingsCommand command;
    command.has_viewport_scope = true;
    command.viewport_indices = {viewport_index};
    command.color_by_changed = attr_changed;
    command.color_by_index = attr;
    command.colormap_changed = colormap_changed;
    command.colormap_index = colormap;
    actions.render_settings_commands.push_back(std::move(command));
}

void queue_clip(
    gs3d::app::UiActions& actions,
    const int viewport_index,
    const gs3d::app::RenderSettingsState& settings
) {
    gs3d::app::RenderSettingsCommand command;
    command.has_viewport_scope = true;
    command.viewport_indices = {viewport_index};
    command.value_clip_changed = true;
    command.value_clip_enabled = settings.value_clip_enabled;
    command.value_clip_min = settings.value_clip_min;
    command.value_clip_max = settings.value_clip_max;
    actions.render_settings_commands.push_back(std::move(command));
}

void draw_drawer_header(
    const char* title,
    gs3d::app::AnalysisRailUiState& rail,
    const float scale
) {
    ScopedFont font(bold_font());
    ImGui::TextUnformatted(title);
    ImGui::SameLine(
        ImGui::GetWindowWidth() - 36.0f * scale
    );
    if (dark_button("×##CloseDrawer", ImVec2(24.0f * scale, 24.0f * scale))) {
        rail.open_drawer = gs3d::app::AnalysisDrawer::kNone;
    }
    ImGui::Separator();
}

void draw_data_drawer(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    const float scale
) {
    draw_drawer_header("数据", state.analysis_rail_ui, scale);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanel2);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f * scale);
    if (ImGui::BeginChild(
            "##AnalysisDatasetCard",
            ImVec2(0.0f, 88.0f * scale),
            ImGuiChildFlags_Borders
        )) {
        ScopedFont font(bold_font());
        ImGui::TextWrapped(
            "%s",
            state.dataset.active_dataset.empty()
                ? "未加载数据"
                : state.dataset.active_dataset.c_str()
        );
        ImGui::PushStyleColor(ImGuiCol_Text, kText3);
        ImGui::Text(
            "%llu 点 · %s",
            static_cast<unsigned long long>(state.dataset.point_count),
            state.dataset.file_size.c_str()
        );
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Text, kCyan);
        ImGui::TextUnformatted(
            state.dataset.format.empty()
                ? "GS3D"
                : state.dataset.format.c_str()
        );
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    static char filter[96]{};
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint(
        "##AnalysisFilter",
        "筛选场景树…",
        filter,
        sizeof(filter)
    );

    section_label("场景树", scale);
    ImGui::TextUnformatted("▥  数据概览");
    ImGui::TextUnformatted("▦  瓦片");
    ImGui::SameLine(ImGui::GetWindowWidth() - 132.0f * scale);
    ImGui::PushStyleColor(ImGuiCol_Text, kText3);
    ImGui::Text(
        "%u 已加载 · %u 等待",
        state.performance.loaded_tiles,
        state.performance.pending_tiles
    );
    ImGui::PopStyleColor();
    ImGui::Indent(20.0f * scale);
    ImGui::PushStyleColor(ImGuiCol_Text, kGreen);
    ImGui::BulletText("已加载  %u", state.performance.loaded_tiles);
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_Text, kAmber);
    ImGui::BulletText("等待  %u", state.performance.pending_tiles);
    ImGui::PopStyleColor();
    ImGui::Unindent(20.0f * scale);
    ImGui::Text("≋  细节层级");
    ImGui::SameLine(ImGui::GetWindowWidth() - 68.0f * scale);
    ImGui::PushStyleColor(ImGuiCol_Text, kText3);
    ImGui::TextUnformatted("5 层");
    ImGui::PopStyleColor();
    ImGui::Text("◇  属性");
    ImGui::SameLine(ImGui::GetWindowWidth() - 68.0f * scale);
    ImGui::PushStyleColor(ImGuiCol_Text, kText3);
    ImGui::Text("%zu 项", state.dataset.attributes.size());
    ImGui::PopStyleColor();
    ImGui::Indent(20.0f * scale);
    for (std::size_t i = 0; i < state.dataset.attributes.size(); ++i) {
        ImGui::BulletText("%s", state.dataset.attributes[i].c_str());
    }
    ImGui::Unindent(20.0f * scale);

    section_label("文件信息", scale);
    key_value("路径", state.dataset.path);
    key_value("格式", state.dataset.format);
    key_value("包围盒", state.dataset.bounding_box);

    section_label("文件操作", scale);
    if (dark_button("打开数据文件…   Ctrl+O", ImVec2(-1.0f, 0.0f))) {
        actions.open_requested = true;
    }
    if (dark_button("打开 GS3D Bundle 项目…", ImVec2(-1.0f, 0.0f))) {
        actions.open_bundle_requested = true;
    }
    if (dark_button("返回欢迎页", ImVec2(-1.0f, 0.0f))) {
        actions.show_welcome_requested = true;
    }
}

void draw_appearance_drawer(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    const int viewport_index,
    const float scale
) {
    draw_drawer_header("外观", state.analysis_rail_ui, scale);
    auto& settings =
        gs3d::app::render_settings_for_view(state, viewport_index);

    section_label("点渲染", scale);
    float point_size = settings.point_size;
    ImGui::SetNextItemWidth(-58.0f * scale);
    if (ImGui::SliderFloat(
            "##AnalysisPointSize",
            &point_size,
            1.0f,
            10.0f,
            "%.1f"
        )) {
        settings.point_size = point_size;
        queue_point_size(actions, viewport_index, point_size);
    }
    ImGui::SameLine();
    ImGui::Text("%.1f", static_cast<double>(settings.point_size));

    static constexpr std::array<const char*, 4> kShapes{
        "方形", "圆形", "菱形", "三角形"
    };
    if (ImGui::BeginCombo(
            "形状",
            kShapes[static_cast<std::size_t>(
                std::clamp(settings.point_shape, 0, 3)
            )]
        )) {
        for (int i = 0; i < static_cast<int>(kShapes.size()); ++i) {
            if (ImGui::Selectable(
                    kShapes[static_cast<std::size_t>(i)],
                    settings.point_shape == i
                )) {
                settings.point_shape = i;
                queue_shape(actions, viewport_index, i);
            }
        }
        ImGui::EndCombo();
    }

    section_label("高度", scale);
    const char* height_name =
        settings.height_by_options.empty()
            ? "未设置"
            : settings.height_by_options[
                static_cast<std::size_t>(std::clamp(
                    settings.height_attr_index,
                    0,
                    static_cast<int>(settings.height_by_options.size()) - 1
                ))
            ].c_str();
    if (ImGui::BeginCombo("高度来源", height_name)) {
        for (int i = 0;
             i < static_cast<int>(settings.height_by_options.size());
             ++i) {
            if (ImGui::Selectable(
                    settings.height_by_options[
                        static_cast<std::size_t>(i)
                    ].c_str(),
                    settings.height_attr_index == i
                )) {
                settings.height_attr_index = i;
                queue_height(
                    actions,
                    viewport_index,
                    i,
                    settings.height_exaggeration,
                    true,
                    false
                );
            }
        }
        ImGui::EndCombo();
    }
    float exaggeration = settings.height_exaggeration;
    if (ImGui::SliderFloat(
            "高度缩放",
            &exaggeration,
            0.01f,
            5.0f,
            "%.2fx"
        )) {
        settings.height_exaggeration = exaggeration;
        queue_height(
            actions,
            viewport_index,
            settings.height_attr_index,
            exaggeration,
            false,
            true
        );
    }

    section_label("着色", scale);
    const char* color_name =
        settings.color_by_options.empty()
            ? "未设置"
            : settings.color_by_options[
                static_cast<std::size_t>(std::clamp(
                    settings.color_attr_index,
                    0,
                    static_cast<int>(settings.color_by_options.size()) - 1
                ))
            ].c_str();
    if (ImGui::BeginCombo("着色字段", color_name)) {
        for (int i = 0;
             i < static_cast<int>(settings.color_by_options.size());
             ++i) {
            if (ImGui::Selectable(
                    settings.color_by_options[
                        static_cast<std::size_t>(i)
                    ].c_str(),
                    settings.color_attr_index == i
                )) {
                settings.color_attr_index = i;
                queue_color(
                    actions,
                    viewport_index,
                    i,
                    settings.colormap_index,
                    true,
                    false
                );
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::BeginCombo(
            "色标",
            colormap_display_name(settings.colormap_index)
        )) {
        for (int i = 0; i < kColormapCount; ++i) {
            if (ImGui::Selectable(
                    colormap_display_name(i),
                    settings.colormap_index == i
                )) {
                settings.colormap_index = i;
                queue_color(
                    actions,
                    viewport_index,
                    settings.color_attr_index,
                    i,
                    false,
                    true
                );
            }
        }
        ImGui::EndCombo();
    }
    const ImVec2 bar_min = ImGui::GetCursorScreenPos();
    const ImVec2 bar_max{
        bar_min.x + ImGui::GetContentRegionAvail().x,
        bar_min.y + 9.0f * scale
    };
    draw_colormap_preview_bar(
        ImGui::GetWindowDrawList(),
        bar_min,
        bar_max,
        settings.colormap_index
    );
    ImGui::Dummy(ImVec2(0.0f, 12.0f * scale));
    ImGui::PushStyleColor(ImGuiCol_Text, kText3);
    ImGui::Text(
        "%.1f      数据范围      %.1f",
        static_cast<double>(settings.data_value_min),
        static_cast<double>(settings.data_value_max)
    );
    ImGui::PopStyleColor();

    section_label("值域裁切", scale);
    bool clipping = settings.value_clip_enabled;
    if (switch_row(
            "##AnalysisClip",
            "值域裁切",
            &clipping,
            "默认关",
            scale
        )) {
        settings.value_clip_enabled = clipping;
        if (clipping &&
            settings.value_clip_min >= settings.value_clip_max) {
            settings.value_clip_min = settings.data_value_min;
            settings.value_clip_max = settings.data_value_max;
        }
        queue_clip(actions, viewport_index, settings);
    }
    ImGui::BeginDisabled(!settings.value_clip_enabled);
    bool clip_changed = false;
    ImGui::SetNextItemWidth(
        (ImGui::GetContentRegionAvail().x - 8.0f * scale) * 0.5f
    );
    clip_changed |= ImGui::InputFloat(
        "##AnalysisClipMin",
        &settings.value_clip_min,
        0.0f,
        0.0f,
        "%.1f"
    );
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    clip_changed |= ImGui::InputFloat(
        "##AnalysisClipMax",
        &settings.value_clip_max,
        0.0f,
        0.0f,
        "%.1f"
    );
    ImGui::EndDisabled();
    if (clip_changed) {
        queue_clip(actions, viewport_index, settings);
    }
    muted_text("Tab 循环着色字段 · Shift+Tab 循环高度字段");
}

void draw_measure_drawer(
    gs3d::app::AppState& state,
    const int viewport_index,
    const float scale
) {
    draw_drawer_header("测量", state.analysis_rail_ui, scale);
    auto& measurement =
        gs3d::app::measurement_for_view(state, viewport_index);
    bool enabled = measurement.measure_mode_active();
    if (switch_row(
            "##AnalysisMeasureMode",
            "测量模式",
            &enabled,
            "M · 视口黄框",
            scale
        )) {
        measurement.set_measure_mode(enabled);
        if (!enabled) {
            measurement.clear_pending();
        }
    }
    if (measurement.has_pending()) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(69, 54, 13, 120));
        if (ImGui::BeginChild(
                "##AnalysisMeasurePending",
                ImVec2(0.0f, 34.0f * scale),
                ImGuiChildFlags_Borders
            )) {
            ImGui::PushStyleColor(ImGuiCol_Text, kAmber);
            ImGui::TextUnformatted("●  等待第二个点…");
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    section_label("工具", scale);
    const float gap = 7.0f * scale;
    const float button_width =
        (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
    dark_button("测距\n中键取点", ImVec2(button_width, 48.0f * scale), true);
    ImGui::SameLine(0.0f, gap);
    dark_button(
        "区域统计\nShift+框选",
        ImVec2(button_width, 48.0f * scale),
        true
    );
    ImGui::BeginDisabled();
    for (int row = 0; row < 3; ++row) {
        const std::array<const char*, 6> labels{
            "面积 · 规划中", "高差 · 规划中",
            "剖面 · 规划中", "角度 · 规划中",
            "标注 · 规划中", "通视 · 规划中"
        };
        dark_button(
            labels[static_cast<std::size_t>(row * 2)],
            ImVec2(button_width, 32.0f * scale)
        );
        ImGui::SameLine(0.0f, gap);
        dark_button(
            labels[static_cast<std::size_t>(row * 2 + 1)],
            ImVec2(button_width, 32.0f * scale)
        );
    }
    ImGui::EndDisabled();

    section_label("距离显示", scale);
    const std::array<const char*, 3> distance_labels{
        "三维", "平面", "都显示"
    };
    const float segment_width =
        (ImGui::GetContentRegionAvail().x - gap * 2.0f) / 3.0f;
    for (int i = 0; i < 3; ++i) {
        if (i > 0) {
            ImGui::SameLine(0.0f, gap);
        }
        if (dark_button(
                distance_labels[static_cast<std::size_t>(i)],
                ImVec2(segment_width, 0.0f),
                static_cast<int>(measurement.display_mode()) == i
            )) {
            measurement.set_display_mode(
                static_cast<gs3d::app::DistanceDisplayMode>(i)
            );
        }
    }

    section_label("测量记录", scale);
    for (std::size_t i = 0; i < measurement.lines().size();) {
        auto& line = measurement.lines()[i];
        ImGui::PushID(static_cast<int>(i));
        const ImU32 line_color = line.color;
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##LineColor", ImVec2(13.0f * scale, 13.0f * scale));
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImGui::GetItemRectMin(),
            ImGui::GetItemRectMax(),
            line_color,
            3.0f * scale
        );
        ImGui::SameLine();
        ImGui::Text(
            "#%zu · %s",
            i + 1,
            line.distance_label(measurement.display_mode()).c_str()
        );
        ImGui::SameLine(ImGui::GetWindowWidth() - 64.0f * scale);
        if (ImGui::SmallButton(line.fixed ? "●" : "○")) {
            measurement.toggle_fixed(i);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("×")) {
            measurement.remove_line(i);
            ImGui::PopID();
            continue;
        }
        (void)cursor;
        ImGui::PopID();
        ++i;
    }
    if (measurement.lines().empty()) {
        muted_text("暂无测量线");
    }
    if (dark_button(
            "全部删除（保留固定）",
            ImVec2(-1.0f, 0.0f)
        )) {
        measurement.remove_all_unfixed();
    }

    section_label("区域统计", scale);
    const auto& stats =
        gs3d::app::region_stats_for_view(state, viewport_index);
    if (stats.computing) {
        muted_text("统计中…");
    } else if (!stats.valid) {
        muted_text("在测量模式下 Shift+左键框选区域");
    } else {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanel2);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f * scale);
        if (ImGui::BeginChild(
                "##AnalysisStatsCard",
                ImVec2(0.0f, 142.0f * scale),
                ImGuiChildFlags_Borders
            )) {
            ImGui::PushStyleColor(ImGuiCol_Text, kTeal);
            ImGui::Text(
                "框内点数  %llu",
                static_cast<unsigned long long>(stats.point_count)
            );
            ImGui::PopStyleColor();
            ImGui::Text(
                "X  %.3f → %.3f",
                stats.world_x_min,
                stats.world_x_max
            );
            ImGui::Text(
                "Y  %.3f → %.3f",
                stats.world_y_min,
                stats.world_y_max
            );
            ImGui::Text(
                "%s avg  %.2f",
                stats.primary_label.c_str(),
                static_cast<double>(stats.fold_avg)
            );
            ImGui::Text(
                "%s avg  %.2f",
                stats.secondary_label.c_str(),
                static_cast<double>(stats.elev_avg)
            );
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
}

void draw_views_drawer(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    const int viewport_index,
    const float scale
) {
    draw_drawer_header("视图", state.analysis_rail_ui, scale);
    section_label("多视口 · Ctrl+N · 最多 4", scale);
    const float gap = 8.0f * scale;
    const float cell =
        (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
    for (int i = 0; i < 4; ++i) {
        if ((i % 2) != 0) {
            ImGui::SameLine(0.0f, gap);
        }
        const bool exists =
            i < static_cast<int>(state.render_views.size());
        const bool visible =
            exists && state.render_views[static_cast<std::size_t>(i)].visible;
        const std::string label = visible
            ? "视图 " + std::to_string(i + 1) +
                (i == viewport_index ? "\n当前" : "")
            : (exists ? "＋ 新视口" : "空槽位");
        if (dark_button(
                (label + "##AnalysisView" + std::to_string(i)).c_str(),
                ImVec2(cell, 66.0f * scale),
                i == viewport_index
            ) &&
            exists) {
            state.render_views[static_cast<std::size_t>(i)].visible = true;
            state.active_viewport_index = i;
        }
    }
    if (dark_button("添加视图", ImVec2(-1.0f, 0.0f))) {
        show_hidden_view(state);
    }

    auto& view =
        state.render_views[static_cast<std::size_t>(viewport_index)];
    section_label("视口开关", scale);
    switch_row(
        "##AnalysisLinked",
        "联动相机",
        &view.camera_linked,
        "同步组 A",
        scale
    );
    bool map_axis = view.show_map_axis;
    if (switch_row(
            "##AnalysisMapAxis",
            "地图轴",
            &map_axis,
            "与世界轴互斥",
            scale
        )) {
        view.show_map_axis = map_axis;
        if (map_axis) {
            view.show_world_axis = false;
        }
    }
    bool world_axis = view.show_world_axis;
    if (switch_row(
            "##AnalysisWorldAxis",
            "世界轴",
            &world_axis,
            "QGIS 风",
            scale
        )) {
        view.show_world_axis = world_axis;
        if (world_axis) {
            view.show_map_axis = false;
            view.show_crosshair = false;
        }
    }
    bool crosshair = view.show_crosshair;
    if (switch_row(
            "##AnalysisCrosshair",
            "十字准线",
            &crosshair,
            "连带地图轴",
            scale
        )) {
        view.show_crosshair = crosshair;
        if (crosshair) {
            view.show_map_axis = true;
            view.show_world_axis = false;
        }
    }
    section_label("交互", scale);
    muted_text(
        "左键旋转 · 右键平移 · 滚轮缩放\n"
        "Ctrl+左键框选 · Shift+左键统计\n"
        "双击设旋转中心 · F 聚焦"
    );
    if (dark_button("复位视角   R", ImVec2(-1.0f, 0.0f))) {
        actions.reset_camera_index = viewport_index;
    }
}

void draw_system_drawer(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    AnalysisRailFrameResult& result,
    const float scale
) {
    draw_drawer_header("系统", state.analysis_rail_ui, scale);
    section_label("性能", scale);
    key_value("FPS", std::to_string(
        static_cast<int>(std::round(state.performance.fps))
    ));
    char frame_time[32]{};
    std::snprintf(
        frame_time,
        sizeof(frame_time),
        "%.2f ms",
        static_cast<double>(state.performance.frame_time_ms)
    );
    key_value("帧耗时", frame_time);
    key_value(
        "可见点",
        std::to_string(state.performance.visible_points)
    );
    char gpu_memory[32]{};
    std::snprintf(
        gpu_memory,
        sizeof(gpu_memory),
        "%.1f MB",
        static_cast<double>(
            bytes_to_mb(state.performance.gpu_memory_bytes)
        )
    );
    key_value("GPU 显存", gpu_memory);
    key_value(
        "瓦片",
        std::to_string(state.performance.loaded_tiles) +
            " 已加载 · " +
            std::to_string(state.performance.pending_tiles) +
            " 等待"
    );
    if (dark_button("清空缓存", ImVec2(-1.0f, 0.0f))) {
        actions.clear_cache_requested = true;
    }

    section_label("主题", scale);
    for (int i = 0; i < kThemeCount; ++i) {
        const auto id = static_cast<ThemeId>(i);
        if (dark_button(
                theme_tokens(id).name,
                ImVec2(-1.0f, 0.0f),
                active_theme() == id
            ) &&
            id != active_theme()) {
            result.theme_change_requested = true;
            result.requested_theme = id;
        }
    }

    section_label("布局", scale);
    if (dark_button("方案 A · 专业工作台", ImVec2(-1.0f, 0.0f))) {
        state.ui_layout_mode = gs3d::app::UiLayoutMode::kWorkbench;
        result.theme_change_requested = true;
        result.requested_theme = ThemeId::kCarbonBlue;
    }
    if (dark_button("方案 B · 悬浮 Dock", ImVec2(-1.0f, 0.0f))) {
        state.ui_layout_mode = gs3d::app::UiLayoutMode::kFloatingDock;
        result.theme_change_requested = true;
        result.requested_theme = ThemeId::kCarbonBlue;
    }

    section_label("工作区", scale);
    if (dark_button("恢复默认工作区", ImVec2(-1.0f, 0.0f))) {
        restore_default_workspace(state);
        actions.restore_default_workspace_requested = true;
    }
    ImGui::BeginDisabled(!has_hidden_view(state));
    if (dark_button("新建工作窗口", ImVec2(-1.0f, 0.0f))) {
        create_workspace_window(state);
    }
    ImGui::EndDisabled();

    section_label("日志", scale);
    for (const auto& line : state.debug_log.lines) {
        muted_text(line.c_str());
    }
    section_label("快捷键", scale);
    muted_text(
        "R 复位 · F 聚焦 · M 测量 · C 复制\n"
        "Tab 切换着色 · Shift+Tab 切换高度\n"
        "Ctrl+O 打开 · Ctrl+N 新建视图 · Esc 收起抽屉"
    );
}

void draw_rail(
    gs3d::app::AppState& state,
    const float scale
) {
    auto& rail = state.analysis_rail_ui;
    const float rail_width = 54.0f * scale;
    const float logo = 34.0f * scale;
    ImGui::SetCursorPos(ImVec2(
        (rail_width - logo) * 0.5f,
        10.0f * scale
    ));
    ImGui::InvisibleButton("##AnalysisLogo", ImVec2(logo, logo));
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilledMultiColor(
        min,
        max,
        IM_COL32(63, 210, 255, 255),
        IM_COL32(124, 92, 255, 255),
        IM_COL32(124, 92, 255, 255),
        IM_COL32(63, 210, 255, 255)
    );
    const ImVec2 logo_text = ImGui::CalcTextSize("G3");
    draw->AddText(
        ImVec2(
            (min.x + max.x - logo_text.x) * 0.5f,
            (min.y + max.y - logo_text.y) * 0.5f
        ),
        IM_COL32_WHITE,
        "G3"
    );

    ImGui::SetCursorPosY(54.0f * scale);
    const std::array<gs3d::app::AnalysisDrawer, 5> drawers{
        gs3d::app::AnalysisDrawer::kData,
        gs3d::app::AnalysisDrawer::kAppearance,
        gs3d::app::AnalysisDrawer::kMeasure,
        gs3d::app::AnalysisDrawer::kViews,
        gs3d::app::AnalysisDrawer::kSystem
    };
    const std::array<const char*, 5> tips{
        "数据 · 数据集与文件",
        "外观 · 着色与渲染",
        "测量 · 区域统计",
        "视图 · 多视口与准星",
        "系统 · 性能与设置"
    };
    for (std::size_t i = 0; i < drawers.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (rail_button(
                "##RailItem",
                static_cast<int>(i),
                tips[i],
                rail.open_drawer == drawers[i],
                scale
            )) {
            toggle_drawer(rail, drawers[i]);
        }
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));
    }
    ImGui::SetCursorPos(ImVec2(
        (rail_width - 30.0f * scale) * 0.5f,
        ImGui::GetWindowHeight() - 42.0f * scale
    ));
    ImGui::InvisibleButton(
        "##AnalysisAvatar",
        ImVec2(30.0f * scale, 30.0f * scale)
    );
    const ImVec2 avatar_min = ImGui::GetItemRectMin();
    const ImVec2 avatar_max = ImGui::GetItemRectMax();
    draw->AddCircleFilled(
        ImVec2(
            (avatar_min.x + avatar_max.x) * 0.5f,
            (avatar_min.y + avatar_max.y) * 0.5f
        ),
        15.0f * scale,
        IM_COL32(255, 104, 126, 255)
    );
    const ImVec2 avatar_text = ImGui::CalcTextSize("我");
    draw->AddText(
        ImVec2(
            (avatar_min.x + avatar_max.x - avatar_text.x) * 0.5f,
            (avatar_min.y + avatar_max.y - avatar_text.y) * 0.5f
        ),
        IM_COL32_WHITE,
        "我"
    );
}

void draw_topbar(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    const int viewport_index,
    const float scale
) {
    ImGui::SetCursorPos(ImVec2(16.0f * scale, 16.0f * scale));
    ImGui::PushStyleColor(ImGuiCol_Text, kText3);
    ImGui::TextUnformatted("项目 / 测区 2026-07 /");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    {
        ScopedFont font(bold_font());
        ImGui::TextUnformatted(
            state.dataset.active_dataset.empty()
                ? "未加载数据"
                : state.dataset.active_dataset.c_str()
        );
    }

    const float steps_x = std::max(
        340.0f * scale,
        ImGui::GetWindowWidth() * 0.36f
    );
    ImGui::SetCursorPos(ImVec2(steps_x, 16.0f * scale));
    ImGui::PushStyleColor(ImGuiCol_Text, kGreen);
    ImGui::TextUnformatted("✓ 导入");
    ImGui::SameLine();
    ImGui::TextUnformatted("—  ✓ 着色");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, kCyan);
    ImGui::TextUnformatted("—  ③ 测量");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, kText3);
    ImGui::TextUnformatted("—  ④ 分析  —  ⑤ 导出");
    ImGui::PopStyleColor();

    const float button_y = 10.0f * scale;
    const float total_buttons = 344.0f * scale;
    ImGui::SetCursorPos(ImVec2(
        std::max(0.0f, ImGui::GetWindowWidth() - total_buttons),
        button_y
    ));
    if (dark_button("＋ 添加视图", ImVec2(92.0f * scale, 30.0f * scale))) {
        show_hidden_view(state);
    }
    ImGui::SameLine();
    if (dark_button("截图", ImVec2(60.0f * scale, 30.0f * scale))) {
        actions.screenshot_requested = true;
    }
    ImGui::SameLine();
    auto& measurement =
        gs3d::app::measurement_for_view(state, viewport_index);
    if (dark_button(
            "测量",
            ImVec2(60.0f * scale, 30.0f * scale),
            measurement.measure_mode_active()
        )) {
        measurement.toggle_measure_mode();
        if (!measurement.measure_mode_active()) {
            measurement.clear_pending();
        }
    }
    ImGui::SameLine();
    if (dark_button("复位", ImVec2(60.0f * scale, 30.0f * scale))) {
        actions.reset_camera_index = viewport_index;
    }
}

bool card_header(
    const char* label,
    bool* open,
    const float scale
) {
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::InvisibleButton(
        (std::string("##CardHeader") + label).c_str(),
        ImVec2(width, 22.0f * scale)
    );
    const ImVec2 min = ImGui::GetItemRectMin();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddText(min, kText2, label);
    const char* arrow = *open ? "▾" : "‹";
    const ImVec2 arrow_size = ImGui::CalcTextSize(arrow);
    draw->AddText(
        ImVec2(min.x + width - arrow_size.x, min.y),
        kText3,
        arrow
    );
    if (ImGui::IsItemClicked()) {
        *open = !*open;
    }
    return *open;
}

void draw_navigation_preview(
    const gs3d::app::NavigationMapState& map,
    const float size
) {
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const auto layout = compute_navigation_preview_layout(
        cursor.x,
        cursor.y,
        size,
        map.tex_w,
        map.tex_h
    );
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(
        ImVec2(layout.container.x, layout.container.y),
        ImVec2(
            layout.container.x + layout.container.width,
            layout.container.y + layout.container.height
        ),
        IM_COL32(16, 19, 26, 255),
        8.0f
    );
    if (map.valid && map.texture_descriptor != VK_NULL_HANDLE) {
        draw->AddImage(
            static_cast<ImTextureID>(
                reinterpret_cast<ImU64>(map.texture_descriptor)
            ),
            ImVec2(layout.image.x, layout.image.y),
            ImVec2(
                layout.image.x + layout.image.width,
                layout.image.y + layout.image.height
            )
        );
        if (map.view_rect_valid &&
            map.tex_w > 0.0f &&
            map.tex_h > 0.0f) {
            const float sx = layout.image.width / map.tex_w;
            const float sy = layout.image.height / map.tex_h;
            draw->AddRect(
                ImVec2(
                    layout.image.x + map.view_rect_min_x * sx,
                    layout.image.y + map.view_rect_min_y * sy
                ),
                ImVec2(
                    layout.image.x + map.view_rect_max_x * sx,
                    layout.image.y + map.view_rect_max_y * sy
                ),
                kRed,
                2.0f,
                0,
                1.5f
            );
        }
    } else {
        const char* empty = "导航图准备中…";
        const ImVec2 text = ImGui::CalcTextSize(empty);
        draw->AddText(
            ImVec2(
                cursor.x + (size - text.x) * 0.5f,
                cursor.y + (size - text.y) * 0.5f
            ),
            kText3,
            empty
        );
    }
    ImGui::Dummy(ImVec2(size, size));
}

void begin_card(const char* id, const float height, const float scale) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanel);
    ImGui::PushStyleColor(ImGuiCol_Border, kLine);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14.0f * scale);
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(13.0f * scale, 12.0f * scale)
    );
    ImGui::BeginChild(
        id,
        ImVec2(0.0f, height),
        ImGuiChildFlags_Borders
    );
}

void end_card() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

void draw_cards(
    gs3d::app::AppState& state,
    const int viewport_index,
    const float scale
) {
    auto& rail = state.analysis_rail_ui;
    auto& map =
        gs3d::app::navigation_map_for_view(state, viewport_index);
    auto& settings =
        gs3d::app::render_settings_for_view(state, viewport_index);
    const auto& stats =
        gs3d::app::region_stats_for_view(state, viewport_index);
    const float width = ImGui::GetContentRegionAvail().x;
    const float nav_size = std::max(48.0f, width - 28.0f * scale);

    begin_card(
        "##AnalysisNavCard",
        rail.navigation_card_open
            ? nav_size + 62.0f * scale
            : 46.0f * scale,
        scale
    );
    if (card_header("导航图", &rail.navigation_card_open, scale)) {
        draw_navigation_preview(map, nav_size);
        ImGui::PushStyleColor(ImGuiCol_Text, kText3);
        ImGui::TextUnformatted("正交俯视 · 红框 = 视野");
        ImGui::PopStyleColor();
    }
    end_card();
    ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));

    begin_card(
        "##AnalysisColorCard",
        rail.color_card_open ? 132.0f * scale : 46.0f * scale,
        scale
    );
    if (card_header("当前着色", &rail.color_card_open, scale)) {
        const char* name =
            settings.color_by_options.empty()
                ? "未设置"
                : settings.color_by_options[
                    static_cast<std::size_t>(std::clamp(
                        settings.color_attr_index,
                        0,
                        static_cast<int>(settings.color_by_options.size()) - 1
                    ))
                ].c_str();
        ImGui::TextUnformatted(name);
        const ImVec2 bar_min = ImGui::GetCursorScreenPos();
        const ImVec2 bar_max{
            bar_min.x + ImGui::GetContentRegionAvail().x,
            bar_min.y + 9.0f * scale
        };
        draw_colormap_preview_bar(
            ImGui::GetWindowDrawList(),
            bar_min,
            bar_max,
            settings.colormap_index
        );
        ImGui::Dummy(ImVec2(0.0f, 13.0f * scale));
        ImGui::PushStyleColor(ImGuiCol_Text, kText3);
        ImGui::Text(
            "%.1f       %s       %.1f",
            static_cast<double>(settings.data_value_min),
            colormap_display_name(settings.colormap_index),
            static_cast<double>(settings.data_value_max)
        );
        ImGui::PopStyleColor();
    }
    end_card();
    ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));

    begin_card(
        "##AnalysisPerfCard",
        rail.performance_card_open ? 132.0f * scale : 46.0f * scale,
        scale
    );
    if (card_header("性能", &rail.performance_card_open, scale)) {
        char value[48]{};
        std::snprintf(
            value,
            sizeof(value),
            "%.1f",
            static_cast<double>(state.performance.fps)
        );
        key_value("FPS", value);
        std::snprintf(
            value,
            sizeof(value),
            "%.2f ms",
            static_cast<double>(state.performance.frame_time_ms)
        );
        key_value("帧耗时", value);
        std::snprintf(
            value,
            sizeof(value),
            "%.1f MB",
            static_cast<double>(
                bytes_to_mb(state.performance.gpu_memory_bytes)
            )
        );
        key_value("GPU 显存", value);
    }
    end_card();
    ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));

    begin_card(
        "##AnalysisStatsSummary",
        rail.stats_card_open ? 126.0f * scale : 46.0f * scale,
        scale
    );
    if (card_header("区域统计", &rail.stats_card_open, scale)) {
        if (stats.valid) {
            ImGui::PushStyleColor(ImGuiCol_Text, kTeal);
            ImGui::Text(
                "框内点数  %llu",
                static_cast<unsigned long long>(stats.point_count)
            );
            ImGui::PopStyleColor();
            key_value(
                stats.primary_label.empty()
                    ? "主属性 avg"
                    : (stats.primary_label + " avg").c_str(),
                std::to_string(stats.fold_avg)
            );
        } else {
            muted_text("Shift+左键框选后显示统计");
        }
        ImGui::PushStyleColor(ImGuiCol_Text, kCyan);
        if (ImGui::Selectable("打开测量抽屉查看详情 →")) {
            rail.open_drawer =
                gs3d::app::AnalysisDrawer::kMeasure;
        }
        ImGui::PopStyleColor();
    }
    end_card();
}

void draw_status_bar(
    const gs3d::app::AppState& state,
    const float scale
) {
    ScopedFont font(status_font());
    ImGui::SetCursorPos(ImVec2(14.0f * scale, 5.0f * scale));
    ImGui::PushStyleColor(ImGuiCol_Text, kGreen);
    ImGui::Text("● %s", state.status_bar.ready_state.c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, kText3);
    ImGui::Text(
        "%.1f FPS    %llu 点    GPU %.1f MB",
        static_cast<double>(state.status_bar.fps),
        static_cast<unsigned long long>(
            state.status_bar.visible_points
        ),
        static_cast<double>(
            bytes_to_mb(state.status_bar.gpu_memory_bytes)
        )
    );
    const std::string right =
        state.status_bar.crs + "    " +
        std::to_string(state.status_bar.pending_tiles) +
        " 等待";
    const float width = ImGui::CalcTextSize(right.c_str()).x;
    ImGui::SameLine(
        std::max(
            ImGui::GetCursorPosX(),
            ImGui::GetWindowWidth() - width - 14.0f * scale
        )
    );
    ImGui::TextUnformatted(right.c_str());
    ImGui::PopStyleColor();
}

} // namespace

AnalysisRailFrameResult draw_analysis_rail_layout(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    const float ui_scale
) {
    AnalysisRailFrameResult result;
    const float scale = std::max(0.5f, ui_scale);
    auto& rail = state.analysis_rail_ui;
    const float dt = std::min(ImGui::GetIO().DeltaTime, 0.05f);
    const float target =
        rail.open_drawer == gs3d::app::AnalysisDrawer::kNone
            ? 0.0f
            : 1.0f;
    const float delta = dt * 7.5f;
    rail.drawer_anim += std::clamp(
        target - rail.drawer_anim,
        -delta,
        delta
    );
    rail.drawer_anim = std::clamp(rail.drawer_anim, 0.0f, 1.0f);

    if (rail.open_drawer != gs3d::app::AnalysisDrawer::kNone &&
        !ImGui::GetIO().WantTextInput &&
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        rail.open_drawer = gs3d::app::AnalysisDrawer::kNone;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const auto layout = compute_analysis_rail_layout(
        viewport->WorkSize.x,
        viewport->WorkSize.y,
        scale,
        rail.drawer_anim
    );
    const int view_index = active_view_index(state);

    const ImGuiWindowFlags host_flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoDocking;
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f * scale);
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        ImVec2(9.0f * scale, 6.0f * scale)
    );
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(7.0f * scale, 6.0f * scale)
    );
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kBg);
    ImGui::PushStyleColor(ImGuiCol_Text, kText);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, kText3);
    ImGui::PushStyleColor(ImGuiCol_Border, kLine);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, kPanel2);
    ImGui::PushStyleColor(
        ImGuiCol_FrameBgHovered,
        IM_COL32(43, 51, 66, 255)
    );
    ImGui::PushStyleColor(
        ImGuiCol_FrameBgActive,
        IM_COL32(49, 59, 77, 255)
    );
    ImGui::PushStyleColor(ImGuiCol_CheckMark, kCyan);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, kCyan);
    ImGui::PushStyleColor(
        ImGuiCol_SliderGrabActive,
        IM_COL32(92, 220, 255, 255)
    );

    if (ImGui::Begin(
            "GeoScatter3D 暗色分析舱###AnalysisRailHost",
            nullptr,
            host_flags
        )) {
        if (begin_region(
                "##AnalysisRail",
                ImVec2(0.0f, 0.0f),
                ImVec2(layout.rail_width, viewport->WorkSize.y),
                kPanel
            )) {
            draw_rail(state, scale);
        }
        ImGui::EndChild();

        if (layout.drawer_width > 1.0f) {
            ImGui::PushStyleVar(
                ImGuiStyleVar_WindowPadding,
                ImVec2(16.0f * scale, 14.0f * scale)
            );
            if (begin_region(
                    "##AnalysisDrawer",
                    ImVec2(layout.rail_width, 0.0f),
                    ImVec2(layout.drawer_width, viewport->WorkSize.y),
                    kPanel,
                    ImGuiWindowFlags_AlwaysVerticalScrollbar
                )) {
                if (rail.drawer_anim > 0.96f && view_index >= 0) {
                    switch (rail.open_drawer) {
                    case gs3d::app::AnalysisDrawer::kData:
                        draw_data_drawer(state, actions, scale);
                        break;
                    case gs3d::app::AnalysisDrawer::kAppearance:
                        draw_appearance_drawer(
                            state,
                            actions,
                            view_index,
                            scale
                        );
                        break;
                    case gs3d::app::AnalysisDrawer::kMeasure:
                        draw_measure_drawer(state, view_index, scale);
                        break;
                    case gs3d::app::AnalysisDrawer::kViews:
                        draw_views_drawer(
                            state,
                            actions,
                            view_index,
                            scale
                        );
                        break;
                    case gs3d::app::AnalysisDrawer::kSystem:
                        draw_system_drawer(
                            state,
                            actions,
                            result,
                            scale
                        );
                        break;
                    default:
                        break;
                    }
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
        }

        if (begin_region(
                "##AnalysisTopbar",
                ImVec2(layout.center_x, 0.0f),
                ImVec2(
                    viewport->WorkSize.x - layout.center_x,
                    layout.topbar_height
                ),
                kPanel
            )) {
            if (view_index >= 0) {
                draw_topbar(
                    state,
                    actions,
                    view_index,
                    scale
                );
            }
        }
        ImGui::EndChild();

        if (begin_region(
                "##AnalysisViewport",
                ImVec2(layout.viewport_x, layout.viewport_y),
                ImVec2(
                    layout.viewport_width,
                    layout.viewport_height
                ),
                kBg
            )) {
            if (view_index >= 0) {
                ViewportCanvasOptions options;
                options.workspace_id = 0;
                options.show_info_badge = true;
                draw_viewport_canvas(
                    state.render_views[
                        static_cast<std::size_t>(view_index)
                    ],
                    actions,
                    options
                );
            }
        }
        ImGui::EndChild();

        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowPadding,
            ImVec2(12.0f * scale, 12.0f * scale)
        );
        if (begin_region(
                "##AnalysisCards",
                ImVec2(layout.cards_x, layout.viewport_y),
                ImVec2(
                    layout.cards_width,
                    layout.viewport_height
                ),
                kBg,
                ImGuiWindowFlags_AlwaysVerticalScrollbar
            )) {
            if (view_index >= 0) {
                draw_cards(state, view_index, scale);
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();

        if (begin_region(
                "##AnalysisStatus",
                ImVec2(layout.center_x, layout.status_y),
                ImVec2(
                    viewport->WorkSize.x - layout.center_x,
                    layout.status_height
                ),
                kPanel
            )) {
            draw_status_bar(state, scale);
        }
        ImGui::EndChild();
    }
    ImGui::End();
    ImGui::PopStyleColor(10);
    ImGui::PopStyleVar(6);

    for (auto& view : state.render_views) {
        if (view.viewport_index != view_index &&
            !view.detached &&
            !view.force_undock_next_frame) {
            view.render_requested = false;
        }
    }
    return result;
}

} // namespace gs3d::ui
