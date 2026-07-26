#include "ui/ViewportCanvas.hpp"

#include "ui/Theme.hpp"
#include "ui/UiPalette.hpp"
#include "ui/UiRoot.hpp"
#include "ui/ViewportAxisTicks.hpp"
#include "ui/WorkspaceManager.hpp"

#include "gui/UiFonts.hpp"
#include "imgui.h"

#include "render/AxisGrid.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace gs3d::ui {

namespace {

// ── 画布叠加层布局常量（从 UiRoot.cpp 拆出，随画布代码走）───────────
namespace LayoutMetrics {
    // 地图轴边距（必须与 UiRoot.hpp compute_plot_rect 一致）
    constexpr float kAxisTopH  = 26.0f;
    constexpr float kAxisLeftW = 46.0f;
    constexpr float kAxisOuterPadding = 6.0f;
    // 信息 badge
    constexpr float kBadgePadX   = 8.0f;
    constexpr float kBadgePadY   = 6.0f;
    constexpr float kBadgeRound  = 4.0f;
    // 方向指示器 — 右下角，缩小并向内移
    constexpr float kGizmoRadius      = 24.0f;
    constexpr float kGizmoInsetRight  = 26.0f;
    constexpr float kGizmoInsetBottom = 26.0f;
    // 比例尺 — 左下角，放在 plot 内部
    constexpr float kScaleBarLen        = 80.0f;
    constexpr float kScaleBarInsetLeft  = 14.0f;
    constexpr float kScaleBarInsetBottom = 14.0f;
} // namespace LayoutMetrics

namespace AxisStyle {
    // 轻量坐标尺风格 — 简洁、克制、低对比。
    //
    // 必须是函数、每次调用时从 palette:: 取值：palette 会在
    // apply_theme() 里被整体重写，缓存进 static/const 的取值永远停留
    // 在启动默认主题（UiPalette.hpp 头部注释明确禁止这种缓存）。
    //
    // 轴线/刻度/标签画在 plot 外侧的窗口底色上 → 用 TextDim 系，
    // 亮暗主题都可读；网格/比例尺画在 plot 内部的暗色画布上 → 用
    // 中性灰（三套主题的 kGray 在暗画布上均可见）。
    inline ImU32 kAxisLine()   { return to_u32(palette::kTextDim, 200); }
    inline ImU32 kMajorTick()  { return to_u32(palette::kTextDim, 215); }
    inline ImU32 kMinorTick()  { return to_u32(palette::kTextDim, 150); }
    inline ImU32 kLabel()      { return to_u32(palette::kTextDim, 240); }
    inline ImU32 kGrid()       { return to_u32(palette::kGray, 38); }
    inline ImU32 kFrame()      { return to_u32(palette::kBorder, 80); }
    inline ImU32 kScaleLine()  { return to_u32(palette::kGray, 200); }
    inline ImU32 kScaleLabel() { return to_u32(palette::kGray, 215); }
    // 信息 badge
    inline ImU32 kBadgeBg()    { return to_u32(palette::kMenuBg, 185); }
    inline ImU32 kBadgeText()  { return to_u32(palette::kText, 245); }
    // 线宽
    constexpr float kAxisLineWidth   = 1.0f;
    constexpr float kFrameWidth      = 1.0f;
    constexpr float kGridWidth       = 0.5f;
    constexpr float kMajorTickWidth  = 1.0f;
    constexpr float kMinorTickWidth  = 1.0f;

    // 两级刻度
    constexpr float kMajorTickLen   = 8.0f;
    constexpr float kMinorTickLen   = 4.0f;
    constexpr int   kMajorCountMin  = 4;
    constexpr int   kMajorCountMax  = 6;

    // 标签与刻度线的间距
    constexpr float kXTickToLabel = 4.0f;
    constexpr float kYTickToLabel = 4.0f;
} // namespace AxisStyle

struct BadgeOverlay {
    char text[128];
    ImVec2 text_size;
    ImVec2 box_min;
    ImVec2 box_max;
};

bool rects_overlap(const ImVec2& a_min,
                   const ImVec2& a_max,
                   const ImVec2& b_min,
                   const ImVec2& b_max)
{
    return a_min.x < b_max.x &&
           a_max.x > b_min.x &&
           a_min.y < b_max.y &&
           a_max.y > b_min.y;
}

BadgeOverlay make_badge_overlay(
    const gs3d::app::RenderViewState& view,
    const ImVec2& plot_min,
    float ui_scale)
{
    BadgeOverlay badge{};
    std::snprintf(badge.text, sizeof(badge.text), "%llu 点  |  %.2f ms",
        static_cast<unsigned long long>(view.points_visible),
        static_cast<double>(view.frame_time_ms));
    badge.text_size = ImGui::CalcTextSize(badge.text);
    const float bx0 = plot_min.x + LayoutMetrics::kBadgePadX * ui_scale;
    const float by0 = plot_min.y + LayoutMetrics::kBadgePadY * ui_scale;
    badge.box_min = {bx0, by0};
    badge.box_max = {
        bx0 + badge.text_size.x + 16.0f * ui_scale,
        by0 + badge.text_size.y + 10.0f * ui_scale
    };
    return badge;
}

ImFont* small_font()
{
    return gs3d::gui::ui_fonts().small;
}

ImFont* axis_font()
{
    return gs3d::gui::ui_fonts().axis;
}

ImFont* medium_font()
{
    return gs3d::gui::ui_fonts().medium;
}

ImFont* status_font()
{
    return gs3d::gui::ui_fonts().status;
}

void draw_hover_property_row(
    const char* label,
    const char* value,
    const float ui_scale,
    const bool highlighted = false
) {
    ImGui::TableNextRow(0, 25.0f * ui_scale);
    if (highlighted) {
        ImGui::TableSetBgColor(
            ImGuiTableBgTarget_RowBg0,
            to_u32(palette::kAccent, 14)
        );
    }
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    if (small_font() != nullptr) {
        ImGui::PushFont(small_font());
    }
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        highlighted ? palette::kAccent : palette::kTextDim
    );
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    if (small_font() != nullptr) {
        ImGui::PopFont();
    }

    ImGui::TableSetColumnIndex(1);
    ImGui::AlignTextToFramePadding();
    if (status_font() != nullptr) {
        ImGui::PushFont(status_font());
    }
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        palette::kText
    );
    const float value_width = ImGui::CalcTextSize(value).x;
    ImGui::SetCursorPosX(std::max(
        ImGui::GetCursorPosX(),
        ImGui::GetWindowContentRegionMax().x -
            value_width -
            4.0f * ui_scale
    ));
    ImGui::TextUnformatted(value);
    ImGui::PopStyleColor();
    if (status_font() != nullptr) {
        ImGui::PopFont();
    }
}

void draw_hover_property_tooltip(
    const gs3d::app::RenderViewState& view,
    const float ui_scale
) {
    char x_value[48];
    char y_value[48];
    char primary_value[48];
    char z_value[48];
    std::snprintf(
        x_value,
        sizeof(x_value),
        "%.6f",
        static_cast<double>(view.hover_x)
    );
    std::snprintf(
        y_value,
        sizeof(y_value),
        "%.6f",
        static_cast<double>(view.hover_y)
    );
    std::snprintf(
        primary_value,
        sizeof(primary_value),
        "%.6f",
        static_cast<double>(view.hover_fold)
    );
    std::snprintf(
        z_value,
        sizeof(z_value),
        "%.6f",
        static_cast<double>(view.hover_elevation)
    );

    const bool dark = theme_tokens(active_theme()).dark;
    ImVec4 glass_bg = palette::kSurface;
    glass_bg.w = dark ? 0.96f : 0.95f;
    const ImVec4 glass_border = dark
        ? ImVec4(1.0f, 1.0f, 1.0f, 0.13f)
        : ImVec4(1.0f, 1.0f, 1.0f, 0.65f);
    ImVec4 separator = palette::kBorder;
    separator.w = dark ? 0.42f : 0.58f;

    const float card_width = 268.0f * ui_scale;
    const float card_height_estimate = 190.0f * ui_scale;
    const float cursor_gap = 14.0f * ui_scale;
    const ImGuiViewport* viewport = ImGui::GetWindowViewport();
    const ImVec2 mouse_pos = ImGui::GetIO().MousePos;
    const ImVec2 work_min = viewport->WorkPos;
    const ImVec2 work_max(
        viewport->WorkPos.x + viewport->WorkSize.x,
        viewport->WorkPos.y + viewport->WorkSize.y
    );
    ImVec2 tooltip_pos(
        mouse_pos.x + cursor_gap,
        mouse_pos.y + cursor_gap
    );
    if (tooltip_pos.x + card_width > work_max.x - cursor_gap) {
        tooltip_pos.x = mouse_pos.x - card_width - cursor_gap;
    }
    if (tooltip_pos.y + card_height_estimate > work_max.y - cursor_gap) {
        tooltip_pos.y =
            mouse_pos.y - card_height_estimate - cursor_gap;
    }
    tooltip_pos.x = std::max(work_min.x + cursor_gap, tooltip_pos.x);
    tooltip_pos.y = std::max(work_min.y + cursor_gap, tooltip_pos.y);
    ImGui::SetNextWindowPos(tooltip_pos, ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(card_width, 0.0f),
        ImVec2(card_width, 300.0f * ui_scale)
    );
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(12.0f * ui_scale, 10.0f * ui_scale)
    );
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowRounding,
        12.0f * ui_scale
    );
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(7.0f * ui_scale, 5.0f * ui_scale)
    );
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, glass_bg);
    ImGui::PushStyleColor(ImGuiCol_Border, glass_border);
    ImGui::PushStyleColor(ImGuiCol_Separator, separator);

    if (ImGui::BeginTooltip()) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const float icon_size = 22.0f * ui_scale;
        const ImVec2 icon_max(
            cursor.x + icon_size,
            cursor.y + icon_size
        );
        dl->AddRectFilled(
            cursor,
            icon_max,
            to_u32(palette::kAccent, dark ? 42 : 28),
            6.0f * ui_scale
        );
        const ImVec2 icon_center(
            cursor.x + icon_size * 0.5f,
            cursor.y + icon_size * 0.5f
        );
        const float reticle_r = 4.2f * ui_scale;
        dl->AddCircle(
            icon_center,
            reticle_r,
            to_u32(palette::kAccent, 235),
            0,
            1.2f * ui_scale
        );
        dl->AddLine(
            ImVec2(icon_center.x - 7.0f * ui_scale, icon_center.y),
            ImVec2(icon_center.x - 2.5f * ui_scale, icon_center.y),
            to_u32(palette::kAccent, 235),
            1.2f * ui_scale
        );
        dl->AddLine(
            ImVec2(icon_center.x + 2.5f * ui_scale, icon_center.y),
            ImVec2(icon_center.x + 7.0f * ui_scale, icon_center.y),
            to_u32(palette::kAccent, 235),
            1.2f * ui_scale
        );
        dl->AddLine(
            ImVec2(icon_center.x, icon_center.y - 7.0f * ui_scale),
            ImVec2(icon_center.x, icon_center.y - 2.5f * ui_scale),
            to_u32(palette::kAccent, 235),
            1.2f * ui_scale
        );
        dl->AddLine(
            ImVec2(icon_center.x, icon_center.y + 2.5f * ui_scale),
            ImVec2(icon_center.x, icon_center.y + 7.0f * ui_scale),
            to_u32(palette::kAccent, 235),
            1.2f * ui_scale
        );
        ImGui::Dummy(ImVec2(icon_size + 1.0f * ui_scale, icon_size));
        ImGui::SameLine();
        if (medium_font() != nullptr) {
            ImGui::PushFont(medium_font());
        }
        ImGui::PushStyleColor(ImGuiCol_Text, palette::kText);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("点属性");
        ImGui::PopStyleColor();
        if (medium_font() != nullptr) {
            ImGui::PopFont();
        }

        const char* action_hint =
            view.copy_feedback_frames > 0 ? "已复制" : "C 复制";
        if (small_font() != nullptr) {
            ImGui::PushFont(small_font());
        }
        const float hint_width = ImGui::CalcTextSize(action_hint).x;
        ImGui::SameLine();
        ImGui::SetCursorPosX(
            std::max(
                ImGui::GetCursorPosX() + 10.0f * ui_scale,
                ImGui::GetWindowContentRegionMax().x - hint_width
            )
        );
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            view.copy_feedback_frames > 0
                ? palette::kGreen
                : palette::kTextFaint
        );
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(action_hint);
        ImGui::PopStyleColor();
        if (small_font() != nullptr) {
            ImGui::PopFont();
        }

        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::BeginTable(
                "##HoverPropertyValues",
                2,
                ImGuiTableFlags_SizingFixedFit |
                ImGuiTableFlags_NoSavedSettings
            )) {
            ImGui::TableSetupColumn(
                "##HoverPropertyLabel",
                ImGuiTableColumnFlags_WidthFixed,
                78.0f * ui_scale
            );
            ImGui::TableSetupColumn(
                "##HoverPropertyValue",
                ImGuiTableColumnFlags_WidthStretch
            );
            draw_hover_property_row("X 坐标", x_value, ui_scale);
            draw_hover_property_row("Y 坐标", y_value, ui_scale);
            draw_hover_property_row(
                view.hover_primary_value_label.empty()
                    ? "主属性"
                    : view.hover_primary_value_label.c_str(),
                primary_value,
                ui_scale,
                true
            );
            draw_hover_property_row(
                view.hover_z_label.empty()
                    ? "高程"
                    : view.hover_z_label.c_str(),
                z_value,
                ui_scale
            );
            ImGui::EndTable();
        }
    }
    ImGui::EndTooltip();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(5);
}

void draw_mock_viewport(const ImVec2& min, const ImVec2& max)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(
        min,
        max,
        to_u32(palette::kViewportBg, 255)
    );

    const float width = max.x - min.x;
    const float height = max.y - min.y;
    for (int i = 0; i < 120; ++i) {
        const float x =
            static_cast<float>((i * 37) % 100) / 100.0f;
        const float y =
            static_cast<float>((i * 53) % 100) / 100.0f;
        const float depth =
            static_cast<float>((i * 17) % 100) / 100.0f;
        draw_list->AddCircleFilled(
            {
                min.x + width * (0.18f + x * 0.64f),
                min.y + height * (0.22f + y * 0.52f) -
                    depth * 28.0f
            },
            1.2f + depth * 1.4f,
            IM_COL32(
                72 + static_cast<int>(depth * 100.0f),
                130 + static_cast<int>(depth * 70.0f),
                215,
                220
            )
        );
    }
}

/*
 * 右下角方向指示器：缩小并内缩到 plot_rect 右下角内部，方向仍由
 * ViewerApp 每帧基于相机 view matrix 计算，所以旋转主视图时 gizmo
 * 同步旋转。
 */
void draw_orientation_gizmo(const gs3d::app::RenderViewState& view,
                            const ImVec2& plot_max,
                            float ui_scale)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const float r = LayoutMetrics::kGizmoRadius * ui_scale;
    const ImVec2 origin{
        plot_max.x - r - LayoutMetrics::kGizmoInsetRight * ui_scale,
        plot_max.y - r - LayoutMetrics::kGizmoInsetBottom * ui_scale
    };

    if (view.gizmo_axes_valid) {
        const float len = r - 1.0f * ui_scale;
        const auto draw_axis = [&](const gs3d::app::RenderViewState::GizmoAxisEnd& end,
                                   ImU32 color) {
            const float mag = std::sqrt(end.dx * end.dx + end.dy * end.dy);
            if (mag < 1.0e-6f) return;
            const ImVec2 direction(end.dx / mag, end.dy / mag);
            const ImVec2 normal(-direction.y, direction.x);
            const ImVec2 tip(
                origin.x + direction.x * len,
                origin.y + direction.y * len
            );
            const float arrow_len = 6.0f * ui_scale;
            const float arrow_half_w = 3.25f * ui_scale;
            const ImVec2 arrow_base(
                tip.x - direction.x * arrow_len,
                tip.y - direction.y * arrow_len
            );
            const ImVec2 arrow_left(
                arrow_base.x + normal.x * arrow_half_w,
                arrow_base.y + normal.y * arrow_half_w
            );
            const ImVec2 arrow_right(
                arrow_base.x - normal.x * arrow_half_w,
                arrow_base.y - normal.y * arrow_half_w
            );
            const ImU32 outline = IM_COL32(10, 14, 20, 150);
            dl->AddLine(origin, tip, outline, 4.0f * ui_scale);
            dl->AddTriangleFilled(tip, arrow_left, arrow_right, outline);
            dl->AddLine(origin, tip, color, 1.8f * ui_scale);
            dl->AddTriangleFilled(tip, arrow_left, arrow_right, color);
        };
        draw_axis(view.gizmo_x_axis, to_u32(palette::kRed, 220));
        draw_axis(view.gizmo_y_axis, to_u32(palette::kGreen, 220));
        draw_axis(view.gizmo_z_axis, to_u32(palette::kBlue, 220));
        dl->AddCircleFilled(
            origin,
            2.5f * ui_scale,
            IM_COL32(232, 238, 246, 235)
        );
    }
}

/*
 * 左下角比例尺：锚定在 plot_rect 内部左下角，与外侧地图轴分层。
 */
void draw_scale_bar(const ImVec2& plot_min,
                    const ImVec2& plot_max,
                    const char* label,
                    float ui_scale)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const float bar_w = LayoutMetrics::kScaleBarLen * ui_scale;
    const float x0 = plot_min.x + LayoutMetrics::kScaleBarInsetLeft * ui_scale;
    const float y  = plot_max.y - LayoutMetrics::kScaleBarInsetBottom * ui_scale;
    const float x1 = x0 + bar_w;
    const float tick_h = 4.0f * ui_scale;
    const float thick = 1.5f * ui_scale;

    dl->AddLine({x0, y}, {x1, y}, AxisStyle::kScaleLine(), thick);
    dl->AddLine({x0, y - tick_h}, {x0, y + 1.0f * ui_scale}, AxisStyle::kScaleLine(), thick);
    dl->AddLine({x1, y - tick_h}, {x1, y + 1.0f * ui_scale}, AxisStyle::kScaleLine(), thick);
    if (axis_font() != nullptr) {
        ImGui::PushFont(axis_font());
    }
    dl->AddText({x0, y - 18.0f * ui_scale}, AxisStyle::kScaleLabel(), label);
    if (axis_font() != nullptr) {
        ImGui::PopFont();
    }
}

/*
 * 视图叠加层：信息 badge + 比例尺 + 方向指示器，全部相对 plot_rect 布局。
 */
void draw_viewport_overlay(
    const gs3d::app::RenderViewState& view,
    const ImVec2& plot_min,
    const ImVec2& plot_max,
    float ui_scale,
    bool show_info_badge = true)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (show_info_badge) {
        const BadgeOverlay badge = make_badge_overlay(view, plot_min, ui_scale);
        dl->AddRectFilled(
            badge.box_min, badge.box_max,
            AxisStyle::kBadgeBg(), LayoutMetrics::kBadgeRound * ui_scale);
        if (small_font() != nullptr) {
            ImGui::PushFont(small_font());
        }
        dl->AddText(
            {badge.box_min.x + 8.0f * ui_scale, badge.box_min.y + 5.0f * ui_scale},
            AxisStyle::kBadgeText(),
            badge.text);
        if (small_font() != nullptr) {
            ImGui::PopFont();
        }
    }

    draw_scale_bar(plot_min, plot_max, view.scale.c_str(), ui_scale);
    draw_orientation_gizmo(view, plot_max, ui_scale);
}

} // namespace

void begin_viewport_frame_shortcuts(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions
) {
    if (ImGui::GetIO().KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_O, false)) {
        actions.open_requested = true;
    }
    if (ImGui::GetIO().KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_N, false)) {
        show_first_hidden_view(state);
    }
}

void finalize_viewport_frame_shortcuts(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions
) {
    if (!ImGui::GetIO().WantTextInput &&
        !ImGui::GetIO().KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_M, false)) {
        int target = state.active_viewport_index;
        for (const auto& frame : actions.viewport_frames) {
            if (frame.active || frame.hovered) {
                target = frame.index;
            }
        }
        auto& measurement =
            gs3d::app::measurement_for_view(state, target);
        measurement.toggle_measure_mode();
        if (!measurement.measure_mode_active()) {
            measurement.clear_pending();
        }
    }
    for (const auto& frame : actions.viewport_frames) {
        if (frame.active || frame.hovered) {
            state.active_viewport_index = frame.index;
        }
    }
}

/*
 * 视口画布主体（声明见 ViewportCanvas.hpp）。工作台布局的视图窗口和
 * 悬浮 Dock 布局的全屏沉浸视口共用这一份实现。
 */
void draw_viewport_canvas(
    gs3d::app::RenderViewState& view,
    gs3d::app::UiActions& actions,
    const ViewportCanvasOptions& options
)
{
    const int workspace_id = options.workspace_id;
    const bool show_info_badge = options.show_info_badge;
    const bool interaction_enabled = options.interaction_enabled;

    ImVec2 available = ImGui::GetContentRegionAvail();
    available.x = std::max(1.0f, available.x);
    available.y = std::max(1.0f, available.y);

    ImGui::InvisibleButton(
        "##ViewportCanvas",
        available,
        ImGuiButtonFlags_MouseButtonLeft |
        ImGuiButtonFlags_MouseButtonRight |
        ImGuiButtonFlags_MouseButtonMiddle
    );
    const ImVec2 canvas_min = ImGui::GetItemRectMin();
    const ImVec2 canvas_max = ImGui::GetItemRectMax();
    const ViewportScreenRect canvas_rect{
        canvas_min.x,
        canvas_min.y,
        canvas_max.x,
        canvas_max.y
    };
    const bool platform_window_focused =
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    const auto input_routing = resolve_viewport_input_routing(
        ImGui::IsItemHovered(),
        ImGui::IsItemActive(),
        platform_window_focused,
        interaction_enabled
    );
    const bool hovered = input_routing.hovered;
    const bool active = input_routing.active;
    const bool show_hover_details =
        interaction_enabled && view.hover_tooltip_visible;
    if (!interaction_enabled) {
        view.box_select_dragging = false;
        view.stats_select_dragging = false;
    }

    // UI scale for high-DPI: use font size relative to default 13 px.
    const float ui_scale = ImGui::GetFontSize() / 13.0f;

    // ── 地图轴模式：点在缩小的 plot_rect 内显示，轴在边距中绘制 ──
    const auto plot_rect = compute_plot_rect(
        view.show_map_axis,
        canvas_rect,
        ui_scale
    );
    ImVec2 plot_min{plot_rect.min_x, plot_rect.min_y};
    ImVec2 plot_max{plot_rect.max_x, plot_rect.max_y};
    ImDrawList* canvas_dl = ImGui::GetWindowDrawList();
    canvas_dl->AddRectFilled(
        canvas_min,
        canvas_max,
        ImGui::GetColorU32(ImGuiCol_WindowBg)
    );
    canvas_dl->AddRectFilled(
        plot_min,
        plot_max,
        to_u32(palette::kViewportBg, 255)
    );

    // ── 测量模式视口边框提示 ──
    if (view.measure_mode_active) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 kMeasureBorder = to_u32(palette::kYellow, 180);
        const float kBorderWidth = 3.5f * ui_scale;
        dl->AddRect(plot_min, plot_max, kMeasureBorder, 0.0f, 0, kBorderWidth);

        const float kBadgePadX = 8.0f * ui_scale;
        const float kBadgePadY = 5.0f * ui_scale;
        const ImU32 kBadgeBg = to_u32(palette::kYellow, 230);
        // 黄色底在三套主题里都是亮色，文字必须固定用深色——暗主题的
        // kText 是近白色，用它会白字压黄底、完全读不清。
        const ImU32 kBadgeText = IM_COL32(32, 26, 8, 255);
        const char* badge_label = "测量模式  中键量距  Shift框选统计";
        const ImVec2 ts = ImGui::CalcTextSize(badge_label);
        const ImVec2 badge_min{
            plot_min.x + kBadgePadX,
            plot_min.y + kBadgePadY};
        const ImVec2 badge_max{
            badge_min.x + ts.x + kBadgePadX * 2.0f,
            badge_min.y + ts.y + kBadgePadY * 2.0f};
        dl->AddRectFilled(badge_min, badge_max, kBadgeBg, 4.0f * ui_scale);
        dl->AddText(
            ImVec2(badge_min.x + kBadgePadX, badge_min.y + kBadgePadY),
            kBadgeText, badge_label);
    }

    if (view.show_live_image &&
        view.descriptor != VK_NULL_HANDLE) {
        ImGui::GetWindowDrawList()->AddImage(
            static_cast<ImTextureID>(
                reinterpret_cast<ImU64>(view.descriptor)
            ),
            plot_min,
            plot_max
        );
    } else {
        draw_mock_viewport(plot_min, plot_max);
    }

    // ── 地图式坐标轴 — 轻量 overlay ────────────
    if (view.show_map_axis) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(canvas_min, canvas_max, true);
        // Badge 隐藏时用零矩形，标签遮挡检测自然全部通过。
        const BadgeOverlay badge = show_info_badge
            ? make_badge_overlay(view, plot_min, ui_scale)
            : BadgeOverlay{};
        const float axis_outer_pad = LayoutMetrics::kAxisOuterPadding * ui_scale;

        // 轴线：顶部 X 轴 + 左侧 Y 轴，全部保持在 plot 外侧科学绘图风格
        dl->AddLine(ImVec2(plot_min.x, plot_min.y),
                    ImVec2(plot_max.x, plot_min.y),
                    AxisStyle::kAxisLine(), AxisStyle::kAxisLineWidth);
        dl->AddLine(ImVec2(plot_min.x, plot_min.y),
                    ImVec2(plot_min.x, plot_max.y),
                    AxisStyle::kAxisLine(), AxisStyle::kAxisLineWidth);

        const float x_range = view.map_axis_x_max - view.map_axis_x_min;
        const float y_range = view.map_axis_y_max - view.map_axis_y_min;

        // 两级刻度：major（长刻度+标签+网格），minor（短刻度，无标签无网格）。
        // 每个 major interval 细分为 kMinorPerMajor 个 minor step，
        // 保证坐标轴读数更细，而背景网格线仍稀疏。
        const int major_cnt = std::clamp(
            static_cast<int>(std::ceil(
                std::max(x_range, y_range) > 0.0f ? 5.0f : 4.0f)),
            AxisStyle::kMajorCountMin, AxisStyle::kMajorCountMax);

        std::vector<float> x_major;
        if (x_range > 0.0f) {
            x_major = gs3d::render::compute_axis_ticks(
                view.map_axis_x_min, view.map_axis_x_max, major_cnt);
        }
        const float x_major_step = (x_major.size() >= 2)
            ? (x_major[1] - x_major[0]) : 1.0f;

        std::vector<float> y_major;
        if (y_range > 0.0f) {
            y_major = gs3d::render::compute_axis_ticks(
                view.map_axis_y_min, view.map_axis_y_max, major_cnt);
        }
        const float y_major_step = (y_major.size() >= 2)
            ? (y_major[1] - y_major[0]) : 1.0f;

        // ---- X 轴（顶部）----
        if (x_range > 0.0f) {
            // 弱网格线（仅 major 位置）
            for (const float tick : x_major) {
                const float t = (tick - view.map_axis_x_min) / x_range;
                const float px = plot_min.x + t * (plot_max.x - plot_min.x);
                dl->AddLine(ImVec2(px, plot_min.y), ImVec2(px, plot_max.y),
                            AxisStyle::kGrid(), AxisStyle::kGridWidth);
            }

            const float tick_len_major = AxisStyle::kMajorTickLen * ui_scale;
            const float tick_len_minor = AxisStyle::kMinorTickLen * ui_scale;
            const float label_gap = AxisStyle::kXTickToLabel * ui_scale;

            // Major ticks 向上 + 标签在顶部外侧
            for (const float tick : x_major) {
                const float t = (tick - view.map_axis_x_min) / x_range;
                const float px = plot_min.x + t * (plot_max.x - plot_min.x);

                dl->AddLine(
                    ImVec2(px, plot_min.y),
                    ImVec2(px, plot_min.y - tick_len_major),
                    AxisStyle::kMajorTick(), AxisStyle::kMajorTickWidth);

                char label[32];
                format_axis_tick_label(label, sizeof(label), tick,
                    view.map_axis_origin_x, x_major_step);
                if (axis_font() != nullptr) {
                    ImGui::PushFont(axis_font());
                }
                const ImVec2 ts = ImGui::CalcTextSize(label);
                const ImVec2 label_min{
                    px - ts.x * 0.5f,
                    std::max(canvas_min.y + axis_outer_pad,
                             plot_min.y - tick_len_major - label_gap - ts.y)
                };
                const ImVec2 label_max{
                    label_min.x + ts.x,
                    label_min.y + ts.y
                };
                if (!rects_overlap(label_min, label_max,
                                   badge.box_min, badge.box_max)) {
                    dl->AddText(label_min, AxisStyle::kLabel(), label);
                }
                if (axis_font() != nullptr) {
                    ImGui::PopFont();
                }
            }

            // Minor ticks 向上
            if (x_major.size() >= 2) {
                const auto x_minors = compute_minor_axis_ticks(
                    x_major_step, x_major.front(),
                    view.map_axis_x_min, view.map_axis_x_max);
                for (const float tick : x_minors) {
                    const float t = (tick - view.map_axis_x_min) / x_range;
                    const float px = plot_min.x + t * (plot_max.x - plot_min.x);
                    dl->AddLine(
                        ImVec2(px, plot_min.y),
                        ImVec2(px, plot_min.y - tick_len_minor),
                        AxisStyle::kMinorTick(), AxisStyle::kMinorTickWidth);
                }
            }
        }

        // ---- Y 轴（左侧）----
        if (y_range > 0.0f) {
            // 网格线：只在 major 位置画
            for (const float tick : y_major) {
                const float t = (tick - view.map_axis_y_min) / y_range;
                const float py = plot_max.y - t * (plot_max.y - plot_min.y);
                dl->AddLine(ImVec2(plot_min.x, py), ImVec2(plot_max.x, py),
                            AxisStyle::kGrid(), AxisStyle::kGridWidth);
            }

            const float tick_len_major = AxisStyle::kMajorTickLen * ui_scale;
            const float tick_len_minor = AxisStyle::kMinorTickLen * ui_scale;
            const float label_gap = AxisStyle::kYTickToLabel * ui_scale;

            // Major ticks 向左 + 标签在左侧外部
            for (const float tick : y_major) {
                const float t = (tick - view.map_axis_y_min) / y_range;
                const float py = plot_max.y - t * (plot_max.y - plot_min.y);

                dl->AddLine(
                    ImVec2(plot_min.x, py),
                    ImVec2(plot_min.x - tick_len_major, py),
                    AxisStyle::kMajorTick(), AxisStyle::kMajorTickWidth);

                char label[32];
                format_axis_tick_label(label, sizeof(label), tick,
                    view.map_axis_origin_y, y_major_step);
                if (axis_font() != nullptr) {
                    ImGui::PushFont(axis_font());
                }
                const ImVec2 ts = ImGui::CalcTextSize(label);
                dl->AddText(
                    ImVec2(std::max(canvas_min.x + axis_outer_pad,
                                    plot_min.x - tick_len_major -
                                    label_gap - ts.x),
                           py - ts.y * 0.5f),
                    AxisStyle::kLabel(), label);
                if (axis_font() != nullptr) {
                    ImGui::PopFont();
                }
            }

            // Minor ticks 向左
            if (y_major.size() >= 2) {
                const auto y_minors = compute_minor_axis_ticks(
                    y_major_step, y_major.front(),
                    view.map_axis_y_min, view.map_axis_y_max);
                for (const float tick : y_minors) {
                    const float t = (tick - view.map_axis_y_min) / y_range;
                    const float py = plot_max.y - t * (plot_max.y - plot_min.y);
                    dl->AddLine(
                        ImVec2(plot_min.x, py),
                        ImVec2(plot_min.x - tick_len_minor, py),
                        AxisStyle::kMinorTick(), AxisStyle::kMinorTickWidth);
                }
            }
        }

        // ── 悬停十字准线 ──
        // Crosshair spanning the full plot area, with coordinate
        // readout at the axis intersection points.
        // Only active when both show_crosshair and show_map_axis are on.
        if (view.show_crosshair &&
            show_hover_details &&
            view.hover_screen_x >= 0.0f &&
            view.hover_screen_y >= 0.0f &&
            view.image_width > 0 && view.image_height > 0) {
            const auto scr = framebuffer_to_plot_screen(
                view.hover_screen_x, view.hover_screen_y,
                canvas_rect, view.show_map_axis,
                view.image_width, view.image_height,
                ui_scale);
            const float cx = plot_min.x + scr.x;
            const float cy = plot_min.y + scr.y;

            const ImU32 kCrosshairLine  = srgb_u32_to_linear(view.crosshair_color) & 0x00FFFFFF | (80u << 24);
            const ImU32 kCrosshairBg    = to_u32(palette::kViewportBg, 200);
            const ImU32 kCrosshairText  = srgb_u32_to_linear(view.crosshair_color) & 0x00FFFFFF | (240u << 24);
            const float kCrosshairWidth = 1.0f * ui_scale;
            const float kLabelPad = 3.0f * ui_scale;
            const float kLabelAxisGap = 3.0f * ui_scale;

            dl->AddLine(ImVec2(plot_min.x, cy), ImVec2(plot_max.x, cy),
                        kCrosshairLine, kCrosshairWidth);
            dl->AddLine(ImVec2(cx, plot_min.y), ImVec2(cx, plot_max.y),
                        kCrosshairLine, kCrosshairWidth);
            dl->AddLine(ImVec2(cx, plot_min.y), ImVec2(cx, plot_max.y),
                        kCrosshairLine, kCrosshairWidth);

            char label_x[32], label_y[32];
            std::snprintf(label_x, sizeof(label_x), "%.*f",
                axis_label_precision(x_major_step),
                static_cast<double>(view.hover_x));
            std::snprintf(label_y, sizeof(label_y), "%.*f",
                axis_label_precision(y_major_step),
                static_cast<double>(view.hover_y));

            if (axis_font() != nullptr) {
                ImGui::PushFont(axis_font());
            }
            const ImVec2 tx = ImGui::CalcTextSize(label_x);
            const ImVec2 ty = ImGui::CalcTextSize(label_y);

            // X value: at top-axis intersection (vertical line meets X axis).
            // Background box above axis line, outside plot.
            {
                const float bg_min_x = cx - tx.x * 0.5f - kLabelPad;
                const float bg_max_x = cx + tx.x * 0.5f + kLabelPad;
                const float bg_min_y = plot_min.y - tx.y - kLabelPad * 2.0f
                                       - kLabelAxisGap;
                const float bg_max_y = plot_min.y - kLabelAxisGap;
                dl->AddRectFilled(
                    ImVec2(bg_min_x, bg_min_y),
                    ImVec2(bg_max_x, bg_max_y),
                    kCrosshairBg, 3.0f * ui_scale);
                dl->AddText(
                    ImVec2(cx - tx.x * 0.5f, bg_min_y + kLabelPad),
                    kCrosshairText, label_x);
            }

            // Y value: at left-axis intersection (horizontal line meets Y axis).
            // Background box left of axis line, outside plot.
            {
                const float bg_min_x = plot_min.x - ty.x - kLabelPad * 2.0f
                                       - kLabelAxisGap;
                const float bg_max_x = plot_min.x - kLabelAxisGap;
                const float bg_min_y = cy - ty.y * 0.5f - kLabelPad;
                const float bg_max_y = cy + ty.y * 0.5f + kLabelPad;
                dl->AddRectFilled(
                    ImVec2(bg_min_x, bg_min_y),
                    ImVec2(bg_max_x, bg_max_y),
                    kCrosshairBg, 3.0f * ui_scale);
                dl->AddText(
                    ImVec2(bg_min_x + kLabelPad, cy - ty.y * 0.5f),
                    kCrosshairText, label_y);
            }

            if (axis_font() != nullptr) {
                ImGui::PopFont();
            }
        }

        dl->PopClipRect();
    }

    // ── 三维世界坐标轴（QGIS 包围盒，随相机旋转）──
    if (view.show_world_axis) {
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const ImU32 kFrameColor  = to_u32(palette::kGray, 180);
        const ImU32 kTickColor   = to_u32(palette::kGray, 140);
        const ImU32 kLabelColor  = to_u32(palette::kText, 200);

        draw_list->PushClipRect(canvas_min, canvas_max, true);

        for (std::size_t i = 0; i < view.axis_lines.size(); ++i) {
            const auto& seg = view.axis_lines[i];
            const ImU32 color = i < 12 ? kFrameColor : kTickColor;
            const float thickness = i < 12 ? 1.5f : 1.0f;
            draw_list->AddLine(
                ImVec2(canvas_min.x + seg.x0, canvas_min.y + seg.y0),
                ImVec2(canvas_min.x + seg.x1, canvas_min.y + seg.y1),
                color,
                thickness
            );
        }

        for (const auto& label : view.axis_tick_labels) {
            draw_list->AddText(
                ImVec2(canvas_min.x + label.x, canvas_min.y + label.y),
                kLabelColor,
                label.text.c_str()
            );
        }

        draw_list->PopClipRect();
    }

    draw_viewport_overlay(view, plot_min, plot_max, ui_scale, show_info_badge);


    // ── 悬浮高亮标记 ──
    // Draws a crosshair+ring at the pick hit-point.  No cursor-movement
    // freshness gate — the pick result's has_hit is the single source of
    // truth.  The marker naturally clears when the next pick has no hit.
    if (show_hover_details &&
        view.hover_screen_x >= 0.0f &&
        view.hover_screen_y >= 0.0f &&
        view.image_width > 0 && view.image_height > 0) {
        const auto scr = framebuffer_to_plot_screen(
            view.hover_screen_x, view.hover_screen_y,
            canvas_rect, view.show_map_axis,
            view.image_width, view.image_height,
            ui_scale);
        const float cx = plot_min.x + scr.x;
        const float cy = plot_min.y + scr.y;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float kR = 8.0f * ui_scale;
        const ImU32 kColor = srgb_u32_to_linear(view.reticle_color) & 0x00FFFFFF | (220u << 24);
        const float kThick = 2.0f * ui_scale;
        dl->AddLine({cx - kR, cy}, {cx + kR, cy}, kColor, kThick);
        dl->AddLine({cx, cy - kR}, {cx, cy + kR}, kColor, kThick);
        dl->AddCircle({cx, cy}, kR + 2.0f * ui_scale, kColor, 0, kThick * 0.7f);

        // "已复制" feedback overlay
        if (view.copy_feedback_frames > 0) {
            const float kFeedbackPad = 4.0f * ui_scale;
            const ImU32 kFeedbackBg = to_u32(palette::kGreen, 220);
            // 绿色底同理：固定深色文字，避免暗主题白字压亮绿底。
            const ImU32 kFeedbackText = IM_COL32(8, 26, 16, 255);
            const char* feedback = "已复制";
            const ImVec2 fs = ImGui::CalcTextSize(feedback);
            const float fb_x = cx - fs.x * 0.5f;
            const float fb_y = cy - kR - fs.y - kFeedbackPad * 2.0f - 8.0f * ui_scale;
            dl->AddRectFilled(
                ImVec2(fb_x - kFeedbackPad, fb_y - kFeedbackPad),
                ImVec2(fb_x + fs.x + kFeedbackPad, fb_y + fs.y + kFeedbackPad),
                kFeedbackBg, 3.0f * ui_scale);
            dl->AddText(ImVec2(fb_x, fb_y), kFeedbackText, feedback);
        }
    }

    if (view.selected_point_visible &&
        view.selected_screen_x >= 0.0f &&
        view.selected_screen_y >= 0.0f &&
        view.image_width > 0 && view.image_height > 0) {
        const auto scr = framebuffer_to_plot_screen(
            view.selected_screen_x, view.selected_screen_y,
            canvas_rect, view.show_map_axis,
            view.image_width, view.image_height,
            ui_scale);
        const float cx = plot_min.x + scr.x;
        const float cy = plot_min.y + scr.y;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 kSelectedColor = to_u32(palette::kVarBlue, 240);
        dl->AddCircle({cx, cy}, 12.0f * ui_scale, kSelectedColor, 0, 2.5f * ui_scale);
        dl->AddCircleFilled({cx, cy}, 3.0f * ui_scale, kSelectedColor);
    }

    // ── 测量线绘制 ──
    // Pre-projected by ViewerApp each frame from world coords via the
    // same MouseRay::world_to_screen pipeline the crosshair uses.
    if (!view.measurement_overlays.empty() &&
        view.image_width > 0 && view.image_height > 0) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(plot_min, plot_max, true);
        constexpr float kMeasureLineWidth = 2.0f;
        constexpr float kMeasureLabelPad = 3.0f;
        const ImU32 kMeasureLabelBg = to_u32(palette::kViewportBg, 200);

        for (const auto& overlay : view.measurement_overlays) {
            if (!overlay.visible) continue;

            const auto sa = framebuffer_to_plot_screen(
                overlay.a_screen_x, overlay.a_screen_y,
                canvas_rect, view.show_map_axis,
                view.image_width, view.image_height,
                ui_scale);
            const auto sb = framebuffer_to_plot_screen(
                overlay.b_screen_x, overlay.b_screen_y,
                canvas_rect, view.show_map_axis,
                view.image_width, view.image_height,
                ui_scale);

            const ImVec2 pa{plot_min.x + sa.x, plot_min.y + sa.y};
            const ImVec2 pb{plot_min.x + sb.x, plot_min.y + sb.y};

            // overlay.color 按 sRGB 值存储（颜色选择器所见），绘制前线性化
            dl->AddLine(pa, pb, srgb_u32_to_linear(overlay.color),
                        kMeasureLineWidth * ui_scale);

            // Distance label at midpoint.
            if (!overlay.label.empty()) {
                const ImVec2 pmid{
                    (pa.x + pb.x) * 0.5f,
                    (pa.y + pb.y) * 0.5f};
                const ImVec2 ts = ImGui::CalcTextSize(
                    overlay.label.c_str());
                dl->AddRectFilled(
                    ImVec2(pmid.x - ts.x * 0.5f - kMeasureLabelPad * ui_scale,
                           pmid.y - ts.y * 0.5f - kMeasureLabelPad * ui_scale),
                    ImVec2(pmid.x + ts.x * 0.5f + kMeasureLabelPad * ui_scale,
                           pmid.y + ts.y * 0.5f + kMeasureLabelPad * ui_scale),
                    kMeasureLabelBg, 3.0f * ui_scale);
                // Label color follows the line color for readability.
                const ImU32 kLabelColor = srgb_u32_to_linear(overlay.color)
                                          & 0x00FFFFFF | (240u << 24);
                dl->AddText(
                    ImVec2(pmid.x - ts.x * 0.5f,
                           pmid.y - ts.y * 0.5f),
                    kLabelColor,
                    overlay.label.c_str());
            }
        }
        dl->PopClipRect();
    }

    // ── 待定测量点标记 + 预览线 ──
    if (view.pending_point_visible &&
        view.pending_point_screen_x >= 0.0f &&
        view.pending_point_screen_y >= 0.0f &&
        view.image_width > 0 && view.image_height > 0) {
        const auto scr = framebuffer_to_plot_screen(
            view.pending_point_screen_x, view.pending_point_screen_y,
            canvas_rect, view.show_map_axis,
            view.image_width, view.image_height,
            ui_scale);
        const float px = plot_min.x + scr.x;
        const float py = plot_min.y + scr.y;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(plot_min, plot_max, true);

        // Filled circle + outer ring in measurement-yellow.
        const float kMarkerR = 7.0f * ui_scale;
        const ImU32 kMarkerFill = to_u32(palette::kYellow, 200);
        const ImU32 kMarkerRing = to_u32(palette::kYellow, 255);
        dl->AddCircleFilled({px, py}, kMarkerR, kMarkerFill);
        dl->AddCircle({px, py}, kMarkerR + 2.0f * ui_scale, kMarkerRing, 0, 2.5f * ui_scale);

        // Preview line to mouse cursor while the cursor is on the plot area.
        const ImGuiIO& io = ImGui::GetIO();
        const float mx = io.MousePos.x;
        const float my = io.MousePos.y;
        if (interaction_enabled &&
            mx >= plot_min.x && mx < plot_max.x &&
            my >= plot_min.y && my < plot_max.y) {
            const ImU32 kPreviewLine = to_u32(palette::kYellow, 100);
            dl->AddLine({px, py}, {mx, my}, kPreviewLine, 1.5f * ui_scale);
        }

        dl->PopClipRect();
    }

    const ImGuiIO& io = ImGui::GetIO();
    gs3d::app::ViewportFrameCmd frame;
    frame.index = view.viewport_index;
    frame.workspace_id = workspace_id;
    frame.width = static_cast<std::uint32_t>(available.x);
    frame.height = static_cast<std::uint32_t>(available.y);
    // Use camera viewport (= GPU framebuffer) dimensions, not ImGui
    // content region, so the mouse→fb mapping stays pixel-accurate.
    const auto mouse_mapping = map_screen_mouse_to_framebuffer(
        io.MousePos.x, io.MousePos.y,
        canvas_rect, view.show_map_axis,
        view.image_width, view.image_height,
        ui_scale);

    frame.hovered = hovered && mouse_mapping.mouse_on_image;
    frame.active =
        active &&
        (mouse_mapping.mouse_on_image || view.box_select_dragging);
    frame.mouse_delta_x = active ? io.MouseDelta.x : 0.0f;
    frame.mouse_delta_y = active ? io.MouseDelta.y : 0.0f;
    frame.mouse_wheel = frame.hovered ? io.MouseWheel : 0.0f;
    frame.mouse_local_x = mouse_mapping.framebuffer_x;
    frame.mouse_local_y = mouse_mapping.framebuffer_y;
    frame.mouse_on_image =
        interaction_enabled && mouse_mapping.mouse_on_image;

    if (frame.hovered || frame.active) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
    }

    // Tooltip: shown whenever we have valid hover data and the cursor is on the image.
    if (frame.mouse_on_image && show_hover_details) {
        draw_hover_property_tooltip(view, ui_scale);
    }

    // ── C key: copy hovered point values to clipboard ──
    if (view.copy_feedback_frames > 0) {
        --view.copy_feedback_frames;
    }
    if (frame.mouse_on_image && show_hover_details &&
        !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
        char clip_buf[256];
        std::snprintf(clip_buf, sizeof(clip_buf),
            "%.6f,%.6f,%.6f,%.6f",
            static_cast<double>(view.hover_x),
            static_cast<double>(view.hover_y),
            static_cast<double>(view.hover_elevation),
            static_cast<double>(view.hover_fold));
        ImGui::SetClipboardText(clip_buf);
        view.copy_feedback_frames = 90; // ~1.5 s at 60 fps
    }

    // Ctrl+左键 = 框选放大，普通左键 = 轨道旋转；两者互斥，框选时不旋转。
    // 测量模式下 Shift+左键 = 框选统计，同样不旋转。
    const bool box_select_button_down =
        active &&
        frame.mouse_on_image &&
        io.KeyCtrl &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left);

    const bool stats_select_button_down =
        active &&
        frame.mouse_on_image &&
        view.measure_mode_active &&
        io.KeyShift &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left);

    frame.rotate =
        active &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
        !io.KeyCtrl &&
        !io.KeyShift;
    frame.pan =
        active &&
        frame.mouse_on_image &&
        ImGui::IsMouseDown(ImGuiMouseButton_Right);
    frame.point_double_clicked =
        active &&
        frame.mouse_on_image &&
        !io.KeyCtrl &&
        !io.KeyShift &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    frame.measure_pick_requested =
        active &&
        frame.mouse_on_image &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Middle);

    if (box_select_button_down && !view.box_select_dragging) {
        view.box_select_dragging = true;
        view.box_select_start_x = frame.mouse_local_x;
        view.box_select_start_y = frame.mouse_local_y;
    }

    if (view.box_select_dragging) {
        // Both corners through framebuffer_to_plot_screen — the same
        // inverse transform the crosshair uses.  Keeps the drawn rect
        // and the box→world unprojection in the same coordinate space.
        const auto scr_start = framebuffer_to_plot_screen(
            view.box_select_start_x, view.box_select_start_y,
            canvas_rect, view.show_map_axis,
            view.image_width, view.image_height,
            ui_scale);
        const auto scr_curr = framebuffer_to_plot_screen(
            frame.mouse_local_x, frame.mouse_local_y,
            canvas_rect, view.show_map_axis,
            view.image_width, view.image_height,
            ui_scale);
        const ImVec2 rect_a(plot_min.x + scr_start.x,
                            plot_min.y + scr_start.y);
        const ImVec2 rect_b(plot_min.x + scr_curr.x,
                            plot_min.y + scr_curr.y);
        ImGui::GetWindowDrawList()->AddRect(
            rect_a, rect_b, to_u32(palette::kAccent, 255),
            0.0f, 0, 1.5f * ui_scale
        );
        ImGui::GetWindowDrawList()->AddRectFilled(
            rect_a, rect_b, to_u32(palette::kAccent, 40)
        );

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            view.box_select_dragging = false;

            const float min_x = std::min(view.box_select_start_x, frame.mouse_local_x);
            const float min_y = std::min(view.box_select_start_y, frame.mouse_local_y);
            const float max_x = std::max(view.box_select_start_x, frame.mouse_local_x);
            const float max_y = std::max(view.box_select_start_y, frame.mouse_local_y);

            // Ignore accidental clicks/tiny drags (< 4px on either axis).
            if (frame.mouse_on_image &&
                max_x - min_x >= 4.0f &&
                max_y - min_y >= 4.0f) {
                frame.box_select_completed = true;
                frame.box_select_min_x = min_x;
                frame.box_select_min_y = min_y;
                frame.box_select_max_x = max_x;
                frame.box_select_max_y = max_y;
            }
        }
    }

    // ── Shift+左键框选统计（测量模式下）──
    if (stats_select_button_down && !view.stats_select_dragging) {
        view.stats_select_dragging = true;
        view.stats_select_start_x = frame.mouse_local_x;
        view.stats_select_start_y = frame.mouse_local_y;
    }

    if (view.stats_select_dragging) {
        const auto scr_start = framebuffer_to_plot_screen(
            view.stats_select_start_x, view.stats_select_start_y,
            canvas_rect, view.show_map_axis,
            view.image_width, view.image_height,
            ui_scale);
        const auto scr_curr = framebuffer_to_plot_screen(
            frame.mouse_local_x, frame.mouse_local_y,
            canvas_rect, view.show_map_axis,
            view.image_width, view.image_height,
            ui_scale);
        const ImVec2 rect_a(plot_min.x + scr_start.x,
                            plot_min.y + scr_start.y);
        const ImVec2 rect_b(plot_min.x + scr_curr.x,
                            plot_min.y + scr_curr.y);
        ImGui::GetWindowDrawList()->AddRect(
            rect_a, rect_b, to_u32(palette::kTeal, 255),
            0.0f, 0, 1.5f * ui_scale
        );
        ImGui::GetWindowDrawList()->AddRectFilled(
            rect_a, rect_b, to_u32(palette::kTeal, 40)
        );

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            view.stats_select_dragging = false;

            const float min_x = std::min(view.stats_select_start_x, frame.mouse_local_x);
            const float min_y = std::min(view.stats_select_start_y, frame.mouse_local_y);
            const float max_x = std::max(view.stats_select_start_x, frame.mouse_local_x);
            const float max_y = std::max(view.stats_select_start_y, frame.mouse_local_y);

            if (frame.mouse_on_image &&
                max_x - min_x >= 4.0f &&
                max_y - min_y >= 4.0f) {
                frame.stats_select_completed = true;
                frame.stats_select_min_x = min_x;
                frame.stats_select_min_y = min_y;
                frame.stats_select_max_x = max_x;
                frame.stats_select_max_y = max_y;
            }
        }
    }

    actions.viewport_frames.push_back(frame);
    view.render_requested = view.visible;

    // ponytail: store canvas rect for screenshot coordinate mapping.
    // GetItemRectMin/Max are in ImGui screen-space (absolute) coordinates.
    view.canvas_rect_min_x = canvas_rect.min_x;
    view.canvas_rect_min_y = canvas_rect.min_y;
    view.canvas_rect_max_x = canvas_rect.max_x;
    view.canvas_rect_max_y = canvas_rect.max_y;
}

} // namespace gs3d::ui
