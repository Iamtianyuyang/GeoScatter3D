#include "ui/WelcomePage.hpp"

#include "gui/UiFonts.hpp"
#include "platform/NativeFileDialog.hpp"
#include "ui/UiPalette.hpp"
#include "ui/Widgets.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <string>

namespace gs3d::ui {

namespace {

ImU32 rgb(
    int red,
    int green,
    int blue,
    int alpha = 255
) {
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

const ImU32 kBackground = to_u32(palette::kBg);
const ImU32 kSurface = to_u32(palette::kSurface);
const ImU32 kSurfaceHover = to_u32(palette::kSurfaceHover);
const ImU32 kBorder = to_u32(palette::kBorder);
const ImU32 kText = to_u32(palette::kText);
const ImU32 kMuted = to_u32(palette::kTextDim);
const ImU32 kFaint = to_u32(palette::kTextFaint);
const ImU32 kBlue = to_u32(palette::kAccent);
const ImU32 kKeywordBlue = to_u32(palette::kVarBlue);
const ImU32 kTypeGreen = to_u32(palette::kGreen);
const ImU32 kError = to_u32(palette::kRed);
const ImU32 kBrandLight = to_u32(palette::kText);
const ImU32 kScatterBlue = to_u32(palette::kAccent);

std::string g_error_message;

ImFont* regular_font()
{
    const auto& fonts = gs3d::gui::ui_fonts();
    return fonts.regular != nullptr ? fonts.regular : ImGui::GetFont();
}

ImFont* medium_font()
{
    const auto& fonts = gs3d::gui::ui_fonts();
    return fonts.medium != nullptr ? fonts.medium : regular_font();
}

ImFont* bold_font()
{
    const auto& fonts = gs3d::gui::ui_fonts();
    return fonts.bold != nullptr ? fonts.bold : medium_font();
}

void draw_text(
    ImDrawList* draw_list,
    ImFont* font,
    float size,
    const ImVec2& position,
    ImU32 color,
    const char* text
) {
    draw_list->AddText(
        font,
        size,
        position,
        color,
        text
    );
}

std::string project_display_name(
    const std::filesystem::path& path
) {
    if (path.empty()) {
        return {};
    }
    const auto name = path.filename().string();
    return name.empty() ? path.string() : name;
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

bool is_illegal_filename_char(char c)
{
    static constexpr char kIllegalChars[] =
        "/\\:*?\"<>|";
    for (const char illegal : kIllegalChars) {
        if (c == illegal) {
            return true;
        }
    }
    return false;
}

void filter_illegal_chars(char* buffer)
{
    char* write = buffer;
    for (const char* read = buffer; *read != '\0'; ++read) {
        if (!is_illegal_filename_char(*read)) {
            *write = *read;
            ++write;
        }
    }
    *write = '\0';
}

bool has_illegal_chars(const char* buffer)
{
    for (const char* p = buffer; *p != '\0'; ++p) {
        if (is_illegal_filename_char(*p)) {
            return true;
        }
    }
    return false;
}

bool project_bundle_exists(
    const std::filesystem::path& parent_dir,
    const std::string& project_name
) {
    std::error_code ec;
    const auto bundle_dir =
        parent_dir / (project_name + ".gs3d.bundle");
    return std::filesystem::exists(bundle_dir, ec);
}

bool is_valid_project(const std::filesystem::path& path)
{
    std::error_code ec;
    return std::filesystem::is_directory(path, ec) &&
        std::filesystem::is_regular_file(path / "manifest.toml", ec);
}

WelcomePageAction open_project()
{
    const auto result =
        gs3d::platform::choose_project_directory();
    if (!result.error.empty()) {
        g_error_message = result.error;
        return {};
    }
    if (!result.path.has_value()) {
        return {};
    }
    if (!is_valid_project(*result.path)) {
        g_error_message =
            "请选择包含 manifest.toml 的 .gs3d.bundle 项目目录";
        return {};
    }
    g_error_message.clear();
    return {
        .kind = WelcomePageActionKind::OpenProject,
        .path = *result.path
    };
}

void draw_folder_icon(
    ImDrawList* draw_list,
    const ImVec2& center,
    float scale,
    ImU32 color
) {
    const float x = center.x;
    const float y = center.y;
    const float thickness = 1.6f * scale;
    const ImVec2 points[] = {
        {x - 10.0f * scale, y - 6.0f * scale},
        {x - 2.0f * scale, y - 6.0f * scale},
        {x + 1.5f * scale, y - 2.5f * scale},
        {x + 11.0f * scale, y - 2.5f * scale},
        {x + 9.0f * scale, y + 7.0f * scale},
        {x - 10.0f * scale, y + 7.0f * scale}
    };
    draw_list->AddPolyline(
        points,
        static_cast<int>(std::size(points)),
        color,
        ImDrawFlags_Closed,
        thickness
    );
}

void draw_plus_icon(
    ImDrawList* draw_list,
    const ImVec2& center,
    float scale,
    ImU32 color
) {
    const float radius = 10.0f * scale;
    draw_list->AddRect(
        ImVec2(center.x - radius, center.y - radius),
        ImVec2(center.x + radius, center.y + radius),
        color,
        3.0f * scale,
        0,
        1.5f * scale
    );
    draw_list->AddLine(
        ImVec2(center.x - 5.0f * scale, center.y),
        ImVec2(center.x + 5.0f * scale, center.y),
        color,
        1.5f * scale
    );
    draw_list->AddLine(
        ImVec2(center.x, center.y - 5.0f * scale),
        ImVec2(center.x, center.y + 5.0f * scale),
        color,
        1.5f * scale
    );
}

enum class CardIcon {
    NewProject,
    Folder,
    Continue
};

bool draw_action_card(
    const char* id,
    const ImVec2& min,
    float width,
    float height,
    float scale,
    const char* title,
    const char* subtitle,
    CardIcon icon,
    bool highlighted = false
) {
    ImGui::SetCursorScreenPos(min);
    ImGui::InvisibleButton(id, ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 max{min.x + width, min.y + height};

    draw_list->AddRectFilled(
        min,
        max,
        hovered ? kSurfaceHover : kSurface,
        8.0f * scale
    );
    const ImU32 border_color = highlighted
        ? to_u32(palette::kAccent, 80)
        : (hovered ? to_u32(palette::kAccent, 120) : kBorder);
    draw_list->AddRect(
        min,
        max,
        border_color,
        8.0f * scale,
        0,
        1.0f * scale
    );

    const ImVec2 icon_center{
        min.x + 28.0f * scale,
        min.y + height * 0.5f
    };
    const ImU32 icon_color = hovered ? kText : to_u32(palette::kTextFaint, 200);
    if (icon == CardIcon::NewProject) {
        draw_plus_icon(draw_list, icon_center, scale, icon_color);
    } else if (icon == CardIcon::Folder) {
        draw_folder_icon(draw_list, icon_center, scale, icon_color);
    } else if (icon == CardIcon::Continue) {
        // Play / forward arrow icon
        const float s = 8.0f * scale;
        const float t = 4.0f * scale;
        const ImU32 c = icon_color;
        draw_list->AddTriangleFilled(
            ImVec2(icon_center.x - t, icon_center.y - s),
            ImVec2(icon_center.x - t, icon_center.y + s),
            ImVec2(icon_center.x + t, icon_center.y),
            c
        );
    }

    draw_text(
        draw_list,
        medium_font(),
        19.0f * scale,
        ImVec2(min.x + 55.0f * scale, min.y + 16.0f * scale),
        kText,
        title
    );
    draw_text(
        draw_list,
        regular_font(),
        17.0f * scale,
        ImVec2(min.x + 55.0f * scale, min.y + 46.0f * scale),
        kMuted,
        subtitle
    );
    draw_text(
        draw_list,
        regular_font(),
        28.0f * scale,
        ImVec2(
            max.x - 22.0f * scale + (hovered ? 3.0f * scale : 0.0f),
            min.y + 24.0f * scale
        ),
        hovered ? kText : kMuted,
        "›"
    );
    return clicked;
}

void draw_section_title(
    ImDrawList* draw_list,
    const ImVec2& position,
    float scale,
    const char* title
) {
    draw_list->AddRectFilled(
        position,
        ImVec2(
            position.x + 4.0f * scale,
            position.y + 20.0f * scale
        ),
        kBlue,
        2.0f * scale
    );
    draw_text(
        draw_list,
        bold_font(),
        21.0f * scale,
        ImVec2(position.x + 14.5f * scale, position.y - 1.0f * scale),
        kText,
        title
    );
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
        std::filesystem::is_directory(entry.path, ec);
    const std::string id =
        "##RecentProject/" + entry.path.generic_string();
    ImGui::SetCursorScreenPos(min);
    ImGui::InvisibleButton(id.c_str(), ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked =
        available && ImGui::IsItemClicked();

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    if (hovered) {
        draw_list->AddRectFilled(
            min,
            ImVec2(min.x + width, min.y + height),
            kSurfaceHover,
            5.0f * scale
        );
    }
    const ImVec2 icon_min{
        min.x + 12.0f * scale,
        min.y + (height - 34.0f * scale) * 0.5f
    };
    draw_list->AddRectFilled(
        icon_min,
        ImVec2(
            icon_min.x + 34.0f * scale,
            icon_min.y + 36.0f * scale
        ),
        kBackground,
        4.0f * scale
    );
    draw_folder_icon(
        draw_list,
        ImVec2(
            icon_min.x + 19.0f * scale,
            icon_min.y + 17.0f * scale
        ),
        0.78f * scale,
        available ? kTypeGreen : kFaint
    );

    const float text_left = min.x + 58.0f * scale;
    const float text_right = min.x + width - 90.0f * scale;
    draw_list->PushClipRect(
        ImVec2(text_left, min.y),
        ImVec2(text_right, min.y + height),
        true
    );
    const auto name = project_display_name(entry.path);
    draw_text(
        draw_list,
        medium_font(),
        17.0f * scale,
        ImVec2(text_left, min.y + 12.0f * scale),
        available ? kText : kFaint,
        name.c_str()
    );
    const auto path = entry.path.string();
    draw_text(
        draw_list,
        regular_font(),
        14.5f * scale,
        ImVec2(text_left, min.y + 36.0f * scale),
        kFaint,
        path.c_str()
    );
    draw_list->PopClipRect();

    const auto time = available
        ? recent_time_label(entry.last_opened_unix)
        : "不可用";
    const ImVec2 time_size = regular_font()->CalcTextSizeA(
        14.5f * scale,
        1000.0f,
        0.0f,
        time.c_str()
    );
    draw_text(
        draw_list,
        regular_font(),
        14.5f * scale,
        ImVec2(
            min.x + width - time_size.x - 10.0f * scale,
            min.y + 11.0f * scale
        ),
        available ? kMuted : kError,
        time.c_str()
    );
    draw_list->AddLine(
        ImVec2(min.x, min.y + height),
        ImVec2(min.x + width, min.y + height),
        to_u32(palette::kBorder, 180),
        1.0f
    );
    if (hovered) {
        ImGui::SetTooltip("%s", path.c_str());
    }
    return clicked;
}

WelcomePageAction draw_new_project_dialog(
    NewProjectDialogState& dialog,
    float scale
) {
    WelcomePageAction action;
    if (!dialog.active) {
        return action;
    }

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(
        ImVec2(580.0f * scale, 0.0f),
        ImGuiCond_Appearing
    );

    // ── Dialog style: Modern SaaS light theme ──
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertU32ToFloat4(kBackground));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kText));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(36.0f * scale, 32.0f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f * scale, 9.0f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12.0f * scale, 10.0f * scale));

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoTitleBar;

    if (!ImGui::BeginPopupModal("##NewProjectDialog", nullptr, flags)) {
        dialog.active = false;
        ImGui::PopStyleVar(6);
        ImGui::PopStyleColor(2);
        return action;
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    bool should_close = false;
    const ImVec2 content_min = ImGui::GetCursorScreenPos();

    // ── Title with accent bar ──
    draw_list->AddRectFilled(
        content_min,
        ImVec2(content_min.x + 4.0f * scale, content_min.y + 32.0f * scale),
        kBlue,
        2.0f * scale
    );
    draw_text(
        draw_list, bold_font(), 32.0f * scale,
        ImVec2(content_min.x + 14.0f * scale, content_min.y - 2.0f * scale),
        kText, "新建项目"
    );
    ImGui::SetCursorScreenPos(ImVec2(
        content_min.x,
        content_min.y + 40.0f * scale
    ));
    draw_text(
        draw_list, regular_font(), 17.0f * scale,
        ImGui::GetCursorScreenPos(),
        kMuted, "选择原始数据文件并命名项目"
    );
    ImGui::SetCursorScreenPos(ImVec2(
        content_min.x,
        content_min.y + 74.0f * scale
    ));

    // ── Data file section ──
    draw_text(
        draw_list, medium_font(), 19.0f * scale,
        ImGui::GetCursorScreenPos(),
        kMuted, "数据文件"
    );
    ImGui::Dummy(ImVec2(1.0f, 12.0f * scale));

    const float file_btn_width = 140.0f * scale;
    const float file_btn_start_y = ImGui::GetCursorScreenPos().y;

    // Secondary-outline button: blue border, subtle blue fill on hover
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(kSurface));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, to_u32(palette::kAccent, 25));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, to_u32(palette::kAccent, 40));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(kBlue));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f * scale);

    if (ImGui::Button("选择数据文件", ImVec2(file_btn_width, 0.0f))) {
        const auto result =
            gs3d::platform::choose_raw_data_file();
        if (!result.error.empty()) {
            dialog.error_message = result.error;
        } else if (result.path.has_value()) {
            std::error_code ec;
            if (!has_raw_extension(*result.path) ||
                !std::filesystem::is_regular_file(*result.path, ec)) {
                dialog.error_message = "请选择有效的 DAT 或 CSV 文件";
            } else {
                dialog.data_file_path = result.path->string();
                const auto stem =
                    result.path->stem().string();
                std::size_t len = std::min<std::size_t>(
                    stem.size(),
                    sizeof(dialog.project_name) - 1
                );
                std::memcpy(dialog.project_name, stem.c_str(), len);
                dialog.project_name[len] = '\0';
                dialog.error_message.clear();
                dialog.name_conflict = false;
            }
        }
    }

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);

    // File-name text aligned vertically centered with the button.
    const float file_btn_frame_h = ImGui::GetFrameHeight();
    const float file_text_size = 16.0f * scale;
    const float file_text_row_h = file_text_size * 1.4f;
    const float file_text_y =
        file_btn_start_y + (file_btn_frame_h - file_text_row_h) * 0.5f;

    ImGui::SameLine(0.0f, 16.0f * scale);
    ImGui::SetCursorScreenPos(ImVec2(
        ImGui::GetCursorScreenPos().x,
        file_text_y
    ));

    if (dialog.data_file_path.empty()) {
        draw_text(
            draw_list, regular_font(), file_text_size,
            ImGui::GetCursorScreenPos(),
            kFaint, "未选择文件"
        );
    } else {
        draw_text(
            draw_list, regular_font(), file_text_size,
            ImGui::GetCursorScreenPos(),
            kText, dialog.data_file_path.c_str()
        );
    }
    // Advance cursor past the button row.
    ImGui::SetCursorScreenPos(ImVec2(
        content_min.x,
        file_btn_start_y + file_btn_frame_h + 8.0f * scale
    ));

    // ── Section gap ──
    ImGui::Dummy(ImVec2(1.0f, 24.0f * scale));

    // ── Project name section ──
    draw_text(
        draw_list, medium_font(), 19.0f * scale,
        ImGui::GetCursorScreenPos(),
        kMuted, "项目名称"
    );
    ImGui::Dummy(ImVec2(1.0f, 12.0f * scale));

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f * scale, 12.0f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(kSurface));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(kBorder));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kText));

    const float input_width = ImGui::GetContentRegionAvail().x;
    ImGui::PushItemWidth(input_width);
    const bool name_edited = widgets::InputText(
        "##ProjectName",
        dialog.project_name,
        sizeof(dialog.project_name),
        ImGuiInputTextFlags_None
    );
    ImGui::PopItemWidth();

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(3);

    if (name_edited) {
        filter_illegal_chars(dialog.project_name);
        dialog.name_conflict = false;
    }

    // Hint
    ImGui::Dummy(ImVec2(1.0f, 8.0f * scale));
    draw_text(
        draw_list, regular_font(), 15.0f * scale,
        ImGui::GetCursorScreenPos(),
        kFaint, "项目名不能包含: / \\ : * ? \" < > |"
    );
    // Advance cursor past the hint row.
    ImGui::SetCursorScreenPos(ImVec2(
        ImGui::GetCursorScreenPos().x,
        ImGui::GetCursorScreenPos().y + 16.0f * scale
    ));

    // ── Error message (card style) ──
    if (!dialog.error_message.empty()) {
        ImGui::Dummy(ImVec2(1.0f, 10.0f * scale));
        const ImVec2 err_min = ImGui::GetCursorScreenPos();
        const ImVec2 err_text_size = regular_font()->CalcTextSizeA(
            15.0f * scale, 1000.0f, 0.0f,
            dialog.error_message.c_str()
        );
        const float err_pad = 10.0f * scale;
        const float err_height = err_text_size.y + err_pad * 2.0f;
        draw_list->AddRectFilled(
            err_min,
            ImVec2(err_min.x + input_width, err_min.y + err_height),
            to_u32(palette::kRed, 18),
            6.0f * scale
        );
        draw_list->AddRect(
            err_min,
            ImVec2(err_min.x + input_width, err_min.y + err_height),
            to_u32(palette::kRed, 80),
            6.0f * scale,
            0,
            1.0f * scale
        );
        draw_text(
            draw_list, regular_font(), 15.0f * scale,
            ImVec2(err_min.x + err_pad, err_min.y + err_pad),
            kError, dialog.error_message.c_str()
        );
        ImGui::Dummy(ImVec2(1.0f, err_height + 6.0f * scale));
    }

    // ── Bottom separator + Buttons ──
    {
        const float btn_w = 130.0f * scale;
        const float btn_h = 44.0f * scale;
        const float btn_spacing = 16.0f * scale;
        const float btn_count = dialog.name_conflict ? 2.0f : 2.0f;
        const float btn_total_w = btn_count * btn_w + (btn_count - 1.0f) * btn_spacing;
        const float btn_x = content_min.x + ImGui::GetContentRegionAvail().x - btn_total_w;

        // Top separator line
        ImGui::Dummy(ImVec2(1.0f, 24.0f * scale));
        const float sep_y = ImGui::GetCursorScreenPos().y;
        draw_list->AddLine(
            ImVec2(content_min.x, sep_y),
            ImVec2(content_min.x + ImGui::GetContentRegionAvail().x, sep_y),
            to_u32(palette::kBorder, 160),
            1.0f * scale
        );
        ImGui::Dummy(ImVec2(1.0f, 20.0f * scale));

        ImGui::SetCursorScreenPos(ImVec2(btn_x, ImGui::GetCursorScreenPos().y));

        if (dialog.name_conflict) {
            // ── "重命名" secondary button ──
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(kSurface));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(kSurfaceHover));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::ColorConvertU32ToFloat4(kBackground));
            ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(kBorder));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kText));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f * scale);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
            if (ImGui::Button("重命名", ImVec2(btn_w, btn_h))) {
                dialog.name_conflict = false;
                dialog.error_message.clear();
            }
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(5);

            ImGui::SameLine(0.0f, btn_spacing);

            // ── "覆盖" danger button with bottom shadow ──
            const ImVec2 cover_min = ImGui::GetCursorScreenPos();
            const ImVec2 cover_max{cover_min.x + btn_w, cover_min.y + btn_h};
            // Bottom shadow
            draw_list->AddRectFilled(
                ImVec2(cover_min.x + 1.0f * scale, cover_max.y),
                ImVec2(cover_max.x - 1.0f * scale, cover_max.y + 2.5f * scale),
                IM_COL32(180, 30, 30, 80),
                4.0f * scale
            );
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(kError));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, srgb_vec4(220, 50, 50));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, srgb_vec4(180, 30, 30));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kBrandLight));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f * scale);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
            if (ImGui::Button("覆盖", ImVec2(btn_w, btn_h))) {
                action.kind = WelcomePageActionKind::NewProject;
                action.path = dialog.data_file_path;
                action.project_name = dialog.project_name;
                dialog.active = false;
                should_close = true;
            }
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(4);
        } else {
            // ── "取消" secondary outline button ──
            const ImVec2 cancel_min = ImGui::GetCursorScreenPos();
            const ImVec2 cancel_max{cancel_min.x + btn_w, cancel_min.y + btn_h};
            // Bottom subtle shadow
            draw_list->AddRectFilled(
                ImVec2(cancel_min.x + 1.0f * scale, cancel_max.y),
                ImVec2(cancel_max.x - 1.0f * scale, cancel_max.y + 2.0f * scale),
                to_u32(palette::kBorder, 120),
                4.0f * scale
            );
            // Hover detection for custom drawing
            ImGui::SetCursorScreenPos(cancel_min);
            ImGui::InvisibleButton("##CancelBtn", ImVec2(btn_w, btn_h));
            const bool cancel_hovered = ImGui::IsItemHovered();
            const bool cancel_active = ImGui::IsItemActive();
            // Background + border
            const ImU32 cancel_bg = cancel_active
                ? to_u32(palette::kSurfaceHover, 255)
                : (cancel_hovered ? to_u32(palette::kAccent, 20) : to_u32(palette::kSurface, 255));
            const ImU32 cancel_border = cancel_hovered ? to_u32(palette::kAccent, 160) : kBorder;
            draw_list->AddRectFilled(cancel_min, cancel_max, cancel_bg, 8.0f * scale);
            draw_list->AddRect(cancel_min, cancel_max, cancel_border, 8.0f * scale, 0, 1.0f * scale);
            // Text centered
            const ImVec2 cancel_text_size = medium_font()->CalcTextSizeA(
                16.0f * scale, 1000.0f, 0.0f, "取消"
            );
            draw_text(
                draw_list, medium_font(), 16.0f * scale,
                ImVec2(
                    cancel_min.x + (btn_w - cancel_text_size.x) * 0.5f,
                    cancel_min.y + (btn_h - cancel_text_size.y) * 0.5f
                ),
                cancel_hovered ? kBlue : kText,
                "取消"
            );
            if (ImGui::IsItemClicked()) {
                dialog.active = false;
                dialog.error_message.clear();
                should_close = true;
            }

            ImGui::SameLine(0.0f, btn_spacing);

            // ── "确定" primary blue button with bottom shadow ──
            const ImVec2 ok_min = ImGui::GetCursorScreenPos();
            const ImVec2 ok_max{ok_min.x + btn_w, ok_min.y + btn_h};
            // Bottom shadow (darker blue)
            draw_list->AddRectFilled(
                ImVec2(ok_min.x + 1.0f * scale, ok_max.y),
                ImVec2(ok_max.x - 1.0f * scale, ok_max.y + 2.5f * scale),
                IM_COL32(11, 78, 203, 100),
                4.0f * scale
            );
            // Hover detection
            ImGui::SetCursorScreenPos(ok_min);
            ImGui::InvisibleButton("##OkBtn", ImVec2(btn_w, btn_h));
            const bool ok_hovered = ImGui::IsItemHovered();
            const bool ok_active = ImGui::IsItemActive();
            // Blue background with matching 8px rounding
            const ImU32 ok_bg = ok_active
                ? IM_COL32(0, 75, 210, 255)
                : (ok_hovered ? IM_COL32(20, 105, 255, 255) : IM_COL32(15, 98, 254, 255));
            draw_list->AddRectFilled(ok_min, ok_max, ok_bg, 8.0f * scale);
            // Subtle inner highlight at top
            draw_list->AddRectFilled(
                ImVec2(ok_min.x, ok_min.y),
                ImVec2(ok_max.x, ok_min.y + 1.5f * scale),
                IM_COL32(80, 150, 255, 60),
                8.0f * scale
            );
            // Text centered
            const ImVec2 ok_text_size = medium_font()->CalcTextSizeA(
                16.0f * scale, 1000.0f, 0.0f, "确定"
            );
            draw_text(
                draw_list, medium_font(), 16.0f * scale,
                ImVec2(
                    ok_min.x + (btn_w - ok_text_size.x) * 0.5f,
                    ok_min.y + (btn_h - ok_text_size.y) * 0.5f - 1.0f * scale
                ),
                IM_COL32(255, 255, 255, 255),
                "确定"
            );
            if (ImGui::IsItemClicked()) {
                const std::string name(dialog.project_name);
                bool valid = true;
                dialog.error_message.clear();

                if (name.empty() || name.find_first_not_of(" \t") == std::string::npos) {
                    dialog.error_message = "项目名称不能为空";
                    valid = false;
                } else if (dialog.data_file_path.empty()) {
                    dialog.error_message = "请先选择数据文件";
                    valid = false;
                } else if (has_illegal_chars(dialog.project_name)) {
                    dialog.error_message = "项目名包含非法字符";
                    valid = false;
                }

                if (valid) {
                    std::filesystem::path data_path(dialog.data_file_path);
                    if (project_bundle_exists(
                            data_path.parent_path(),
                            name
                        )) {
                        dialog.name_conflict = true;
                        dialog.error_message =
                            "项目 \"" + name + ".gs3d.bundle\" 已存在，"
                            "覆盖还是重命名？";
                    } else {
                        action.kind = WelcomePageActionKind::NewProject;
                        action.path = dialog.data_file_path;
                        action.project_name = dialog.project_name;
                        dialog.active = false;
                        should_close = true;
                    }
                }
            }
        }

    if (should_close) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();

    ImGui::PopStyleVar(6);
    ImGui::PopStyleColor(2);

    } // button block

    return action;
}

WelcomePageAction draw_start_panel(
    WelcomePageModel& model,
    const ImVec2& min,
    float width,
    float scale
) {
    WelcomePageAction action;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    float y = min.y;
    draw_section_title(draw_list, ImVec2(min.x, y), scale, "快速开始");
    y += 36.0f * scale;

    const float card_height = 84.0f * scale;
    const float gap = 12.0f * scale;
    if (draw_action_card(
            "##NewProject",
            ImVec2(min.x, y),
            width,
            card_height,
            scale,
            "新建项目",
            "导入 DAT / CSV 数据创建项目",
            CardIcon::NewProject
        )) {
        model.new_project_dialog = NewProjectDialogState{};
        model.new_project_dialog.active = true;
        model.new_project_dialog.should_open = true;
    }
    y += card_height + gap;
    if (draw_action_card(
            "##OpenProject",
            ImVec2(min.x, y),
            width,
            card_height,
            scale,
            "打开项目",
            "加载 .gs3d.bundle 项目",
            CardIcon::Folder
        )) {
        action = open_project();
    }
    y += card_height + gap;

    std::error_code ec;
    const bool has_current =
        !model.current_path.empty() &&
        std::filesystem::exists(model.current_path, ec);
    if (has_current) {
        const auto name = project_display_name(model.current_path);
        if (draw_action_card(
                "##ContinueCurrent",
                ImVec2(min.x, y),
                width,
                card_height,
                scale,
                "继续编辑",
                name.c_str(),
                CardIcon::Continue,
                true
            )) {
            action.kind = WelcomePageActionKind::ContinueCurrent;
        }
        y += card_height;
    }

    if (!g_error_message.empty()) {
        y += 12.0f * scale;
        draw_text(
            draw_list,
            regular_font(),
            14.5f * scale,
            ImVec2(min.x, y),
            kError,
            g_error_message.c_str()
        );
        y += 18.0f * scale;
    }

    ImGui::SetCursorScreenPos(ImVec2(min.x, y));
    ImGui::Dummy(ImVec2(width, 1.0f));
    return action;
}

WelcomePageAction draw_recent_panel(
    const WelcomePageModel& model,
    const ImVec2& min,
    float width,
    float scale
) {
    WelcomePageAction action;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_section_title(
        draw_list,
        min,
        scale,
        "最近使用"
    );

    const char* clear_label = "清除历史";
    const ImVec2 clear_size = regular_font()->CalcTextSizeA(
        14.5f * scale,
        1000.0f,
        0.0f,
        clear_label
    );
    const ImVec2 clear_min{
        min.x + width - clear_size.x - 8.0f * scale,
        min.y - 3.0f * scale
    };
    ImGui::SetCursorScreenPos(clear_min);
    ImGui::InvisibleButton(
        "##ClearRecent",
        ImVec2(
            clear_size.x + 8.0f * scale,
            24.0f * scale
        )
    );
    const bool clear_hovered = ImGui::IsItemHovered();
    if (clear_hovered) {
        draw_list->AddRectFilled(
            clear_min,
            ImVec2(
                clear_min.x + clear_size.x + 8.0f * scale,
                clear_min.y + 24.0f * scale
            ),
            kSurfaceHover,
            4.0f * scale
        );
    }
    draw_text(
        draw_list,
        regular_font(),
        17.0f * scale,
        ImVec2(clear_min.x + 4.0f * scale, clear_min.y + 4.0f * scale),
        clear_hovered ? kText : kMuted,
        clear_label
    );
    if (ImGui::IsItemClicked()) {
        action.kind = WelcomePageActionKind::ClearRecent;
    }

    float y = min.y + 36.0f * scale;
    const float card_pad = 8.0f * scale;
    if (model.recent_projects.empty()) {
        const float empty_height = 160.0f * scale;
        draw_list->AddRectFilled(
            ImVec2(min.x, y),
            ImVec2(min.x + width, y + empty_height),
            kSurface,
            8.0f * scale
        );
        // Decorative point-cloud illustration for empty state
        {
            const float cx = min.x + width * 0.5f;
            const float cy = y + empty_height * 0.35f;
            constexpr int kDecorPoints = 30;
            std::srand(static_cast<unsigned>(std::hash<std::string>{}("empty_state_deco")));
            for (int i = 0; i < kDecorPoints; ++i) {
                const float dx = (static_cast<float>(std::rand()) / RAND_MAX - 0.5f) * width * 0.7f;
                const float dy = (static_cast<float>(std::rand()) / RAND_MAX - 0.5f) * empty_height * 0.4f;
                const float r = (1.0f + static_cast<float>(std::rand()) / RAND_MAX * 2.5f) * scale;
                const int alpha = 15 + static_cast<int>(static_cast<float>(std::rand()) / RAND_MAX * 50);
                draw_list->AddCircleFilled(
                    ImVec2(cx + dx, cy + dy), r,
                    IM_COL32(15, 98, 254, alpha)
                );
            }
        }
        draw_text(
            draw_list,
            medium_font(),
            19.0f * scale,
            ImVec2(min.x + width * 0.5f - 75.0f * scale, y + empty_height * 0.62f),
            kMuted,
            "还没有历史项目"
        );
        draw_text(
            draw_list,
            regular_font(),
            17.0f * scale,
            ImVec2(min.x + width * 0.5f - 85.0f * scale, y + empty_height * 0.75f),
            kFaint,
            "打开项目后会自动出现在这里"
        );
        y += empty_height;
    } else {
        const float row_height = 68.0f * scale;
        const std::size_t count = std::min<std::size_t>(
            model.recent_projects.size(),
            6
        );
        const float list_height = count * row_height + card_pad * 2.0f;
        draw_list->AddRectFilled(
            ImVec2(min.x, y),
            ImVec2(min.x + width, y + list_height),
            kSurface,
            8.0f * scale
        );
        y += card_pad;
        for (std::size_t index = 0; index < count; ++index) {
            const auto& entry = model.recent_projects[index];
            if (draw_recent_row(
                    entry,
                    ImVec2(min.x + card_pad, y),
                    width - card_pad * 2.0f,
                    row_height,
                    scale
                )) {
                action = {
                    .kind = WelcomePageActionKind::OpenProject,
                    .path = entry.path
                };
            }
            y += row_height;
        }
        y += card_pad;
    }
    ImGui::SetCursorScreenPos(ImVec2(min.x, y));
    ImGui::Dummy(ImVec2(width, 1.0f));
    return action;
}

} // namespace

namespace {

struct HeroParticle {
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    float radius = 0.0f;
    int alpha = 0;
};

static std::vector<HeroParticle> g_hero_particles;
static bool g_hero_particles_init = false;

void init_hero_particles(float width, float height, float scale)
{
    if (g_hero_particles_init) return;
    g_hero_particles.clear();
    constexpr int kParticleCount = 60;
    g_hero_particles.reserve(kParticleCount);
    for (int i = 0; i < kParticleCount; ++i) {
        HeroParticle p;
        p.x = static_cast<float>(std::rand()) / RAND_MAX * width;
        p.y = static_cast<float>(std::rand()) / RAND_MAX * height;
        p.vx = (static_cast<float>(std::rand()) / RAND_MAX - 0.5f) * 0.6f * scale;
        p.vy = (static_cast<float>(std::rand()) / RAND_MAX - 0.5f) * 0.6f * scale;
        p.radius = (1.5f + static_cast<float>(std::rand()) / RAND_MAX * 3.0f) * scale;
        p.alpha = 30 + static_cast<int>(static_cast<float>(std::rand()) / RAND_MAX * 80);
        g_hero_particles.push_back(p);
    }
    g_hero_particles_init = true;
}

void draw_hero_particles(
    ImDrawList* draw_list,
    const ImVec2& min,
    const ImVec2& max,
    float scale
)
{
    const float w = max.x - min.x;
    const float h = max.y - min.y;
    init_hero_particles(w, h, scale);
    const ImU32 particle_color = IM_COL32(15, 98, 254, 255);
    for (auto& p : g_hero_particles) {
        p.x += p.vx;
        p.y += p.vy;
        if (p.x < 0.0f) p.x = w;
        if (p.x > w) p.x = 0.0f;
        if (p.y < 0.0f) p.y = h;
        if (p.y > h) p.y = 0.0f;
        draw_list->AddCircleFilled(
            ImVec2(min.x + p.x, min.y + p.y),
            p.radius,
            IM_COL32(15, 98, 254, p.alpha)
        );
    }
}

void draw_brand(
    ImDrawList* draw_list,
    const WelcomePageModel& model,
    const ImVec2& min,
    float width,
    float scale
) {
    const float icon_size = 68.0f * scale;
    const float hero_height = 150.0f * scale;
    const ImVec2 hero_max{min.x + width, min.y + hero_height};

    // Particle animation behind brand (transparent background)
    draw_hero_particles(draw_list, min, hero_max, scale);

    const float left_pad = 16.0f * scale;
    const float top_pad = 18.0f * scale;
    const ImVec2 logo_min{min.x + left_pad, min.y + top_pad};
    const ImVec2 logo_max{logo_min.x + icon_size, logo_min.y + icon_size};
    if (model.logo_texture != VK_NULL_HANDLE) {
        const ImTextureID texture = static_cast<ImTextureID>(
            reinterpret_cast<ImU64>(model.logo_texture)
        );
        draw_list->AddImage(
            texture,
            logo_min,
            logo_max
        );
    } else {
        // Blue gradient fallback matching HTML prototype (#0F62FE -> #6B9BFF)
        draw_list->AddRectFilledMultiColor(
            logo_min,
            logo_max,
            IM_COL32(15, 98, 254, 255),
            IM_COL32(107, 155, 255, 255),
            IM_COL32(107, 155, 255, 255),
            IM_COL32(15, 98, 254, 255)
        );
        const float g_size = 30.0f * scale;
        const ImVec2 g_pos{
            logo_min.x + (icon_size - g_size) * 0.5f,
            logo_min.y + (icon_size - g_size) * 0.5f - 2.0f * scale
        };
        draw_text(
            draw_list,
            bold_font(),
            g_size,
            g_pos,
            IM_COL32(255, 255, 255, 255),
            "G"
        );
    }

    const float brand_size = 42.0f * scale;
    const ImVec2 text_pos{
        logo_max.x + 18.0f * scale,
        logo_min.y + 2.0f * scale
    };
    draw_text(
        draw_list,
        bold_font(),
        brand_size,
        text_pos,
        kBrandLight,
        "Geo"
    );
    const float geo_width = bold_font()->CalcTextSizeA(
        brand_size,
        1000.0f,
        0.0f,
        "Geo"
    ).x;
    draw_text(
        draw_list,
        bold_font(),
        brand_size,
        ImVec2(text_pos.x + geo_width, text_pos.y),
        kScatterBlue,
        "Scatter"
    );
    const float scatter_width = bold_font()->CalcTextSizeA(
        brand_size,
        1000.0f,
        0.0f,
        "Scatter"
    ).x;
    draw_text(
        draw_list,
        bold_font(),
        brand_size,
        ImVec2(text_pos.x + geo_width + scatter_width, text_pos.y),
        kBrandLight,
        "3D"
    );
    draw_text(
        draw_list,
        regular_font(),
        18.0f * scale,
        ImVec2(text_pos.x + 1.0f * scale, text_pos.y + brand_size + 8.0f * scale),
        kMuted,
        "三维散点数据工作台"
    );
}

} // namespace

WelcomePageAction draw_welcome_page(
    WelcomePageModel& model,
    float ui_scale
) {
    WelcomePageAction action;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleColor(
        ImGuiCol_WindowBg,
        ImGui::ColorConvertU32ToFloat4(kBackground)
    );
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(0.0f, 0.0f)
    );
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (!ImGui::Begin("##StandaloneWelcome", nullptr, flags)) {
        ImGui::End();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor();
        return action;
    }

    const ImVec2 canvas_min = ImGui::GetWindowPos();
    const ImVec2 available = ImGui::GetWindowSize();
    // Original application scaling rule: use the monitor-PPI-derived scale
    // directly, without welcome-page-specific caps or window multipliers.
    const float scale = std::max(ui_scale, 0.8f);
    const float side_margin = std::clamp(
        available.x * 0.055f,
        24.0f * scale,
        64.0f * scale
    );
    const float top_margin = std::clamp(
        available.y * 0.055f,
        24.0f * scale,
        48.0f * scale
    );
    const float content_width = std::min(
        std::max(0.0f, available.x - side_margin * 2.0f),
        980.0f * scale
    );
    const float content_left =
        canvas_min.x + (available.x - content_width) * 0.5f;
    const ImVec2 content_min{
        content_left,
        canvas_min.y + top_margin
    };

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_brand(draw_list, model, content_min, content_width, scale);

    const float hero_height = 150.0f * scale;
    const ImVec2 body_min{
        content_min.x,
        content_min.y + hero_height
    };
    const float body_height =
        available.y - (body_min.y - canvas_min.y) - 20.0f * scale;
    const bool landscape =
        content_width >= 760.0f * scale &&
        available.x > available.y * 1.05f;
    const ImGuiWindowFlags body_flags = landscape
        ? ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse
        : ImGuiWindowFlags_None;
    ImGui::SetCursorScreenPos(body_min);
    ImGui::PushStyleColor(
        ImGuiCol_ChildBg,
        ImGui::ColorConvertU32ToFloat4(kBackground)
    );
    ImGui::PushStyleColor(
        ImGuiCol_ScrollbarBg,
        ImGui::ColorConvertU32ToFloat4(kBackground)
    );
    ImGui::PushStyleColor(
        ImGuiCol_ScrollbarGrab,
        ImGui::ColorConvertU32ToFloat4(kBorder)
    );
    ImGui::PushStyleColor(
        ImGuiCol_ScrollbarGrabHovered,
        ImGui::ColorConvertU32ToFloat4(kMuted)
    );
    ImGui::BeginChild(
        "##WelcomeScrollableContent",
        ImVec2(content_width, std::max(body_height, 80.0f * scale)),
        false,
        body_flags
    );
    const ImVec2 child_origin = ImGui::GetCursorScreenPos();
    const float child_width = ImGui::GetContentRegionAvail().x;

    if (landscape) {
        const float gap = 36.0f * scale;
        const float left_width = std::clamp(
            child_width * 0.38f,
            300.0f * scale,
            380.0f * scale
        );
        const float right_width =
            std::max(280.0f * scale, child_width - left_width - gap);
        action = draw_start_panel(
            model,
            child_origin,
            left_width,
            scale
        );
        const auto recent_action = draw_recent_panel(
            model,
            ImVec2(child_origin.x + left_width + gap, child_origin.y),
            right_width,
            scale
        );
        if (recent_action.kind != WelcomePageActionKind::None) {
            action = recent_action;
        }
        ImGui::SetCursorScreenPos(ImVec2(
            child_origin.x,
            child_origin.y + 540.0f * scale
        ));
        ImGui::Dummy(ImVec2(child_width, 1.0f));
    } else {
        action = draw_start_panel(
            model,
            child_origin,
            child_width,
            scale
        );
        const ImVec2 recent_min{
            child_origin.x,
            ImGui::GetCursorScreenPos().y + 36.0f * scale
        };
        const auto recent_action = draw_recent_panel(
            model,
            recent_min,
            child_width,
            scale
        );
        if (recent_action.kind != WelcomePageActionKind::None) {
            action = recent_action;
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(4);

    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();

    // Render the new project dialog if active.
    // OpenPopup must be called at the same ID-stack level as BeginPopupModal
    // (outside any ImGui child window), otherwise the computed IDs mismatch.
    if (model.new_project_dialog.active) {
        if (model.new_project_dialog.should_open) {
            model.new_project_dialog.should_open = false;
            ImGui::OpenPopup("##NewProjectDialog");
        }
        const auto dialog_action = draw_new_project_dialog(
            model.new_project_dialog,
            scale
        );
        if (dialog_action.kind != WelcomePageActionKind::None) {
            action = dialog_action;
        }
    }

    return action;
}

} // namespace gs3d::ui
