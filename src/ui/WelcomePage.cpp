#include "ui/WelcomePage.hpp"

#include "gui/UiFonts.hpp"
#include "platform/NativeFileDialog.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
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

const ImU32 kBackground = rgb(30, 30, 30);
const ImU32 kSurface = rgb(37, 37, 38);
const ImU32 kSurfaceHover = rgb(42, 45, 46);
const ImU32 kBorder = rgb(62, 62, 62);
const ImU32 kText = rgb(218, 218, 218);
const ImU32 kMuted = rgb(154, 154, 154);
const ImU32 kFaint = rgb(112, 112, 112);
const ImU32 kBlue = rgb(0, 122, 204);
const ImU32 kKeywordBlue = rgb(86, 156, 214);
const ImU32 kTypeGreen = rgb(78, 201, 176);
const ImU32 kError = rgb(244, 71, 71);
const ImU32 kBrandLight = rgb(230, 238, 245);
const ImU32 kScatterBlue = rgb(91, 159, 204);

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

WelcomePageAction open_raw_data()
{
    const auto result =
        gs3d::platform::choose_raw_data_file();
    if (!result.error.empty()) {
        g_error_message = result.error;
        return {};
    }
    if (!result.path.has_value()) {
        return {};
    }
    std::error_code ec;
    if (!has_raw_extension(*result.path) ||
        !std::filesystem::is_regular_file(*result.path, ec)) {
        g_error_message = "请选择有效的 DAT 或 CSV 文件";
        return {};
    }
    g_error_message.clear();
    return {
        .kind = WelcomePageActionKind::OpenRawData,
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

void draw_data_icon(
    ImDrawList* draw_list,
    const ImVec2& center,
    float scale,
    ImU32 color
) {
    const float rx = 9.0f * scale;
    const float ry = 3.4f * scale;
    const float top = center.y - 6.0f * scale;
    const float bottom = center.y + 6.0f * scale;
    draw_list->AddEllipse(
        ImVec2(center.x, top),
        ImVec2(rx, ry),
        color,
        0.0f,
        20,
        1.5f * scale
    );
    draw_list->AddEllipse(
        ImVec2(center.x, bottom),
        ImVec2(rx, ry),
        color,
        0.0f,
        20,
        1.5f * scale
    );
    draw_list->AddLine(
        ImVec2(center.x - rx, top),
        ImVec2(center.x - rx, bottom),
        color,
        1.5f * scale
    );
    draw_list->AddLine(
        ImVec2(center.x + rx, top),
        ImVec2(center.x + rx, bottom),
        color,
        1.5f * scale
    );
    draw_list->AddEllipse(
        ImVec2(center.x, center.y),
        ImVec2(rx, ry),
        rgb(180, 180, 180, 190),
        0.0f,
        20,
        1.0f * scale
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
    Data
};

bool draw_action_card(
    const char* id,
    const ImVec2& min,
    float width,
    float height,
    float scale,
    const char* title,
    const char* subtitle,
    CardIcon icon
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
        6.0f * scale
    );
    draw_list->AddRect(
        min,
        max,
        hovered ? rgb(86, 156, 214, 150) : kBorder,
        6.0f * scale,
        0,
        1.0f * scale
    );

    const ImVec2 icon_center{
        min.x + 28.0f * scale,
        min.y + height * 0.5f
    };
    const ImU32 icon_color = hovered ? kText : rgb(180, 180, 180);
    if (icon == CardIcon::NewProject) {
        draw_plus_icon(draw_list, icon_center, scale, icon_color);
    } else if (icon == CardIcon::Folder) {
        draw_folder_icon(draw_list, icon_center, scale, icon_color);
    } else {
        draw_data_icon(draw_list, icon_center, scale, icon_color);
    }

    draw_text(
        draw_list,
        medium_font(),
        14.0f * scale,
        ImVec2(min.x + 55.0f * scale, min.y + 15.0f * scale),
        kText,
        title
    );
    draw_text(
        draw_list,
        regular_font(),
        11.5f * scale,
        ImVec2(min.x + 55.0f * scale, min.y + 37.0f * scale),
        kMuted,
        subtitle
    );
    draw_text(
        draw_list,
        regular_font(),
        21.0f * scale,
        ImVec2(
            max.x - 22.0f * scale + (hovered ? 3.0f * scale : 0.0f),
            min.y + 19.0f * scale
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
            position.y + 18.0f * scale
        ),
        kBlue,
        2.0f * scale
    );
    draw_text(
        draw_list,
        bold_font(),
        16.0f * scale,
        ImVec2(position.x + 13.0f * scale, position.y - 1.0f * scale),
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
        min.x + 10.0f * scale,
        min.y + (height - 30.0f * scale) * 0.5f
    };
    draw_list->AddRectFilled(
        icon_min,
        ImVec2(
            icon_min.x + 30.0f * scale,
            icon_min.y + 30.0f * scale
        ),
        kBackground,
        4.0f * scale
    );
    draw_folder_icon(
        draw_list,
        ImVec2(
            icon_min.x + 15.0f * scale,
            icon_min.y + 15.0f * scale
        ),
        0.72f * scale,
        available ? kTypeGreen : kFaint
    );

    const float text_left = min.x + 52.0f * scale;
    const float text_right = min.x + width - 84.0f * scale;
    draw_list->PushClipRect(
        ImVec2(text_left, min.y),
        ImVec2(text_right, min.y + height),
        true
    );
    const auto name = project_display_name(entry.path);
    draw_text(
        draw_list,
        medium_font(),
        13.0f * scale,
        ImVec2(text_left, min.y + 10.0f * scale),
        available ? kText : kFaint,
        name.c_str()
    );
    const auto path = entry.path.string();
    draw_text(
        draw_list,
        regular_font(),
        10.5f * scale,
        ImVec2(text_left, min.y + 31.0f * scale),
        kFaint,
        path.c_str()
    );
    draw_list->PopClipRect();

    const auto time = available
        ? recent_time_label(entry.last_opened_unix)
        : "不可用";
    const ImVec2 time_size = regular_font()->CalcTextSizeA(
        10.5f * scale,
        1000.0f,
        0.0f,
        time.c_str()
    );
    draw_text(
        draw_list,
        regular_font(),
        10.5f * scale,
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
        rgb(62, 62, 62, 180),
        1.0f
    );
    if (hovered) {
        ImGui::SetTooltip("%s", path.c_str());
    }
    return clicked;
}

WelcomePageAction draw_start_panel(
    const WelcomePageModel& model,
    const ImVec2& min,
    float width,
    float scale
) {
    WelcomePageAction action;
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    float y = min.y;
    draw_section_title(draw_list, ImVec2(min.x, y), scale, "开始");
    y += 31.0f * scale;

    const float card_height = 67.0f * scale;
    const float gap = 10.0f * scale;
    if (draw_action_card(
            "##NewProject",
            ImVec2(min.x, y),
            width,
            card_height,
            scale,
            "新建项目",
            "选择 DAT / CSV 创建工程",
            CardIcon::NewProject
        )) {
        action = open_raw_data();
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
    if (draw_action_card(
            "##LoadRawData",
            ImVec2(min.x, y),
            width,
            card_height,
            scale,
            "加载原始数据",
            "导入 DAT / CSV 并创建项目",
            CardIcon::Data
        )) {
        action = open_raw_data();
    }
    y += card_height;

    if (!g_error_message.empty()) {
        y += 12.0f * scale;
        draw_text(
            draw_list,
            regular_font(),
            11.5f * scale,
            ImVec2(min.x, y),
            kError,
            g_error_message.c_str()
        );
        y += 18.0f * scale;
    }

    std::error_code ec;
    const bool has_current =
        !model.current_path.empty() &&
        std::filesystem::exists(model.current_path, ec);
    if (has_current) {
        y += 23.0f * scale;
        draw_section_title(
            draw_list,
            ImVec2(min.x, y),
            scale,
            "当前会话"
        );
        y += 31.0f * scale;
        const float session_height = 76.0f * scale;
        const ImVec2 session_min{min.x, y};
        draw_list->AddRectFilled(
            session_min,
            ImVec2(min.x + width, y + session_height),
            kSurface,
            6.0f * scale
        );
        draw_list->AddRect(
            session_min,
            ImVec2(min.x + width, y + session_height),
            kBorder,
            6.0f * scale
        );
        draw_data_icon(
            draw_list,
            ImVec2(
                min.x + 27.0f * scale,
                y + session_height * 0.5f
            ),
            scale,
            kTypeGreen
        );
        const auto name = project_display_name(model.current_path);
        draw_list->PushClipRect(
            ImVec2(min.x + 52.0f * scale, y),
            ImVec2(
                min.x + width - 108.0f * scale,
                y + session_height
            ),
            true
        );
        draw_text(
            draw_list,
            medium_font(),
            13.0f * scale,
            ImVec2(min.x + 52.0f * scale, y + 17.0f * scale),
            kText,
            name.c_str()
        );
        const auto path = model.current_path.string();
        draw_text(
            draw_list,
            regular_font(),
            10.5f * scale,
            ImVec2(min.x + 52.0f * scale, y + 41.0f * scale),
            kMuted,
            path.c_str()
        );
        draw_list->PopClipRect();

        const ImVec2 button_min{
            min.x + width - 94.0f * scale,
            y + 22.0f * scale
        };
        ImGui::SetCursorScreenPos(button_min);
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            ImGui::ColorConvertU32ToFloat4(kBackground)
        );
        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            ImGui::ColorConvertU32ToFloat4(kBlue)
        );
        ImGui::PushStyleColor(
            ImGuiCol_ButtonActive,
            ImGui::ColorConvertU32ToFloat4(kKeywordBlue)
        );
        if (ImGui::Button(
                "继续编辑",
                ImVec2(78.0f * scale, 31.0f * scale)
            )) {
            action.kind = WelcomePageActionKind::ContinueCurrent;
        }
        ImGui::PopStyleColor(3);
        y += session_height;
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
        11.5f * scale,
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
        11.5f * scale,
        ImVec2(clear_min.x + 4.0f * scale, clear_min.y + 4.0f * scale),
        clear_hovered ? kText : kMuted,
        clear_label
    );
    if (ImGui::IsItemClicked()) {
        action.kind = WelcomePageActionKind::ClearRecent;
    }

    float y = min.y + 32.0f * scale;
    if (model.recent_projects.empty()) {
        const float empty_height = 126.0f * scale;
        draw_list->AddRectFilled(
            ImVec2(min.x, y),
            ImVec2(min.x + width, y + empty_height),
            kSurface,
            6.0f * scale
        );
        draw_text(
            draw_list,
            medium_font(),
            13.0f * scale,
            ImVec2(min.x + 18.0f * scale, y + 31.0f * scale),
            kMuted,
            "还没有历史项目"
        );
        draw_text(
            draw_list,
            regular_font(),
            11.0f * scale,
            ImVec2(min.x + 18.0f * scale, y + 57.0f * scale),
            kFaint,
            "打开项目后会自动出现在这里"
        );
        y += empty_height;
    } else {
        const float row_height = 59.0f * scale;
        const std::size_t count = std::min<std::size_t>(
            model.recent_projects.size(),
            6
        );
        for (std::size_t index = 0; index < count; ++index) {
            const auto& entry = model.recent_projects[index];
            if (draw_recent_row(
                    entry,
                    ImVec2(min.x, y),
                    width,
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
    }
    ImGui::SetCursorScreenPos(ImVec2(min.x, y));
    ImGui::Dummy(ImVec2(width, 1.0f));
    return action;
}

void draw_brand(
    ImDrawList* draw_list,
    const WelcomePageModel& model,
    const ImVec2& min,
    float scale
) {
    const float icon_size = 54.0f * scale;
    if (model.logo_texture != VK_NULL_HANDLE) {
        const ImTextureID texture = static_cast<ImTextureID>(
            reinterpret_cast<ImU64>(model.logo_texture)
        );
        draw_list->AddImage(
            texture,
            min,
            ImVec2(min.x + icon_size, min.y + icon_size)
        );
    } else {
        draw_list->AddRectFilled(
            min,
            ImVec2(min.x + icon_size, min.y + icon_size),
            kSurface,
            8.0f * scale
        );
    }

    const float brand_size = 25.0f * scale;
    const ImVec2 text_pos{
        min.x + icon_size + 14.0f * scale,
        min.y + 3.0f * scale
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
        11.5f * scale,
        ImVec2(text_pos.x + 1.0f * scale, min.y + 37.0f * scale),
        kMuted,
        "三维散点数据工作台"
    );
}

} // namespace

WelcomePageAction draw_welcome_page(
    const WelcomePageModel& model,
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
    draw_brand(draw_list, model, content_min, scale);

    const ImVec2 body_min{
        content_min.x,
        content_min.y + 82.0f * scale
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
        const float gap = 44.0f * scale;
        const float left_width = std::clamp(
            child_width * 0.41f,
            310.0f * scale,
            390.0f * scale
        );
        const float right_width =
            std::max(240.0f * scale, child_width - left_width - gap);
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
            child_origin.y + 520.0f * scale
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
            ImGui::GetCursorScreenPos().y + 34.0f * scale
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
    return action;
}

} // namespace gs3d::ui
