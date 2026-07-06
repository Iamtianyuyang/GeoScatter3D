#include "ui/WelcomePage.hpp"

#include "gui/UiFonts.hpp"
#include "imgui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace gs3d::ui {

namespace {

ImU32 display_color(int red, int green, int blue, int alpha = 255)
{
    static const std::array<int, 256> linear_bytes = [] {
        std::array<int, 256> values{};
        for (int value = 0; value < 256; ++value) {
            const float srgb = static_cast<float>(value) / 255.0f;
            const float linear = srgb <= 0.04045f
                ? srgb / 12.92f
                : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
            values[static_cast<std::size_t>(value)] =
                static_cast<int>(std::lround(linear * 255.0f));
        }
        return values;
    }();
    return IM_COL32(
        linear_bytes[static_cast<std::size_t>(red)],
        linear_bytes[static_cast<std::size_t>(green)],
        linear_bytes[static_cast<std::size_t>(blue)],
        alpha
    );
}

const ImU32 kPanelBg = display_color(14, 28, 43, 238);
const ImU32 kPanelHover = display_color(20, 39, 58, 248);
const ImU32 kPanelBorder = display_color(83, 111, 139, 105);
const ImU32 kPanelBorderHover = display_color(84, 169, 246, 215);
const ImU32 kPrimary = display_color(71, 157, 236);
const ImU32 kPrimaryHover = display_color(92, 177, 255);
const ImU32 kText = display_color(239, 245, 252);
const ImU32 kTextMuted = display_color(174, 189, 204);
const ImU32 kTextMetadata = display_color(132, 153, 174);
const ImU32 kTextFaint = display_color(112, 132, 153);
const ImU32 kDanger = display_color(224, 103, 103);
const ImU32 kSessionBg = display_color(12, 31, 48, 248);
const ImU32 kRecentBg = display_color(9, 22, 36, 236);
const ImU32 kPlaceholderBg = display_color(13, 29, 45, 130);

enum class ActionCardIcon {
    Folder,
    Database
};

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
    VkDescriptorSet logo_descriptor,
    float scale
) {
    const float H = 32.0f * scale;

    if (logo_descriptor != VK_NULL_HANDLE) {
        const ImTextureID tid = static_cast<ImTextureID>(
            reinterpret_cast<ImU64>(logo_descriptor)
        );
        draw_list->AddImage(
            tid,
            ImVec2(center.x - H, center.y - H),
            ImVec2(center.x + H, center.y + H),
            ImVec2(0.0f, 0.0f),
            ImVec2(1.0f, 1.0f)
        );
    } else {
        // Fallback: dark square when no texture available
        draw_list->AddRectFilled(
            ImVec2(center.x - H, center.y - H),
            ImVec2(center.x + H, center.y + H),
            display_color(9, 26, 45),
            14.0f * scale
        );
    }
}

void draw_welcome_backdrop(
    ImDrawList* draw_list,
    const ImVec2& min,
    const ImVec2& size,
    float scale
) {
    const ImVec2 max{min.x + size.x, min.y + size.y};
    draw_list->AddRectFilledMultiColor(
        min,
        max,
        display_color(7, 17, 29),
        display_color(5, 14, 24),
        display_color(8, 20, 34),
        display_color(7, 17, 29)
    );

    draw_list->PushClipRect(min, max, true);
    constexpr int kRows = 15;
    constexpr int kColumns = 58;
    const float cloud_left = min.x + size.x * 0.27f;
    const float cloud_width = size.x * 0.73f;
    for (int row = 0; row < kRows; ++row) {
        const float row_t = static_cast<float>(row) /
            static_cast<float>(kRows - 1);
        for (int column = 0; column < kColumns; ++column) {
            const float t = static_cast<float>(column) /
                static_cast<float>(kColumns - 1);
            const float wave =
                std::sin(t * 12.0f + row_t * 2.1f) *
                    26.0f * scale +
                std::sin(t * 4.7f - row_t * 1.5f) *
                    18.0f * scale;
            const float x = cloud_left + t * cloud_width +
                row_t * 16.0f * scale;
            const float y = min.y + 54.0f * scale +
                row_t * 11.0f * scale + wave;
            const int alpha = static_cast<int>(
                10.0f + (1.0f - row_t) * 24.0f
            );
            draw_list->AddCircleFilled(
                ImVec2(x, y),
                0.75f * scale,
                display_color(86, 171, 242, alpha),
                6
            );
        }
    }

    constexpr int kContourCount = 8;
    constexpr int kContourPoints = 42;
    for (int contour = 0; contour < kContourCount; ++contour) {
        std::array<ImVec2, kContourPoints> points{};
        for (int i = 0; i < kContourPoints; ++i) {
            const float t = static_cast<float>(i) /
                static_cast<float>(kContourPoints - 1);
            points[static_cast<std::size_t>(i)] = ImVec2(
                min.x - 46.0f * scale +
                    t * size.x * 0.44f,
                max.y - 54.0f * scale +
                    contour * 8.0f * scale +
                    std::sin(t * 9.0f + contour * 0.42f) *
                        (20.0f + contour * 1.5f) * scale
            );
        }
        draw_list->AddPolyline(
            points.data(),
            kContourPoints,
            display_color(70, 145, 205, 8),
            ImDrawFlags_None,
            0.7f * scale
        );
    }
    draw_list->PopClipRect();
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

void draw_section_label(
    ImDrawList* draw_list,
    const ImVec2& position,
    float scale,
    const char* text,
    float font_size = 13.0f
) {
    draw_list->AddRectFilled(
        position,
        ImVec2(
            position.x + 3.0f * scale,
            position.y + (font_size + 6.0f) * scale
        ),
        kPrimary,
        1.5f * scale
    );
    draw_text(
        draw_list,
        font_size * scale,
        ImVec2(
            position.x + 13.0f * scale,
            position.y - 1.0f * scale
        ),
        display_color(196, 211, 226),
        text
    );
}

void draw_action_icon(
    ImDrawList* draw_list,
    ActionCardIcon icon,
    const ImVec2& center,
    float scale
) {
    const ImU32 color = display_color(114, 196, 255);
    const float thickness = 1.7f * scale;
    if (icon == ActionCardIcon::Folder) {
        const ImVec2 points[] = {
            {center.x - 15.0f * scale, center.y - 7.0f * scale},
            {center.x - 5.0f * scale, center.y - 7.0f * scale},
            {center.x - 1.0f * scale, center.y - 2.0f * scale},
            {center.x + 17.0f * scale, center.y - 2.0f * scale},
            {center.x + 14.0f * scale, center.y + 12.0f * scale},
            {center.x - 17.0f * scale, center.y + 12.0f * scale},
            {center.x - 15.0f * scale, center.y - 7.0f * scale}
        };
        draw_list->AddPolyline(
            points,
            7,
            color,
            ImDrawFlags_None,
            thickness
        );
        return;
    }

    draw_list->AddEllipse(
        ImVec2(center.x, center.y - 9.0f * scale),
        ImVec2(13.0f * scale, 5.0f * scale),
        color,
        0.0f,
        24,
        thickness
    );
    draw_list->AddEllipse(
        ImVec2(center.x, center.y + 8.0f * scale),
        ImVec2(13.0f * scale, 5.0f * scale),
        color,
        0.0f,
        24,
        thickness
    );
    draw_list->AddLine(
        ImVec2(center.x - 13.0f * scale, center.y - 9.0f * scale),
        ImVec2(center.x - 13.0f * scale, center.y + 8.0f * scale),
        color,
        thickness
    );
    draw_list->AddLine(
        ImVec2(center.x + 13.0f * scale, center.y - 9.0f * scale),
        ImVec2(center.x + 13.0f * scale, center.y + 8.0f * scale),
        color,
        thickness
    );
    draw_list->AddEllipse(
        ImVec2(center.x, center.y - 1.0f * scale),
        ImVec2(13.0f * scale, 5.0f * scale),
        display_color(114, 196, 255, 150),
        0.0f,
        24,
        thickness
    );
}

void draw_document_icon(
    ImDrawList* draw_list,
    const ImVec2& center,
    float scale
) {
    const ImU32 color = display_color(105, 190, 255);
    const float thickness = 1.3f * scale;
    const ImVec2 min{
        center.x - 8.0f * scale,
        center.y - 12.0f * scale
    };
    const ImVec2 max{
        center.x + 8.0f * scale,
        center.y + 12.0f * scale
    };
    draw_list->AddRect(
        min,
        max,
        color,
        1.5f * scale,
        0,
        thickness
    );
    draw_list->AddLine(
        ImVec2(center.x + 1.0f * scale, min.y),
        ImVec2(center.x + 1.0f * scale, center.y - 5.0f * scale),
        color,
        thickness
    );
    draw_list->AddLine(
        ImVec2(center.x + 1.0f * scale, center.y - 5.0f * scale),
        ImVec2(max.x, center.y - 5.0f * scale),
        color,
        thickness
    );
    draw_list->AddLine(
        ImVec2(center.x - 4.0f * scale, center.y + 1.0f * scale),
        ImVec2(center.x + 4.0f * scale, center.y + 1.0f * scale),
        color,
        thickness
    );
    draw_list->AddLine(
        ImVec2(center.x - 4.0f * scale, center.y + 6.0f * scale),
        ImVec2(center.x + 4.0f * scale, center.y + 6.0f * scale),
        color,
        thickness
    );
}

void draw_dataset_icon(
    ImDrawList* draw_list,
    const ImVec2& center,
    float scale
) {
    const ImVec2 points[] = {
        {center.x, center.y - 14.0f * scale},
        {center.x + 13.0f * scale, center.y - 7.0f * scale},
        {center.x + 13.0f * scale, center.y + 8.0f * scale},
        {center.x, center.y + 15.0f * scale},
        {center.x - 13.0f * scale, center.y + 8.0f * scale},
        {center.x - 13.0f * scale, center.y - 7.0f * scale}
    };
    const ImU32 color = display_color(103, 187, 255, 230);
    for (int i = 0; i < 6; ++i) {
        draw_list->AddLine(
            points[i],
            points[(i + 1) % 6],
            color,
            1.1f * scale
        );
        draw_list->AddCircleFilled(
            points[i],
            2.2f * scale,
            display_color(105, 193, 255),
            8
        );
    }
    draw_list->AddLine(
        points[0],
        points[3],
        color,
        1.0f * scale
    );
    draw_list->AddLine(
        points[1],
        points[4],
        color,
        1.0f * scale
    );
}

bool draw_action_card(
    const char* id,
    const ImVec2& min,
    const ImVec2& size,
    float scale,
    const char* number,
    const char* title,
    const char* description,
    ActionCardIcon icon
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
        8.0f * scale
    );
    draw_list->AddRect(
        min,
        ImVec2(min.x + size.x, min.y + size.y),
        hovered ? kPanelBorderHover : kPanelBorder,
        8.0f * scale,
        0,
        hovered ? 1.4f : 1.0f
    );
    draw_list->AddRectFilled(
        min,
        ImVec2(
            min.x + 3.0f * scale,
            min.y + size.y
        ),
        hovered ? kPrimaryHover : kPrimary,
        8.0f * scale,
        ImDrawFlags_RoundCornersLeft
    );
    const ImVec2 badge_min{
        min.x + 22.0f * scale,
        min.y + (size.y - 62.0f * scale) * 0.5f
    };
    const ImVec2 badge_max{
        badge_min.x + 62.0f * scale,
        badge_min.y + 62.0f * scale
    };
    draw_list->AddRectFilled(
        badge_min,
        badge_max,
        display_color(68, 112, 153, hovered ? 54 : 38),
        9.0f * scale
    );
    draw_action_icon(
        draw_list,
        icon,
        ImVec2(
            (badge_min.x + badge_max.x) * 0.5f,
            (badge_min.y + badge_max.y) * 0.5f
        ),
        1.12f * scale
    );
    draw_text(
        draw_list,
        15.5f * scale,
        ImVec2(
            min.x + 106.0f * scale,
            min.y + 27.0f * scale
        ),
        display_color(97, 180, 251),
        number
    );
    draw_text(
        draw_list,
        18.0f * scale,
        ImVec2(
            min.x + 144.0f * scale,
            min.y + 25.0f * scale
        ),
        kText,
        title
    );
    draw_text(
        draw_list,
        12.5f * scale,
        ImVec2(
            min.x + 106.0f * scale,
            min.y + 63.0f * scale
        ),
        kTextMuted,
        description
    );
    const float arrow_x = min.x + size.x - 34.0f * scale;
    const float arrow_y = min.y + size.y * 0.5f;
    draw_list->AddLine(
        ImVec2(arrow_x - 5.0f * scale, arrow_y - 9.0f * scale),
        ImVec2(arrow_x + 3.0f * scale, arrow_y),
        hovered ? kPrimaryHover : display_color(177, 195, 214, 230),
        2.0f * scale
    );
    draw_list->AddLine(
        ImVec2(arrow_x + 3.0f * scale, arrow_y),
        ImVec2(arrow_x - 5.0f * scale, arrow_y + 9.0f * scale),
        hovered ? kPrimaryHover : display_color(177, 195, 214, 230),
        2.0f * scale
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
    float scale,
    bool prominent
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
    if (prominent || hovered) {
        draw_list->AddRectFilled(
            min,
            ImVec2(min.x + width, min.y + height),
            hovered
                ? kPanelHover
                : display_color(14, 32, 49, 210),
            7.0f * scale
        );
    }
    const ImVec2 icon_min{
        min.x + 16.0f * scale,
        min.y + (height - 48.0f * scale) * 0.5f
    };
    draw_list->AddRectFilled(
        icon_min,
        ImVec2(
            icon_min.x + 48.0f * scale,
            icon_min.y + 48.0f * scale
        ),
        display_color(63, 102, 139, 42),
        8.0f * scale
    );
    draw_document_icon(
        draw_list,
        ImVec2(
            icon_min.x + 24.0f * scale,
            icon_min.y + 24.0f * scale
        ),
        1.08f * scale
    );
    const std::string name =
        entry.path.filename().string();
    draw_list->PushClipRect(
        ImVec2(min.x + 78.0f * scale, min.y),
        ImVec2(
            min.x + width - 90.0f * scale,
            min.y + height
        ),
        true
    );
    draw_text(
        draw_list,
        15.5f * scale,
        ImVec2(
            min.x + 78.0f * scale,
            min.y + 17.0f * scale
        ),
        available ? kText : kTextFaint,
        name.c_str()
    );
    const std::string path_text = entry.path.string();
    draw_text(
        draw_list,
        10.5f * scale,
        ImVec2(
            min.x + 78.0f * scale,
            min.y + 48.0f * scale
        ),
        kTextMetadata,
        path_text.c_str()
    );
    draw_list->PopClipRect();
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
        11.0f * scale,
        ImVec2(
            min.x + width - time_size.x - 13.0f * scale,
            min.y + 18.0f * scale
        ),
        available ? kTextFaint : kDanger,
        time.c_str()
    );
    draw_list->AddLine(
        ImVec2(min.x, min.y + height),
        ImVec2(min.x + width, min.y + height),
        display_color(91, 124, 154, 70),
        1.0f
    );
    if (hovered) {
        ImGui::SetTooltip("%s", path_text.c_str());
    }
    return clicked;
}

void draw_recent_placeholder(
    ImDrawList* draw_list,
    const ImVec2& min,
    float width,
    float height,
    float scale,
    int index
) {
    draw_list->AddRectFilled(
        min,
        ImVec2(min.x + width, min.y + height),
        kPlaceholderBg,
        6.0f * scale
    );
    draw_list->AddCircle(
        ImVec2(
            min.x + 27.0f * scale,
            min.y + height * 0.5f
        ),
        5.0f * scale,
        display_color(89, 132, 169, 105),
        12,
        1.0f * scale
    );
    draw_text(
        draw_list,
        11.0f * scale,
        ImVec2(
            min.x + 48.0f * scale,
            min.y + (height - 12.0f * scale) * 0.5f
        ),
        index == 0 ? kTextMetadata : kTextFaint,
        index == 0
            ? "暂无更多最近项目"
            : "最近打开的项目会显示在这里"
    );
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
    draw_section_label(draw_list, ImVec2(min.x, y), scale, "开始");
    y += 29.0f * scale;

    const float card_gap = 14.0f * scale;
    const bool side_by_side =
        !compact && width >= 840.0f * scale;
    const float card_width = side_by_side
        ? (width - card_gap) * 0.5f
        : width;
    const float card_height = 104.0f * scale;
    if (draw_action_card(
            "##OpenProjectCard",
            ImVec2(min.x, y),
            ImVec2(card_width, card_height),
            scale,
            "01",
            "打开项目",
            "加载 .gs3d.bundle 项目",
            ActionCardIcon::Folder
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
            "导入 DAT / CSV 并创建项目",
            ActionCardIcon::Database
        )) {
        open_file_dialog(FileDialogKind::RawData);
    }
    y += side_by_side
        ? card_height
        : card_height * 2.0f + card_gap;

    y += 18.0f * scale;
    draw_section_label(
        draw_list,
        ImVec2(min.x, y),
        scale,
        "当前会话"
    );
    y += 29.0f * scale;
    const bool compact_current = width < 500.0f * scale;
    const float current_height =
        (compact_current ? 148.0f : 96.0f) * scale;
    draw_list->AddRectFilled(
        ImVec2(min.x, y),
        ImVec2(min.x + width, y + current_height),
        kSessionBg,
        9.0f * scale
    );
    draw_list->AddRect(
        ImVec2(min.x, y),
        ImVec2(min.x + width, y + current_height),
        display_color(72, 145, 207, 145),
        9.0f * scale,
        0,
        1.1f * scale
    );
    draw_list->AddRectFilled(
        ImVec2(min.x, y),
        ImVec2(
            min.x + 3.0f * scale,
            y + current_height
        ),
        kPrimary,
        9.0f * scale,
        ImDrawFlags_RoundCornersLeft
    );
    const ImVec2 dataset_icon_min{
        min.x + 20.0f * scale,
        y + 19.0f * scale
    };
    draw_list->AddRectFilled(
        dataset_icon_min,
        ImVec2(
            dataset_icon_min.x + 58.0f * scale,
            dataset_icon_min.y + 58.0f * scale
        ),
        display_color(66, 107, 145, 42),
        9.0f * scale
    );
    draw_dataset_icon(
        draw_list,
        ImVec2(
            dataset_icon_min.x + 29.0f * scale,
            dataset_icon_min.y + 29.0f * scale
        ),
        1.08f * scale
    );
    const char* dataset_name =
        state.dataset.active_dataset.empty()
            ? "尚未加载数据"
            : state.dataset.active_dataset.c_str();
    const float continue_width = 140.0f * scale;
    const float continue_height = 42.0f * scale;
    const float continue_left = compact_current
        ? min.x + 16.0f * scale
        : min.x + width - continue_width - 16.0f * scale;
    const float text_clip_right = compact_current
        ? min.x + width - 16.0f * scale
        : continue_left - 12.0f * scale;
    draw_list->PushClipRect(
        ImVec2(min.x + 94.0f * scale, y),
        ImVec2(text_clip_right, y + current_height),
        true
    );
    draw_text(
        draw_list,
        17.0f * scale,
        ImVec2(
            min.x + 94.0f * scale,
            y + 22.0f * scale
        ),
        kText,
        dataset_name
    );
    const std::string summary =
        format_point_count(state.dataset.point_count) +
        "  ·  " + state.dataset.format;
    draw_text(
        draw_list,
        12.0f * scale,
        ImVec2(
            min.x + 94.0f * scale,
            y + 57.0f * scale
        ),
        kTextMuted,
        summary.c_str()
    );
    draw_list->PopClipRect();

    ImGui::SetCursorScreenPos(ImVec2(
        continue_left,
        y + (compact_current ? 94.0f : 27.0f) * scale
    ));
    ImGui::PushStyleColor(
        ImGuiCol_Button,
        ImGui::ColorConvertU32ToFloat4(kPrimary)
    );
    ImGui::PushStyleColor(
        ImGuiCol_ButtonHovered,
        ImGui::ColorConvertU32ToFloat4(kPrimaryHover)
    );
    ImGui::PushStyleColor(
        ImGuiCol_ButtonActive,
        ImGui::ColorConvertU32ToFloat4(
            display_color(61, 143, 218)
        )
    );
    ImGui::PushStyleVar(
        ImGuiStyleVar_FrameRounding,
        6.0f * scale
    );
    if (ImGui::Button(
            "继续编辑    >",
            ImVec2(
                compact_current
                    ? width - 32.0f * scale
                    : continue_width,
                continue_height
            )
        )) {
        enter_workspace = true;
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
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
    const float panel_height = std::clamp(
        max_height,
        330.0f * scale,
        460.0f * scale
    );
    draw_list->AddRectFilled(
        min,
        ImVec2(min.x + width, min.y + panel_height),
        kRecentBg,
        9.0f * scale
    );
    draw_list->AddRect(
        min,
        ImVec2(min.x + width, min.y + panel_height),
        kPanelBorder,
        9.0f * scale,
        0,
        1.0f * scale
    );
    draw_section_label(
        draw_list,
        ImVec2(
            min.x + 18.0f * scale,
            min.y + 20.0f * scale
        ),
        scale,
        "最近使用",
        16.0f
    );
    draw_text(
        draw_list,
        12.0f * scale,
        ImVec2(
            min.x + 18.0f * scale,
            min.y + 51.0f * scale
        ),
        kTextMuted,
        "快速回到最近打开的 GeoScatter3D 项目"
    );
    const float list_top = min.y + 84.0f * scale;
    const float row_height = 82.0f * scale;
    if (state.recent_projects.empty()) {
        const ImVec2 empty_max{
            min.x + width - 14.0f * scale,
            std::min(
                min.y + panel_height - 14.0f * scale,
                list_top + 132.0f * scale
            )
        };
        draw_list->AddRectFilled(
            ImVec2(min.x + 14.0f * scale, list_top),
            empty_max,
            kPanelBg,
            6.0f * scale
        );
        draw_text(
            draw_list,
            13.0f * scale,
            ImVec2(
                min.x + 34.0f * scale,
                list_top + 34.0f * scale
            ),
            kTextMuted,
            "还没有历史项目"
        );
        draw_text(
            draw_list,
            11.0f * scale,
            ImVec2(
                min.x + 34.0f * scale,
                list_top + 64.0f * scale
            ),
            kTextFaint,
            "打开项目后会自动出现在这里"
        );
        return panel_height;
    }

    const float list_height =
        panel_height - 98.0f * scale;
    ImGui::SetCursorScreenPos(ImVec2(
        min.x + 14.0f * scale,
        list_top
    ));
    ImGui::PushStyleColor(
        ImGuiCol_ChildBg,
        ImVec4(0.0f, 0.0f, 0.0f, 0.0f)
    );
    ImGui::BeginChild(
        "##RecentProjectList",
        ImVec2(width - 28.0f * scale, list_height),
        false,
        ImGuiWindowFlags_NoBackground
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
                scale,
                i == 0
            )) {
            actions.open_project_path = entry.path.string();
        }
    }
    const std::size_t visible_slots = std::min<std::size_t>(
        state.recent_projects.size(),
        4
    );
    const std::size_t placeholder_count = 4 - visible_slots;
    const float placeholder_gap = 6.0f * scale;
    const float used_recent_height =
        static_cast<float>(state.recent_projects.size()) *
        row_height;
    const float placeholder_height = std::clamp(
        placeholder_count > 0
            ? (list_height - used_recent_height -
                static_cast<float>(placeholder_count - 1) *
                    placeholder_gap) /
                static_cast<float>(placeholder_count)
            : 0.0f,
        46.0f * scale,
        64.0f * scale
    );
    for (std::size_t i = 0; i < placeholder_count; ++i) {
        draw_recent_placeholder(
            draw_list,
            ImVec2(
                list_origin.x,
                list_origin.y +
                    used_recent_height +
                    static_cast<float>(i) *
                        (placeholder_height + placeholder_gap)
            ),
            row_width,
            placeholder_height,
            scale,
            static_cast<int>(i)
        );
    }
    const float list_content_height =
        used_recent_height +
        static_cast<float>(placeholder_count) *
            placeholder_height +
        static_cast<float>(
            placeholder_count > 0 ? placeholder_count - 1 : 0
        ) * placeholder_gap;
    ImGui::SetCursorScreenPos(ImVec2(
        list_origin.x,
        list_origin.y + list_content_height
    ));
    ImGui::Dummy(ImVec2(1.0f, 1.0f));
    ImGui::EndChild();
    ImGui::PopStyleColor();
    return panel_height;
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
    draw_welcome_backdrop(
        draw_list,
        canvas_min,
        available,
        ui_scale
    );

    const bool landscape =
        available.x >= 930.0f * ui_scale &&
        available.x > available.y * 1.10f;
    const float side_margin = std::clamp(
        available.x * 0.042f,
        24.0f * ui_scale,
        68.0f * ui_scale
    );
    const float top_margin = std::clamp(
        available.y * 0.045f,
        24.0f * ui_scale,
        48.0f * ui_scale
    );
    const float content_width = std::max(
        0.0f,
        std::min(
            available.x - side_margin * 2.0f,
            1460.0f * ui_scale
        )
    );
    const float content_left =
        canvas_min.x + (available.x - content_width) * 0.5f;
    float content_bottom = canvas_min.y + top_margin;

    const ImVec2 brand_center{
        content_left + 31.0f * ui_scale,
        canvas_min.y + top_margin + 31.0f * ui_scale
    };
    draw_brand_mark(draw_list, brand_center, state.logo_texture, ui_scale);
    draw_text(
        draw_list,
        32.0f * ui_scale,
        ImVec2(
            brand_center.x + 52.0f * ui_scale,
            brand_center.y - 25.0f * ui_scale
        ),
        kText,
        "GeoScatter3D"
    );
    draw_text(
        draw_list,
        13.0f * ui_scale,
        ImVec2(
            brand_center.x + 53.0f * ui_scale,
            brand_center.y + 17.0f * ui_scale
        ),
        display_color(92, 177, 248),
        "三维散点数据工作台"
    );

    const float section_top =
        brand_center.y + 58.0f * ui_scale;
    bool enter_workspace = false;
    if (landscape) {
        const float column_gap = std::clamp(
            content_width * 0.035f,
            28.0f * ui_scale,
            48.0f * ui_scale
        );
        const float usable_width =
            content_width - column_gap;
        const float left_width = usable_width * 0.52f;
        const float right_width =
            usable_width - left_width;
        draw_text(
            draw_list,
            30.0f * ui_scale,
            ImVec2(content_left, section_top),
            kText,
            "欢迎回来"
        );
        draw_text(
            draw_list,
            13.0f * ui_scale,
            ImVec2(
                content_left,
                section_top + 45.0f * ui_scale
            ),
            kTextMuted,
            "打开项目，或从 DAT / CSV 开始新的分析。"
        );
        const float load_height = draw_load_section(
            state,
            actions,
            ImVec2(
                content_left,
                section_top + 62.0f * ui_scale
            ),
            left_width,
            true,
            ui_scale,
            enter_workspace
        );
        const float left_visual_height =
            62.0f * ui_scale + load_height;
        const float available_right_height = std::max(
            330.0f * ui_scale,
            available.y -
                (section_top - canvas_min.y) -
                24.0f * ui_scale
        );
        const float recent_target_height = std::min(
            left_visual_height * 0.86f,
            available_right_height
        );
        const float recent_height = draw_recent_section(
            state,
            actions,
            ImVec2(
                content_left + left_width + column_gap,
                section_top
            ),
            right_width,
            recent_target_height,
            ui_scale
        );
        content_bottom = std::max(
            section_top + 62.0f * ui_scale + load_height,
            section_top + recent_height
        );
    } else {
        draw_text(
            draw_list,
            29.0f * ui_scale,
            ImVec2(content_left, section_top),
            kText,
            "欢迎回来"
        );
        draw_text(
            draw_list,
            12.5f * ui_scale,
            ImVec2(
                content_left,
                section_top + 42.0f * ui_scale
            ),
            kTextMuted,
            "打开项目，或从 DAT / CSV 开始新的分析。"
        );
        const float load_height = draw_load_section(
            state,
            actions,
            ImVec2(
                content_left,
                section_top + 62.0f * ui_scale
            ),
            content_width,
            false,
            ui_scale,
            enter_workspace
        );
        const float recent_top =
            section_top +
            62.0f * ui_scale +
            load_height +
            28.0f * ui_scale;
        const float recent_height = draw_recent_section(
            state,
            actions,
            ImVec2(content_left, recent_top),
            content_width,
            360.0f * ui_scale,
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
