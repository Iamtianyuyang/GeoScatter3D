#include "ui/WelcomePage.hpp"

#include "gui/UiFonts.hpp"
#include "imgui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace gs3d::ui {

namespace {

constexpr ImU32 kPageBg = IM_COL32(25, 26, 29, 255);
constexpr ImU32 kPanelBg = IM_COL32(36, 37, 41, 255);
constexpr ImU32 kPanelHover = IM_COL32(42, 44, 49, 255);
constexpr ImU32 kPanelBorder = IM_COL32(104, 108, 117, 90);
constexpr ImU32 kPanelBorderHover = IM_COL32(70, 146, 218, 190);
constexpr ImU32 kPrimary = IM_COL32(45, 137, 214, 255);
constexpr ImU32 kPrimaryHover = IM_COL32(57, 151, 231, 255);
constexpr ImU32 kPrimarySoft = IM_COL32(45, 137, 214, 42);
constexpr ImU32 kText = IM_COL32(232, 233, 235, 255);
constexpr ImU32 kTextMuted = IM_COL32(168, 171, 178, 255);
constexpr ImU32 kTextFaint = IM_COL32(124, 128, 137, 255);
constexpr ImU32 kDanger = IM_COL32(224, 103, 103, 255);

enum class FileDialogKind {
    None,
    Project,
    RawData
};

struct FileDialogState {
    FileDialogKind kind = FileDialogKind::None;
    std::filesystem::path directory;
    std::filesystem::path selection;
    std::array<char, 1024> path_input{};
    std::string error;
};

FileDialogState g_file_dialog;

ImFont* regular_font()
{
    auto* font = gs3d::gui::ui_fonts().regular;
    return font != nullptr ? font : ImGui::GetFont();
}

void set_path_input(const std::filesystem::path& path)
{
    const std::string text = path.string();
    std::snprintf(
        g_file_dialog.path_input.data(),
        g_file_dialog.path_input.size(),
        "%s",
        text.c_str()
    );
}

bool has_raw_extension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        }
    );
    return extension == ".csv" || extension == ".dat";
}

bool is_project_directory(const std::filesystem::path& path)
{
    std::error_code ec;
    return std::filesystem::is_directory(path, ec) &&
        std::filesystem::is_regular_file(
            path / "manifest.toml",
            ec
        );
}

std::filesystem::path normalized_project_selection(
    const std::filesystem::path& path
) {
    if (path.filename() == "manifest.toml") {
        return path.parent_path();
    }
    return path;
}

bool is_valid_selection(
    FileDialogKind kind,
    const std::filesystem::path& path
) {
    std::error_code ec;
    if (kind == FileDialogKind::Project) {
        return is_project_directory(
            normalized_project_selection(path)
        );
    }
    return has_raw_extension(path) &&
        std::filesystem::is_regular_file(path, ec);
}

void open_file_dialog(FileDialogKind kind)
{
    g_file_dialog = {};
    g_file_dialog.kind = kind;

    std::error_code ec;
    auto start = std::filesystem::current_path(ec);
    if (ec) {
        start = ".";
    }
    if (std::filesystem::is_directory(start / "data", ec)) {
        start /= "data";
    }
    g_file_dialog.directory = start;
    set_path_input(start);
    ImGui::OpenPopup("选择数据###WelcomeFileDialog");
}

void accept_file_dialog(gs3d::app::UiActions& actions)
{
    std::filesystem::path selected =
        g_file_dialog.path_input.data();
    if (selected.empty()) {
        selected = g_file_dialog.selection;
    }
    if (!is_valid_selection(g_file_dialog.kind, selected)) {
        g_file_dialog.error =
            g_file_dialog.kind == FileDialogKind::Project
                ? "请选择包含 manifest.toml 的 .gs3d.bundle 项目目录"
                : "请选择有效的 .csv 或 .dat 文件";
        return;
    }

    if (g_file_dialog.kind == FileDialogKind::Project) {
        actions.open_project_path =
            normalized_project_selection(selected).string();
    } else {
        actions.open_raw_data_path = selected.string();
    }
    ImGui::CloseCurrentPopup();
}

std::vector<std::filesystem::directory_entry> visible_entries()
{
    std::vector<std::filesystem::directory_entry> entries;
    std::error_code ec;
    std::filesystem::directory_iterator iterator(
        g_file_dialog.directory,
        std::filesystem::directory_options::skip_permission_denied,
        ec
    );
    const std::filesystem::directory_iterator end;
    for (; !ec && iterator != end; iterator.increment(ec)) {
        const auto& entry = *iterator;
        const auto name = entry.path().filename().string();
        if (!name.empty() && name.front() == '.') {
            continue;
        }
        if (entry.is_directory(ec)) {
            entries.push_back(entry);
            continue;
        }
        if (g_file_dialog.kind == FileDialogKind::RawData &&
            has_raw_extension(entry.path())) {
            entries.push_back(entry);
        } else if (
            g_file_dialog.kind == FileDialogKind::Project &&
            entry.path().filename() == "manifest.toml"
        ) {
            entries.push_back(entry);
        }
    }
    std::stable_sort(
        entries.begin(),
        entries.end(),
        [](const auto& lhs, const auto& rhs) {
            std::error_code lhs_ec;
            std::error_code rhs_ec;
            const bool lhs_directory = lhs.is_directory(lhs_ec);
            const bool rhs_directory = rhs.is_directory(rhs_ec);
            if (lhs_directory != rhs_directory) {
                return lhs_directory;
            }
            return lhs.path().filename().string() <
                rhs.path().filename().string();
        }
    );
    return entries;
}

void draw_file_dialog(
    gs3d::app::UiActions& actions,
    float ui_scale
) {
    if (g_file_dialog.kind == FileDialogKind::None) {
        return;
    }

    const char* visible_title =
        g_file_dialog.kind == FileDialogKind::Project
            ? "打开 GeoScatter3D 项目###WelcomeFileDialog"
            : "加载原始数据###WelcomeFileDialog";
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float dialog_margin = 32.0f * ui_scale;
    ImGui::SetNextWindowSize(
        ImVec2(
            std::min(
                760.0f * ui_scale,
                std::max(
                    420.0f * ui_scale,
                    viewport->WorkSize.x - dialog_margin
                )
            ),
            std::min(
                540.0f * ui_scale,
                std::max(
                    360.0f * ui_scale,
                    viewport->WorkSize.y - dialog_margin
                )
            )
        ),
        ImGuiCond_Appearing
    );
    if (!ImGui::BeginPopupModal(
            visible_title,
            nullptr,
            ImGuiWindowFlags_NoCollapse
        )) {
        return;
    }

    if (ImGui::Button("上一级")) {
        const auto parent = g_file_dialog.directory.parent_path();
        if (!parent.empty()) {
            g_file_dialog.directory = parent;
            g_file_dialog.selection.clear();
            set_path_input(g_file_dialog.directory);
            g_file_dialog.error.clear();
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled(
        "%s",
        g_file_dialog.directory.string().c_str()
    );
    ImGui::Separator();

    const float footer_height = 96.0f * ui_scale;
    ImGui::BeginChild(
        "##FileEntries",
        ImVec2(0.0f, -footer_height),
        true
    );
    const auto entries = visible_entries();
    if (entries.empty()) {
        ImGui::TextDisabled("此目录中没有可显示的内容");
    }
    for (const auto& entry : entries) {
        std::error_code ec;
        const bool is_directory = entry.is_directory(ec);
        const bool is_bundle = is_directory &&
            is_project_directory(entry.path());
        const bool is_manifest =
            entry.path().filename() == "manifest.toml";
        const bool is_raw =
            !is_directory && has_raw_extension(entry.path());

        std::string prefix;
        if (is_bundle || is_manifest) {
            prefix = "[项目]  ";
        } else if (is_directory) {
            prefix = "[目录]  ";
        } else {
            prefix = "[数据]  ";
        }
        const std::string label =
            prefix + entry.path().filename().string();
        const bool selected =
            g_file_dialog.selection == entry.path();
        if (ImGui::Selectable(label.c_str(), selected)) {
            g_file_dialog.selection = entry.path();
            set_path_input(entry.path());
            g_file_dialog.error.clear();
        }
        if (ImGui::IsItemHovered() &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            if (is_bundle || is_manifest || is_raw) {
                set_path_input(entry.path());
                accept_file_dialog(actions);
            } else if (is_directory) {
                g_file_dialog.directory = entry.path();
                g_file_dialog.selection.clear();
                set_path_input(g_file_dialog.directory);
            }
        }
    }
    ImGui::EndChild();

    ImGui::TextDisabled("路径");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText(
        "##SelectedPath",
        g_file_dialog.path_input.data(),
        g_file_dialog.path_input.size()
    );
    if (!g_file_dialog.error.empty()) {
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImGui::ColorConvertU32ToFloat4(kDanger)
        );
        ImGui::TextUnformatted(g_file_dialog.error.c_str());
        ImGui::PopStyleColor();
    } else {
        ImGui::TextDisabled(
            g_file_dialog.kind == FileDialogKind::Project
                ? "支持包含 manifest.toml 的项目目录"
                : "支持 CSV / DAT，加载后会自动生成项目包"
        );
    }

    const float button_width = 96.0f * ui_scale;
    ImGui::SetCursorPosX(
        ImGui::GetWindowContentRegionMax().x -
        button_width * 2.0f -
        ImGui::GetStyle().ItemSpacing.x
    );
    if (ImGui::Button(
            "取消",
            ImVec2(button_width, 0.0f)
        )) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(
        ImGuiCol_Button,
        ImGui::ColorConvertU32ToFloat4(kPrimary)
    );
    ImGui::PushStyleColor(
        ImGuiCol_ButtonHovered,
        ImGui::ColorConvertU32ToFloat4(kPrimaryHover)
    );
    if (ImGui::Button(
            g_file_dialog.kind == FileDialogKind::Project
                ? "打开项目"
                : "加载数据",
            ImVec2(button_width, 0.0f)
        )) {
        accept_file_dialog(actions);
    }
    ImGui::PopStyleColor(2);
    ImGui::EndPopup();
}

void draw_brand_mark(
    ImDrawList* draw_list,
    const ImVec2& center,
    float scale
) {
    const float radius = 25.0f * scale;
    draw_list->AddRectFilled(
        ImVec2(center.x - radius, center.y - radius),
        ImVec2(center.x + radius, center.y + radius),
        kPrimary,
        5.0f * scale
    );
    const ImVec2 points[] = {
        {center.x - 10.0f * scale, center.y + 8.0f * scale},
        {center.x - 5.0f * scale, center.y - 8.0f * scale},
        {center.x + 5.0f * scale, center.y + 1.0f * scale},
        {center.x + 12.0f * scale, center.y - 10.0f * scale},
        {center.x + 13.0f * scale, center.y + 11.0f * scale},
    };
    draw_list->AddPolyline(
        points,
        5,
        IM_COL32(235, 246, 255, 170),
        ImDrawFlags_None,
        1.2f * scale
    );
    for (const auto& point : points) {
        draw_list->AddCircleFilled(
            point,
            2.7f * scale,
            IM_COL32(255, 255, 255, 255),
            12
        );
    }
}

void draw_text(
    ImDrawList* draw_list,
    float size,
    const ImVec2& position,
    ImU32 color,
    const char* text
) {
    draw_list->AddText(
        regular_font(),
        size,
        position,
        color,
        text
    );
}

bool draw_action_card(
    const char* id,
    const ImVec2& min,
    const ImVec2& size,
    float scale,
    const char* number,
    const char* title,
    const char* description
) {
    ImGui::SetCursorScreenPos(min);
    ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(
        min,
        ImVec2(min.x + size.x, min.y + size.y),
        hovered ? kPanelHover : kPanelBg,
        4.0f * scale
    );
    draw_list->AddRect(
        min,
        ImVec2(min.x + size.x, min.y + size.y),
        hovered ? kPanelBorderHover : kPanelBorder,
        4.0f * scale,
        0,
        hovered ? 1.4f : 1.0f
    );
    const ImVec2 badge_min{
        min.x + 18.0f * scale,
        min.y + 18.0f * scale
    };
    const ImVec2 badge_max{
        badge_min.x + 29.0f * scale,
        badge_min.y + 29.0f * scale
    };
    draw_list->AddRectFilled(
        badge_min,
        badge_max,
        kPrimarySoft,
        3.0f * scale
    );
    draw_text(
        draw_list,
        11.0f * scale,
        ImVec2(
            badge_min.x + 7.0f * scale,
            badge_min.y + 6.0f * scale
        ),
        IM_COL32(102, 183, 250, 255),
        number
    );
    draw_text(
        draw_list,
        15.0f * scale,
        ImVec2(
            min.x + 60.0f * scale,
            min.y + 17.0f * scale
        ),
        kText,
        title
    );
    draw_text(
        draw_list,
        12.0f * scale,
        ImVec2(
            min.x + 18.0f * scale,
            min.y + 61.0f * scale
        ),
        kTextMuted,
        description
    );
    draw_text(
        draw_list,
        12.0f * scale,
        ImVec2(
            min.x + size.x - 32.0f * scale,
            min.y + 26.0f * scale
        ),
        hovered ? kPrimaryHover : kTextFaint,
        ">"
    );
    return clicked;
}

std::string format_point_count(std::uint64_t count)
{
    char buffer[64]{};
    if (count >= 100000000ULL) {
        std::snprintf(
            buffer,
            sizeof(buffer),
            "%.2f 亿点",
            static_cast<double>(count) / 100000000.0
        );
    } else if (count >= 10000ULL) {
        std::snprintf(
            buffer,
            sizeof(buffer),
            "%.1f 万点",
            static_cast<double>(count) / 10000.0
        );
    } else {
        std::snprintf(
            buffer,
            sizeof(buffer),
            "%llu 点",
            static_cast<unsigned long long>(count)
        );
    }
    return buffer;
}

std::string recent_time_label(std::int64_t opened_unix)
{
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    const auto elapsed = std::max<std::int64_t>(0, now - opened_unix);
    if (elapsed < 60) {
        return "刚刚";
    }
    if (elapsed < 3600) {
        return std::to_string(elapsed / 60) + " 分钟前";
    }
    if (elapsed < 86400) {
        return std::to_string(elapsed / 3600) + " 小时前";
    }
    return std::to_string(elapsed / 86400) + " 天前";
}

bool draw_recent_row(
    const gs3d::app::RecentProjectEntry& entry,
    const ImVec2& min,
    float width,
    float height,
    float scale
) {
    std::error_code ec;
    const bool available =
        std::filesystem::exists(entry.path, ec);
    const std::string id =
        "##RecentProject" + entry.path.string();
    ImGui::SetCursorScreenPos(min);
    ImGui::InvisibleButton(
        id.c_str(),
        ImVec2(width, height)
    );
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked =
        available && ImGui::IsItemClicked();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    if (hovered) {
        draw_list->AddRectFilled(
            min,
            ImVec2(min.x + width, min.y + height),
            kPanelHover,
            3.0f * scale
        );
    }
    const std::string name =
        entry.path.filename().string();
    draw_text(
        draw_list,
        13.0f * scale,
        ImVec2(
            min.x + 13.0f * scale,
            min.y + 9.0f * scale
        ),
        available ? kText : kTextFaint,
        name.c_str()
    );
    const std::string path_text = entry.path.string();
    draw_text(
        draw_list,
        10.5f * scale,
        ImVec2(
            min.x + 13.0f * scale,
            min.y + 31.0f * scale
        ),
        kTextFaint,
        path_text.c_str()
    );
    const std::string time = available
        ? recent_time_label(entry.last_opened_unix)
        : "路径不可用";
    const ImVec2 time_size = regular_font()->CalcTextSizeA(
        10.5f * scale,
        10000.0f,
        0.0f,
        time.c_str()
    );
    draw_text(
        draw_list,
        10.5f * scale,
        ImVec2(
            min.x + width - time_size.x - 13.0f * scale,
            min.y + 9.0f * scale
        ),
        available ? kTextFaint : kDanger,
        time.c_str()
    );
    draw_list->AddLine(
        ImVec2(min.x, min.y + height),
        ImVec2(min.x + width, min.y + height),
        IM_COL32(90, 93, 100, 65),
        1.0f
    );
    if (hovered) {
        ImGui::SetTooltip("%s", path_text.c_str());
    }
    return clicked;
}

float draw_load_section(
    const gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    const ImVec2& min,
    float width,
    bool compact,
    float scale,
    bool& enter_workspace
) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    float y = min.y;
    draw_text(
        draw_list,
        11.0f * scale,
        ImVec2(min.x, y),
        kTextFaint,
        "开始"
    );
    y += 25.0f * scale;

    const float card_gap = 12.0f * scale;
    const bool side_by_side =
        width >= 620.0f * scale;
    const float card_width = side_by_side
        ? (width - card_gap) * 0.5f
        : width;
    const float card_height =
        (compact ? 84.0f : 96.0f) * scale;
    if (draw_action_card(
            "##OpenProjectCard",
            ImVec2(min.x, y),
            ImVec2(card_width, card_height),
            scale,
            "01",
            "打开项目",
            "加载 .gs3d.bundle 项目"
        )) {
        open_file_dialog(FileDialogKind::Project);
    }
    const ImVec2 raw_min = side_by_side
        ? ImVec2(min.x + card_width + card_gap, y)
        : ImVec2(min.x, y + card_height + card_gap);
    if (draw_action_card(
            "##OpenRawCard",
            raw_min,
            ImVec2(card_width, card_height),
            scale,
            "02",
            "加载原始数据",
            "导入 DAT / CSV 并创建项目"
        )) {
        open_file_dialog(FileDialogKind::RawData);
    }
    y += side_by_side
        ? card_height
        : card_height * 2.0f + card_gap;

    y += 18.0f * scale;
    draw_text(
        draw_list,
        11.0f * scale,
        ImVec2(min.x, y),
        kTextFaint,
        "当前会话"
    );
    y += 25.0f * scale;
    const float current_height = 69.0f * scale;
    draw_list->AddRectFilled(
        ImVec2(min.x, y),
        ImVec2(min.x + width, y + current_height),
        kPanelBg,
        4.0f * scale
    );
    draw_list->AddRectFilled(
        ImVec2(min.x, y),
        ImVec2(
            min.x + 3.0f * scale,
            y + current_height
        ),
        kPrimary,
        4.0f * scale,
        ImDrawFlags_RoundCornersLeft
    );
    const char* dataset_name =
        state.dataset.active_dataset.empty()
            ? "尚未加载数据"
            : state.dataset.active_dataset.c_str();
    draw_text(
        draw_list,
        13.5f * scale,
        ImVec2(
            min.x + 17.0f * scale,
            y + 13.0f * scale
        ),
        kText,
        dataset_name
    );
    const std::string summary =
        format_point_count(state.dataset.point_count) +
        "  ·  " + state.dataset.format;
    draw_text(
        draw_list,
        11.0f * scale,
        ImVec2(
            min.x + 17.0f * scale,
            y + 39.0f * scale
        ),
        kTextMuted,
        summary.c_str()
    );

    const float continue_width = 118.0f * scale;
    const float continue_height = 31.0f * scale;
    ImGui::SetCursorScreenPos(ImVec2(
        min.x + width - continue_width - 14.0f * scale,
        y + 19.0f * scale
    ));
    ImGui::PushStyleColor(
        ImGuiCol_Button,
        ImGui::ColorConvertU32ToFloat4(kPrimary)
    );
    ImGui::PushStyleColor(
        ImGuiCol_ButtonHovered,
        ImGui::ColorConvertU32ToFloat4(kPrimaryHover)
    );
    if (ImGui::Button(
            "继续编辑",
            ImVec2(continue_width, continue_height)
        )) {
        enter_workspace = true;
    }
    ImGui::PopStyleColor(2);
    return y + current_height - min.y;
}

float draw_recent_section(
    const gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    const ImVec2& min,
    float width,
    float max_height,
    float scale
) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_text(
        draw_list,
        16.0f * scale,
        min,
        kText,
        "最近使用"
    );
    draw_text(
        draw_list,
        11.0f * scale,
        ImVec2(min.x, min.y + 27.0f * scale),
        kTextFaint,
        "快速回到最近打开的 GeoScatter3D 项目"
    );
    const float list_top = min.y + 58.0f * scale;
    const float row_height = 55.0f * scale;
    if (state.recent_projects.empty()) {
        const ImVec2 empty_max{
            min.x + width,
            list_top + 116.0f * scale
        };
        draw_list->AddRectFilled(
            ImVec2(min.x, list_top),
            empty_max,
            kPanelBg,
            4.0f * scale
        );
        draw_text(
            draw_list,
            12.0f * scale,
            ImVec2(
                min.x + 18.0f * scale,
                list_top + 31.0f * scale
            ),
            kTextMuted,
            "还没有历史项目"
        );
        draw_text(
            draw_list,
            10.5f * scale,
            ImVec2(
                min.x + 18.0f * scale,
                list_top + 58.0f * scale
            ),
            kTextFaint,
            "打开项目后会自动出现在这里"
        );
        return 174.0f * scale;
    }

    const float list_height = std::max(
        row_height * 2.0f,
        max_height - 58.0f * scale
    );
    ImGui::SetCursorScreenPos(ImVec2(min.x, list_top));
    ImGui::BeginChild(
        "##RecentProjectList",
        ImVec2(width, list_height),
        false,
        ImGuiWindowFlags_None
    );
    const ImVec2 list_origin = ImGui::GetCursorScreenPos();
    const float row_width = ImGui::GetContentRegionAvail().x;
    for (std::size_t i = 0;
         i < state.recent_projects.size();
         ++i) {
        const auto& entry =
            state.recent_projects[i];
        if (draw_recent_row(
                entry,
                ImVec2(
                    list_origin.x,
                    list_origin.y +
                        static_cast<float>(i) * row_height
                ),
                row_width,
                row_height,
                scale
            )) {
            actions.open_project_path = entry.path.string();
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(
        list_origin.x,
        list_origin.y +
            static_cast<float>(state.recent_projects.size()) *
            row_height
    ));
    ImGui::Dummy(ImVec2(1.0f, 1.0f));
    ImGui::EndChild();
    return 58.0f * scale + list_height;
}

} // namespace

bool draw_welcome_page(
    const gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    float ui_scale
) {
    const ImVec2 canvas_min = ImGui::GetCursorScreenPos();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(
        canvas_min,
        ImVec2(
            canvas_min.x + available.x,
            canvas_min.y + available.y
        ),
        kPageBg
    );

    const bool landscape =
        available.x >= 880.0f * ui_scale &&
        available.x > available.y * 1.18f;
    const float side_margin = std::clamp(
        available.x * 0.055f,
        24.0f * ui_scale,
        76.0f * ui_scale
    );
    const float top_margin = std::clamp(
        available.y * 0.07f,
        24.0f * ui_scale,
        56.0f * ui_scale
    );
    const float content_width = std::min(
        available.x - side_margin * 2.0f,
        1180.0f * ui_scale
    );
    const float content_left =
        canvas_min.x + (available.x - content_width) * 0.5f;
    float content_bottom = canvas_min.y + top_margin;

    const ImVec2 brand_center{
        content_left + 25.0f * ui_scale,
        canvas_min.y + top_margin + 25.0f * ui_scale
    };
    draw_brand_mark(draw_list, brand_center, ui_scale);
    draw_text(
        draw_list,
        22.0f * ui_scale,
        ImVec2(
            brand_center.x + 39.0f * ui_scale,
            brand_center.y - 17.0f * ui_scale
        ),
        kText,
        "GeoScatter3D"
    );
    draw_text(
        draw_list,
        11.0f * ui_scale,
        ImVec2(
            brand_center.x + 40.0f * ui_scale,
            brand_center.y + 12.0f * ui_scale
        ),
        kTextFaint,
        "三维散点数据工作台"
    );

    const float section_top =
        brand_center.y + 72.0f * ui_scale;
    bool enter_workspace = false;
    if (landscape) {
        const float column_gap = 70.0f * ui_scale;
        const float left_width =
            (content_width - column_gap) * 0.54f;
        const float right_width =
            content_width - column_gap - left_width;
        draw_text(
            draw_list,
            26.0f * ui_scale,
            ImVec2(content_left, section_top),
            kText,
            "欢迎回来"
        );
        draw_text(
            draw_list,
            12.5f * ui_scale,
            ImVec2(
                content_left,
                section_top + 40.0f * ui_scale
            ),
            kTextMuted,
            "打开项目，或从 DAT / CSV 开始新的分析。"
        );
        const float load_height = draw_load_section(
            state,
            actions,
            ImVec2(
                content_left,
                section_top + 82.0f * ui_scale
            ),
            left_width,
            true,
            ui_scale,
            enter_workspace
        );
        const float recent_height = draw_recent_section(
            state,
            actions,
            ImVec2(
                content_left + left_width + column_gap,
                section_top
            ),
            right_width,
            std::max(
                220.0f * ui_scale,
                available.y -
                    (section_top - canvas_min.y) -
                    28.0f * ui_scale
            ),
            ui_scale
        );
        content_bottom = std::max(
            section_top + 82.0f * ui_scale + load_height,
            section_top + recent_height
        );
    } else {
        draw_text(
            draw_list,
            24.0f * ui_scale,
            ImVec2(content_left, section_top),
            kText,
            "开始探索你的数据"
        );
        draw_text(
            draw_list,
            12.0f * ui_scale,
            ImVec2(
                content_left,
                section_top + 36.0f * ui_scale
            ),
            kTextMuted,
            "打开项目，或从 DAT / CSV 创建新的项目。"
        );
        const float load_height = draw_load_section(
            state,
            actions,
            ImVec2(
                content_left,
                section_top + 72.0f * ui_scale
            ),
            content_width,
            false,
            ui_scale,
            enter_workspace
        );
        const float recent_top =
            section_top +
            72.0f * ui_scale +
            load_height +
            34.0f * ui_scale;
        const float recent_height = draw_recent_section(
            state,
            actions,
            ImVec2(content_left, recent_top),
            content_width,
            270.0f * ui_scale,
            ui_scale
        );
        content_bottom = recent_top + recent_height;
    }

    const float required_height =
        content_bottom - canvas_min.y + 28.0f * ui_scale;
    if (required_height > available.y) {
        ImGui::SetCursorScreenPos(ImVec2(
            canvas_min.x,
            canvas_min.y + required_height - 1.0f
        ));
        ImGui::Dummy(ImVec2(1.0f, 1.0f));
    }

    draw_file_dialog(actions, ui_scale);
    return enter_workspace;
}

} // namespace gs3d::ui
