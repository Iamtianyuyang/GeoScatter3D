#include "ui/UiRoot.hpp"

#include "gui/UiFonts.hpp"
#include "imgui.h"
#include "imgui_internal.h"

#include "render/AxisGrid.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace gs3d::ui {

namespace {

constexpr const char* kHostWindowName =
    "GeoScatter3D 工作台###GeoScatter3DWorkspace";
constexpr const char* kDatasetWindowName =
    "项目###DatasetPanel";
constexpr const char* kRenderSettingsWindowName =
    "属性###RenderSettings";
constexpr const char* kDebugLogWindowName =
    "日志###DebugLog";
constexpr const char* kTileInspectorWindowName =
    "瓦片###TileInspector";
constexpr const char* kLodViewWindowName =
    "细节层级###LodView";
constexpr const char* kPerformanceWindowName =
    "性能###Performance";
constexpr const char* kNavigationMapWindowName =
    "导航图###NavigationMap";

std::string render_view_window_name(int index)
{
    return "视图 " + std::to_string(index + 1) +
        "###RenderView" + std::to_string(index);
}

float bytes_to_mb(std::uint64_t bytes)
{
    return static_cast<float>(bytes) / (1024.0f * 1024.0f);
}

std::uint32_t visible_view_signature(
    const gs3d::app::AppState& state
) {
    std::uint32_t signature = 0;
    for (const auto& view : state.render_views) {
        if (view.visible &&
            view.viewport_index >= 0 &&
            view.viewport_index < 24) {
            signature |=
                1u << static_cast<std::uint32_t>(view.viewport_index);
        }
    }
    return signature;
}

bool show_first_hidden_view(gs3d::app::AppState& state)
{
    const auto hidden = std::find_if(
        state.render_views.begin(),
        state.render_views.end(),
        [](const auto& view) {
            return !view.visible;
        }
    );
    if (hidden == state.render_views.end()) {
        return false;
    }

    hidden->visible = true;
    hidden->render_requested = false;
    return true;
}

// ── 布局与样式常量（GIS / 地图软件风格）──────────────────────────────
namespace LayoutMetrics {
    constexpr float kDockLeftWidth = 220.0f;
    constexpr float kDockRightWidth = 240.0f;
    constexpr float kTopToolbarHeight = 28.0f;
    constexpr float kStatusBarHeight = 22.0f;
    constexpr float kViewportToolbarGap = 6.0f;
    constexpr float kViewportToolbarFramePadX = 5.0f;
    constexpr float kViewportToolbarFramePadY = 2.0f;
    constexpr float kPanelHeaderGap = 8.0f;
    constexpr float kPanelSectionGap = 8.0f;
    constexpr float kPanelLabelWidth = 82.0f;
    constexpr float kPanelInsetX = 10.0f;
    constexpr float kStatusInsetX = 9.0f;
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
    // 轻量坐标尺风格 — 简洁、克制、低对比
    constexpr ImU32 kAxisLine    = IM_COL32(170, 170, 170, 190);
    constexpr ImU32 kMajorTick   = IM_COL32(185, 185, 185, 210);
    constexpr ImU32 kMinorTick   = IM_COL32(145, 145, 145, 160);
    constexpr ImU32 kLabel       = IM_COL32(190, 190, 190, 220);
    constexpr ImU32 kGrid        = IM_COL32(120, 120, 120, 35);
    constexpr ImU32 kFrame       = IM_COL32(70,  72,  78,  80);
    constexpr ImU32 kScaleLine   = IM_COL32(180, 185, 195, 200);
    constexpr ImU32 kScaleLabel  = IM_COL32(190, 195, 205, 215);
    // 信息 badge
    constexpr ImU32 kBadgeBg     = IM_COL32(14,  15,  18,  185);
    constexpr ImU32 kBadgeText   = IM_COL32(210, 215, 225, 245);
    // 方向指示器
    constexpr ImU32 kGizmoBg     = IM_COL32(16,  18,  22,  200);

    // 线宽
    constexpr float kAxisLineWidth   = 1.0f;
    constexpr float kFrameWidth      = 1.0f;
    constexpr float kGridWidth       = 0.5f;
    constexpr float kMajorTickWidth  = 1.0f;
    constexpr float kMinorTickWidth  = 1.0f;

    // 两级刻度
    constexpr float kMajorTickLen   = 8.0f;
    constexpr float kMinorTickLen   = 4.0f;
    constexpr int   kMinorPerMajor  = 4;
    constexpr int   kMajorCountMin  = 4;
    constexpr int   kMajorCountMax  = 6;

    // 标签与刻度线的间距
    constexpr float kXTickToLabel = 4.0f;
    constexpr float kYTickToLabel = 4.0f;
} // namespace AxisStyle

} // namespace

namespace {

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
    const ImVec2& plot_min)
{
    BadgeOverlay badge{};
    std::snprintf(badge.text, sizeof(badge.text), "%llu 点  |  %.2f ms",
        static_cast<unsigned long long>(view.points_visible),
        static_cast<double>(view.frame_time_ms));
    badge.text_size = ImGui::CalcTextSize(badge.text);
    const float bx0 = plot_min.x + LayoutMetrics::kBadgePadX;
    const float by0 = plot_min.y + LayoutMetrics::kBadgePadY;
    badge.box_min = {bx0, by0};
    badge.box_max = {
        bx0 + badge.text_size.x + 16.0f,
        by0 + badge.text_size.y + 10.0f
    };
    return badge;
}

ImFont* regular_font()
{
    return gs3d::gui::ui_fonts().regular;
}

ImFont* small_font()
{
    return gs3d::gui::ui_fonts().small;
}

ImFont* panel_title_font()
{
    return gs3d::gui::ui_fonts().panel_title;
}

ImFont* axis_font()
{
    return gs3d::gui::ui_fonts().axis;
}

ImFont* status_font()
{
    return gs3d::gui::ui_fonts().status;
}

void draw_panel_section_label(const char* label)
{
    if (panel_title_font() != nullptr) {
        ImGui::PushFont(panel_title_font());
    }
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
    ImGui::TextDisabled("%s", label);
    ImGui::Separator();
    ImGui::PopStyleVar();
    if (panel_title_font() != nullptr) {
        ImGui::PopFont();
    }
}

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

void draw_mock_viewport(const ImVec2& min, const ImVec2& max)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(
        min,
        max,
        IM_COL32(20, 22, 25, 255)
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
                            const ImVec2& plot_max)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const float r = LayoutMetrics::kGizmoRadius;
    const ImVec2 origin{
        plot_max.x - r - LayoutMetrics::kGizmoInsetRight,
        plot_max.y - r - LayoutMetrics::kGizmoInsetBottom
    };

    dl->AddCircleFilled(origin, r, AxisStyle::kGizmoBg);

    // 用相机实时方向绘制三轴（不再是硬编码的静态方向）
    if (view.gizmo_axes_valid) {
        const float len = r - 2.0f;
        const auto draw_axis = [&](const gs3d::app::RenderViewState::GizmoAxisEnd& end,
                                   ImU32 color) {
            // 归一化屏幕空间方向，缩放到 gizmo 半径
            const float mag = std::sqrt(end.dx * end.dx + end.dy * end.dy);
            if (mag < 1.0e-6f) return;
            const float s = len / mag;
            dl->AddLine(origin,
                        ImVec2(origin.x + end.dx * s,
                               origin.y + end.dy * s),
                        color, 1.5f);
        };
        draw_axis(view.gizmo_x_axis, IM_COL32(225, 92, 92, 220));
        draw_axis(view.gizmo_y_axis, IM_COL32(91, 204, 122, 220));
        draw_axis(view.gizmo_z_axis, IM_COL32(81, 141, 230, 220));
    }
}

/*
 * 左下角比例尺：锚定在 plot_rect 内部左下角，与外侧地图轴分层。
 */
void draw_scale_bar(const ImVec2& plot_min,
                    const ImVec2& plot_max,
                    const char* label)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const float bar_w = LayoutMetrics::kScaleBarLen;
    const float x0 = plot_min.x + LayoutMetrics::kScaleBarInsetLeft;
    const float y  = plot_max.y - LayoutMetrics::kScaleBarInsetBottom;
    const float x1 = x0 + bar_w;

    dl->AddLine({x0, y}, {x1, y}, AxisStyle::kScaleLine, 1.5f);
    dl->AddLine({x0, y - 4.0f}, {x0, y + 1.0f}, AxisStyle::kScaleLine, 1.5f);
    dl->AddLine({x1, y - 4.0f}, {x1, y + 1.0f}, AxisStyle::kScaleLine, 1.5f);
    if (axis_font() != nullptr) {
        ImGui::PushFont(axis_font());
    }
    dl->AddText({x0, y - 18.0f}, AxisStyle::kScaleLabel, label);
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
    const ImVec2& plot_max)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const BadgeOverlay badge = make_badge_overlay(view, plot_min);
    dl->AddRectFilled(
        badge.box_min, badge.box_max,
        AxisStyle::kBadgeBg, LayoutMetrics::kBadgeRound);
    if (small_font() != nullptr) {
        ImGui::PushFont(small_font());
    }
    dl->AddText(
        {badge.box_min.x + 8.0f, badge.box_min.y + 5.0f},
        AxisStyle::kBadgeText,
        badge.text);
    if (small_font() != nullptr) {
        ImGui::PopFont();
    }

    // ── 比例尺和方向指示器 ──
    draw_scale_bar(plot_min, plot_max, view.scale.c_str());
    draw_orientation_gizmo(view, plot_max);
}

void draw_viewport_window(
    gs3d::app::RenderViewState& view,
    gs3d::app::UiActions& actions
) {
    view.render_requested = false;
    const auto window_name =
        render_view_window_name(view.viewport_index);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::SetNextWindowSize(
        ImVec2(760.0f, 520.0f),
        ImGuiCond_FirstUseEver
    );
    const bool content_visible =
        ImGui::Begin(window_name.c_str(), &view.visible, flags);
    if (!content_visible) {
        ImGui::End();
        return;
    }

    const auto* window_viewport = ImGui::GetWindowViewport();
    view.detached =
        !ImGui::IsWindowDocked() ||
        (window_viewport != nullptr &&
         window_viewport->ID != ImGui::GetMainViewport()->ID);

    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        ImVec2(LayoutMetrics::kViewportToolbarFramePadX,
               LayoutMetrics::kViewportToolbarFramePadY)
    );
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(LayoutMetrics::kViewportToolbarGap, 4.0f)
    );
    ImGui::TextDisabled(
        view.detached ? "独立窗口" : "工作区"
    );
    ImGui::SameLine();
    if (ImGui::SmallButton("复位视角")) {
        actions.reset_camera_index = view.viewport_index;
    }
    ImGui::SameLine();
    ImGui::Checkbox("联动相机", &view.camera_linked);
    ImGui::SameLine();
    if (ImGui::Checkbox("地图轴", &view.show_map_axis)) {
        if (view.show_map_axis) {
            view.show_world_axis = false;
        }
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("世界轴", &view.show_world_axis)) {
        if (view.show_world_axis) {
            view.show_map_axis = false;
        }
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("十字准线", &view.show_crosshair)) {
        if (view.show_crosshair && !view.show_map_axis) {
            view.show_map_axis = true;
            view.show_world_axis = false;
        }
    }
    const float hint_threshold = 520.0f;
    const float short_hint_threshold = 250.0f;
    const float hint_space = ImGui::GetContentRegionAvail().x;
    const char* hint = nullptr;
    if (hint_space > hint_threshold) {
        hint =
            "左键旋转  右键平移  滚轮光标缩放  "
            "双击定轴  F聚焦  Ctrl+左键框选";
    } else if (hint_space > short_hint_threshold) {
        hint = "左键旋转  右键平移  双击定轴  F聚焦";
    }
    if (hint != nullptr) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(170, 176, 188, 125));
        ImGui::TextUnformatted(hint);
        ImGui::PopStyleColor();
    }
    ImGui::PopStyleVar(2);
    ImGui::Separator();

    ImVec2 available = ImGui::GetContentRegionAvail();
    available.x = std::max(1.0f, available.x);
    available.y = std::max(1.0f, available.y);

    ImGui::InvisibleButton(
        "##ViewportCanvas",
        available,
        ImGuiButtonFlags_MouseButtonLeft |
        ImGuiButtonFlags_MouseButtonRight
    );
    const ImVec2 canvas_min = ImGui::GetItemRectMin();
    const ImVec2 canvas_max = ImGui::GetItemRectMax();
    const ViewportScreenRect canvas_rect{
        canvas_min.x,
        canvas_min.y,
        canvas_max.x,
        canvas_max.y
    };
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

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
        const BadgeOverlay badge = make_badge_overlay(view, plot_min);
        const float axis_outer_pad = LayoutMetrics::kAxisOuterPadding * ui_scale;

        // 轴线：顶部 X 轴 + 左侧 Y 轴，全部保持在 plot 外侧科学绘图风格
        dl->AddLine(ImVec2(plot_min.x, plot_min.y),
                    ImVec2(plot_max.x, plot_min.y),
                    AxisStyle::kAxisLine, AxisStyle::kAxisLineWidth);
        dl->AddLine(ImVec2(plot_min.x, plot_min.y),
                    ImVec2(plot_min.x, plot_max.y),
                    AxisStyle::kAxisLine, AxisStyle::kAxisLineWidth);

        const float x_range = view.map_axis_x_max - view.map_axis_x_min;
        const float y_range = view.map_axis_y_max - view.map_axis_y_min;

        // 两级刻度：major（长刻度+标签+网格），minor（短刻度，无标签无网格）。
        // 每个 major interval 细分为 kMinorPerMajor 个 minor step，
        // 保证坐标轴读数更细，而背景网格线仍稀疏。
        const int major_cnt = std::clamp(
            static_cast<int>(std::ceil(
                std::max(x_range, y_range) > 0.0f ? 5.0f : 4.0f)),
            AxisStyle::kMajorCountMin, AxisStyle::kMajorCountMax);

        // ---- 辅助 lambda：根据刻度步长选择小数位数 ----
        const auto fmt_label = [](char* buf, std::size_t buf_size,
                                  float tick, double origin_offset,
                                  float major_step) {
            // Choose precision based on step magnitude so that
            // zoomed-in views show decimals and zoomed-out views
            // stay compact.
            int prec = 0;
            if (major_step < 1.0f) {
                prec = static_cast<int>(
                    std::ceil(-std::log10(std::max(major_step, 1.0e-6f))));
                if (prec < 0) prec = 0;
                if (prec > 6) prec = 6;
            }
            std::snprintf(buf, buf_size, "%.*f", prec,
                static_cast<double>(tick) + origin_offset);
        };

        // ---- 辅助 lambda：生成 minor ticks ----
        //
        // 必须用 double + 整数索引步进：之前用 `v += minor_step` 的
        // float 累加，在缩放视野下（如 first_major≈-14544、
        // minor_step≈0.002）float 的精度间隙已经 >= step，循环永不
        // 终止 → 主线程死循环 → UI 卡死。这里把迭代改成 `i * step`
        // 索引步进，强制上限，并处理退化（range 极小而 step 极小）。
        const auto make_minors = [](float major_step, float first_major,
                                     float range_min, float range_max)
            -> std::vector<float>
        {
            std::vector<float> minors;
            if (major_step <= 0.0f || !std::isfinite(major_step)) {
                return minors;
            }
            const float minor_step =
                major_step / static_cast<float>(AxisStyle::kMinorPerMajor);
            if (minor_step <= 0.0f || !std::isfinite(minor_step)) {
                return minors;
            }

            constexpr int kMaxMinorsPerSide = 200;

            const double minor_step_d = static_cast<double>(minor_step);
            const double first_major_d = static_cast<double>(first_major);
            const double range_min_d = static_cast<double>(range_min);
            const double range_max_d = static_cast<double>(range_max);
            // 阈值用 double 算，避免在边界上漏一根 / 多一根
            const double end_threshold =
                minor_step_d * 0.5;

            // 向左（向 range_min）
            int left_count = static_cast<int>(
                std::floor((first_major_d - range_min_d) / minor_step_d));
            if (left_count < 0) left_count = 0;
            if (left_count > kMaxMinorsPerSide) left_count = kMaxMinorsPerSide;
            minors.reserve(
                static_cast<std::size_t>(left_count + kMaxMinorsPerSide));
            for (int i = 1; i <= left_count; ++i) {
                const double v = first_major_d -
                                 static_cast<double>(i) * minor_step_d;
                if (v <= range_min_d + end_threshold) {
                    break;
                }
                minors.push_back(static_cast<float>(v));
            }

            // 向右（向 range_max）
            int right_count = static_cast<int>(
                std::floor((range_max_d - first_major_d) / minor_step_d));
            if (right_count < 0) right_count = 0;
            if (right_count > kMaxMinorsPerSide) {
                right_count = kMaxMinorsPerSide;
            }
            for (int i = 1; i <= right_count; ++i) {
                const double v = first_major_d +
                                 static_cast<double>(i) * minor_step_d;
                if (v >= range_max_d - end_threshold) {
                    break;
                }
                minors.push_back(static_cast<float>(v));
            }

            // 退化降级：上面两侧迭代都已经有迭代上限 kMaxMinorsPerSide
            // 作为死循环安全网。若 range_min/range_max 与 first_major
            // 都几乎重合（极度病态视野），仍保证输出最多 2*kMaxMinorsPerSide
            // 个 tick，且都不会让 v += step 的累加逻辑出现。
            if (minors.size() >
                static_cast<std::size_t>(2 * kMaxMinorsPerSide)) {
                minors.resize(2 * kMaxMinorsPerSide);
            }
            return minors;
        };

        // ---- X 轴（顶部）----
        if (x_range > 0.0f) {
            const auto x_major = gs3d::render::compute_axis_ticks(
                view.map_axis_x_min, view.map_axis_x_max, major_cnt);
            const float x_major_step = (x_major.size() >= 2)
                ? (x_major[1] - x_major[0]) : 1.0f;

            // 弱网格线（仅 major 位置）
            for (const float tick : x_major) {
                const float t = (tick - view.map_axis_x_min) / x_range;
                const float px = plot_min.x + t * (plot_max.x - plot_min.x);
                dl->AddLine(ImVec2(px, plot_min.y), ImVec2(px, plot_max.y),
                            AxisStyle::kGrid, AxisStyle::kGridWidth);
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
                    AxisStyle::kMajorTick, AxisStyle::kMajorTickWidth);

                char label[32];
                fmt_label(label, sizeof(label), tick,
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
                    dl->AddText(label_min, AxisStyle::kLabel, label);
                }
                if (axis_font() != nullptr) {
                    ImGui::PopFont();
                }
            }

            // Minor ticks 向上
            if (x_major.size() >= 2) {
                const auto x_minors = make_minors(
                    x_major_step, x_major.front(),
                    view.map_axis_x_min, view.map_axis_x_max);
                for (const float tick : x_minors) {
                    const float t = (tick - view.map_axis_x_min) / x_range;
                    const float px = plot_min.x + t * (plot_max.x - plot_min.x);
                    dl->AddLine(
                        ImVec2(px, plot_min.y),
                        ImVec2(px, plot_min.y - tick_len_minor),
                        AxisStyle::kMinorTick, AxisStyle::kMinorTickWidth);
                }
            }
        }

        // ---- Y 轴（左侧）----
        // --- diagnostic: Y-axis visibility trace (off by default) ---
        constexpr bool kYAxisDiag = false;  // set true to enable
        if (kYAxisDiag) {
            static int ydiag_count = 0;
            if (++ydiag_count % 30 == 0) {
                std::fprintf(stderr,
                    "[YAXIS] frame=%d y_range=%.6f "
                    "y_min=%.2f y_max=%.2f "
                    "plot_y=[%.1f,%.1f] major_cnt=%d\n",
                    ydiag_count,
                    static_cast<double>(y_range),
                    static_cast<double>(view.map_axis_y_min),
                    static_cast<double>(view.map_axis_y_max),
                    static_cast<double>(plot_min.y),
                    static_cast<double>(plot_max.y),
                    major_cnt);
            }
        }
        if (y_range > 0.0f) {
            const auto y_major = gs3d::render::compute_axis_ticks(
                view.map_axis_y_min, view.map_axis_y_max, major_cnt);
            if (kYAxisDiag) {
                static int ydiag_count2 = 0;
                if (++ydiag_count2 % 30 == 0) {
                    std::fprintf(stderr,
                        "[YAXIS] y_major cnt=%zu step=%.2f "
                        "first=%.2f last=%.2f\n",
                        y_major.size(),
                        y_major.size() >= 2
                            ? static_cast<double>(y_major[1] - y_major[0])
                            : -1.0,
                        y_major.empty()
                            ? 0.0
                            : static_cast<double>(y_major.front()),
                        y_major.empty()
                            ? 0.0
                            : static_cast<double>(y_major.back()));
                    for (size_t ti = 0; ti < std::min(y_major.size(), size_t{4}); ++ti) {
                        const float t = (y_major[ti] - view.map_axis_y_min) / y_range;
                        const float py = plot_max.y - t * (plot_max.y - plot_min.y);
                        std::fprintf(stderr,
                            "[YAXIS]   tick[%zu]=%.2f t=%.4f py=%.1f "
                            "in_plot=%s\n",
                            ti,
                            static_cast<double>(y_major[ti]),
                            static_cast<double>(t),
                            static_cast<double>(py),
                            (py >= plot_min.y && py <= plot_max.y) ? "YES" : "NO");
                    }
                }
            }
            const float y_major_step = (y_major.size() >= 2)
                ? (y_major[1] - y_major[0]) : 1.0f;

            // 网格线：只在 major 位置画
            for (const float tick : y_major) {
                const float t = (tick - view.map_axis_y_min) / y_range;
                const float py = plot_max.y - t * (plot_max.y - plot_min.y);
                dl->AddLine(ImVec2(plot_min.x, py), ImVec2(plot_max.x, py),
                            AxisStyle::kGrid, AxisStyle::kGridWidth);
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
                    AxisStyle::kMajorTick, AxisStyle::kMajorTickWidth);

                char label[32];
                fmt_label(label, sizeof(label), tick,
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
                    AxisStyle::kLabel, label);
                if (axis_font() != nullptr) {
                    ImGui::PopFont();
                }
            }

            // Minor ticks 向左
            if (y_major.size() >= 2) {
                const auto y_minors = make_minors(
                    y_major_step, y_major.front(),
                    view.map_axis_y_min, view.map_axis_y_max);
                for (const float tick : y_minors) {
                    const float t = (tick - view.map_axis_y_min) / y_range;
                    const float py = plot_max.y - t * (plot_max.y - plot_min.y);
                    dl->AddLine(
                        ImVec2(plot_min.x, py),
                        ImVec2(plot_min.x - tick_len_minor, py),
                        AxisStyle::kMinorTick, AxisStyle::kMinorTickWidth);
                }
            }
        }

        // ── 悬停十字准线 ──
        // Crosshair spanning the full plot area, with coordinate
        // readout at the axis intersection points.
        // Only active when both show_crosshair and show_map_axis are on.
        if (view.show_crosshair &&
            view.hover_tooltip_visible &&
            view.hover_screen_x >= 0.0f &&
            view.hover_screen_y >= 0.0f &&
            view.image_width > 0 && view.image_height > 0) {
            const auto scr = framebuffer_to_plot_screen(
                view.hover_screen_x, view.hover_screen_y,
                canvas_rect, view.show_map_axis,
                view.image_width, view.image_height);
            const float cx = plot_min.x + scr.x;
            const float cy = plot_min.y + scr.y;

            constexpr ImU32 kCrosshairLine  = IM_COL32(255, 220, 60, 80);
            constexpr ImU32 kCrosshairBg    = IM_COL32(14,  15,  18,  200);
            constexpr ImU32 kCrosshairText  = IM_COL32(255, 220, 60, 240);
            constexpr float kCrosshairWidth = 1.0f;
            constexpr float kLabelPad = 3.0f;
            constexpr float kLabelAxisGap = 3.0f;

            dl->AddLine(ImVec2(plot_min.x, cy), ImVec2(plot_max.x, cy),
                        kCrosshairLine, kCrosshairWidth);
            dl->AddLine(ImVec2(cx, plot_min.y), ImVec2(cx, plot_max.y),
                        kCrosshairLine, kCrosshairWidth);

            // Precision from axis major-step estimate, same family as
            // fmt_label used by tick labels.
            const auto hover_prec = [](float step) -> int {
                if (step <= 0.0f || step >= 1.0f) return 0;
                int p = static_cast<int>(
                    std::ceil(-std::log10(
                        static_cast<double>(std::max(step, 1.0e-6f)))));
                return std::clamp(p, 0, 6);
            };

            const float x_step = (x_range > 0.0f)
                ? x_range / static_cast<float>(std::max(major_cnt, 1))
                : 1.0f;
            const float y_step = (y_range > 0.0f)
                ? y_range / static_cast<float>(std::max(major_cnt, 1))
                : 1.0f;

            char label_x[32], label_y[32];
            std::snprintf(label_x, sizeof(label_x), "%.*f",
                hover_prec(x_step),
                static_cast<double>(view.hover_x));
            std::snprintf(label_y, sizeof(label_y), "%.*f",
                hover_prec(y_step),
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
                    kCrosshairBg, 3.0f);
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
                    kCrosshairBg, 3.0f);
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
        constexpr ImU32 kFrameColor  = IM_COL32(220, 220, 220, 180);
        constexpr ImU32 kTickColor   = IM_COL32(200, 200, 200, 140);
        constexpr ImU32 kLabelColor  = IM_COL32(230, 230, 230, 200);

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

    draw_viewport_overlay(view, plot_min, plot_max);


    // ── 悬浮高亮标记 ──
    // Draws a crosshair+ring at the pick hit-point.  No cursor-movement
    // freshness gate — the pick result's has_hit is the single source of
    // truth.  The marker naturally clears when the next pick has no hit.
    if (view.hover_tooltip_visible &&
        view.hover_screen_x >= 0.0f &&
        view.hover_screen_y >= 0.0f &&
        view.image_width > 0 && view.image_height > 0) {
        const auto scr = framebuffer_to_plot_screen(
            view.hover_screen_x, view.hover_screen_y,
            canvas_rect, view.show_map_axis,
            view.image_width, view.image_height);
        const float cx = plot_min.x + scr.x;
        const float cy = plot_min.y + scr.y;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        constexpr float kR = 8.0f;
        constexpr ImU32 kColor = IM_COL32(255, 220, 60, 220);
        constexpr float kThick = 2.0f;
        dl->AddLine({cx - kR, cy}, {cx + kR, cy}, kColor, kThick);
        dl->AddLine({cx, cy - kR}, {cx, cy + kR}, kColor, kThick);
        dl->AddCircle({cx, cy}, kR + 2.0f, kColor, 0, kThick * 0.7f);
    }

    if (view.selected_point_visible &&
        view.selected_screen_x >= 0.0f &&
        view.selected_screen_y >= 0.0f &&
        view.image_width > 0 && view.image_height > 0) {
        const auto scr = framebuffer_to_plot_screen(
            view.selected_screen_x, view.selected_screen_y,
            canvas_rect, view.show_map_axis,
            view.image_width, view.image_height);
        const float cx = plot_min.x + scr.x;
        const float cy = plot_min.y + scr.y;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        constexpr ImU32 kSelectedColor = IM_COL32(70, 220, 255, 240);
        dl->AddCircle({cx, cy}, 12.0f, kSelectedColor, 0, 2.5f);
        dl->AddCircleFilled({cx, cy}, 3.0f, kSelectedColor);
    }

    const ImGuiIO& io = ImGui::GetIO();
    gs3d::app::ViewportFrameCmd frame;
    frame.index = view.viewport_index;
    frame.width = static_cast<std::uint32_t>(available.x);
    frame.height = static_cast<std::uint32_t>(available.y);
    // Use camera viewport (= GPU framebuffer) dimensions, not ImGui
    // content region, so the mouse→fb mapping stays pixel-accurate.
    const auto mouse_mapping = map_screen_mouse_to_framebuffer(
        io.MousePos.x, io.MousePos.y,
        canvas_rect, view.show_map_axis,
        view.image_width, view.image_height);

    frame.hovered = hovered && mouse_mapping.mouse_on_image;
    frame.active =
        active &&
        (mouse_mapping.mouse_on_image || view.box_select_dragging);
    frame.mouse_delta_x = active ? io.MouseDelta.x : 0.0f;
    frame.mouse_delta_y = active ? io.MouseDelta.y : 0.0f;
    frame.mouse_wheel = frame.hovered ? io.MouseWheel : 0.0f;
    frame.mouse_local_x = mouse_mapping.framebuffer_x;
    frame.mouse_local_y = mouse_mapping.framebuffer_y;
    frame.mouse_on_image = mouse_mapping.mouse_on_image;

    if (frame.hovered || frame.active) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }

    // Tooltip: shown whenever we have valid hover data and the cursor is on the image.
    if (frame.mouse_on_image && view.hover_tooltip_visible) {
        ImGui::SetTooltip(
            "x: %.2f\ny: %.2f\n%s: %.3f\n%s: %.2f",
            static_cast<double>(view.hover_x),
            static_cast<double>(view.hover_y),
            view.hover_primary_value_label.c_str(),
            static_cast<double>(view.hover_fold),
            view.hover_z_label.c_str(),
            static_cast<double>(view.hover_elevation)
        );
    }

    // Ctrl+左键 = 框选放大，普通左键 = 轨道旋转；两者互斥，框选时不旋转。
    const bool box_select_button_down =
        active &&
        frame.mouse_on_image &&
        io.KeyCtrl &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left);

    frame.rotate =
        active &&
        frame.mouse_on_image &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
        !io.KeyCtrl;
    frame.pan =
        active &&
        frame.mouse_on_image &&
        ImGui::IsMouseDown(ImGuiMouseButton_Right);
    frame.point_double_clicked =
        active &&
        frame.mouse_on_image &&
        !io.KeyCtrl &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

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
            view.image_width, view.image_height);
        const auto scr_curr = framebuffer_to_plot_screen(
            frame.mouse_local_x, frame.mouse_local_y,
            canvas_rect, view.show_map_axis,
            view.image_width, view.image_height);
        const ImVec2 rect_a(plot_min.x + scr_start.x,
                            plot_min.y + scr_start.y);
        const ImVec2 rect_b(plot_min.x + scr_curr.x,
                            plot_min.y + scr_curr.y);
        ImGui::GetWindowDrawList()->AddRect(
            rect_a, rect_b, IM_COL32(255, 220, 0, 255)
        );
        ImGui::GetWindowDrawList()->AddRectFilled(
            rect_a, rect_b, IM_COL32(255, 220, 0, 32)
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

    actions.viewport_frames.push_back(frame);
    view.render_requested = view.visible;

    ImGui::End();
}

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
        if (panel_title_font() != nullptr) {
            ImGui::PushFont(panel_title_font());
        }
        ImGui::TextUnformatted(state.dataset.active_dataset.c_str());
        if (panel_title_font() != nullptr) {
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

void draw_navigation_map(gs3d::app::AppState& state)
{
    if (!state.panels.navigation_map) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(240.0f, 260.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(
            kNavigationMapWindowName,
            &state.panels.navigation_map
        )) {
        ImGui::End();
        return;
    }

    auto& nm = state.navigation_map;

    // 缩略图区域：尽量撑满内容区，保持正方形
    const float avail_w = ImGui::GetContentRegionAvail().x;
    const float avail_h = ImGui::GetContentRegionAvail().y;
    const float size = std::min(avail_w, avail_h);
    if (size < 16.0f) {
        ImGui::End();
        return;
    }

    const ImVec2 img_min = ImGui::GetCursorScreenPos();
    const ImVec2 img_max = {img_min.x + size, img_min.y + size};

    // 背景填充（缩略图未就绪时显示暗色占位）
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (nm.valid && nm.texture_descriptor != VK_NULL_HANDLE) {
        // 缩略图纹理内部按 bbox 宽高比渲染，显示时用 ImGui Image 的
        // UV 范围保持宽高比，letterbox 两侧自动为 clear color。
        const float tex_aspect = nm.tex_w / nm.tex_h;
        const float img_aspect = size / size;  // 1.0 (square display)
        ImVec2 uv0{0.0f, 0.0f};
        ImVec2 uv1{1.0f, 1.0f};
        dl->AddImage(
            static_cast<ImTextureID>(
                reinterpret_cast<ImU64>(nm.texture_descriptor)),
            img_min,
            img_max,
            uv0,
            uv1
        );
    } else {
        dl->AddRectFilled(
            img_min,
            img_max,
            IM_COL32(32, 34, 40, 255)
        );
    }

    // 视野框叠加
    if (nm.view_rect_valid) {
        // view_rect 在纹理像素空间 [0, tex_w]×[0, tex_h]，
        // 纹理北在上(tex_y=0)、南在下(tex_y=tex_h)，和 ImGui
        // AddImage 的映射一致，直接缩放，不需要额外 Y 翻转。
        const float sx = size / nm.tex_w;
        const float sy = size / nm.tex_h;
        const ImVec2 rect_min{
            img_min.x + nm.view_rect_min_x * sx,
            img_min.y + nm.view_rect_min_y * sy
        };
        const ImVec2 rect_max{
            img_min.x + nm.view_rect_max_x * sx,
            img_min.y + nm.view_rect_max_y * sy
        };
        dl->AddRect(
            rect_min,
            rect_max,
            IM_COL32(255, 80, 80, 220),
            0.0f,
            0,
            2.0f
        );
    }

    ImGui::End();
}

void draw_auxiliary_panels(gs3d::app::AppState& state)
{
    if (state.panels.performance) {
        if (ImGui::Begin(
                kPerformanceWindowName,
                &state.panels.performance
            )) {
            ImGui::Text("帧率        %.1f", state.performance.fps);
            ImGui::Text(
                "帧耗时      %.2f ms",
                state.performance.frame_time_ms
            );
            ImGui::Text(
                "可见点数    %llu",
                static_cast<unsigned long long>(
                    state.performance.visible_points
                )
            );
            ImGui::Text(
                "GPU 显存    %.1f MB",
                bytes_to_mb(state.performance.gpu_memory_bytes)
            );
            ImGui::Text(
                "瓦片        %u 已加载 / %u 等待",
                state.performance.loaded_tiles,
                state.performance.pending_tiles
            );
        }
        ImGui::End();
    }

    if (state.panels.debug_log) {
        if (ImGui::Begin(kDebugLogWindowName, &state.panels.debug_log)) {
            for (const auto& line : state.debug_log.lines) {
                ImGui::TextUnformatted(line.c_str());
            }
        }
        ImGui::End();
    }

    if (state.panels.tile_inspector) {
        if (ImGui::Begin(
                kTileInspectorWindowName,
                &state.panels.tile_inspector
            )) {
            ImGui::Text(
                "已加载瓦片  %u",
                state.render_settings.loaded_tiles
            );
            ImGui::Text(
                "等待瓦片    %u",
                state.render_settings.pending_tiles
            );
            ImGui::Text(
                "GPU 驻留    %s",
                state.render_settings.cache_usage.c_str()
            );
            ImGui::Text(
                "CPU 缓存    %s",
                state.render_settings.cpu_cache_usage.c_str()
            );
        }
        ImGui::End();
    }

    if (state.panels.lod_view) {
        if (ImGui::Begin(kLodViewWindowName, &state.panels.lod_view)) {
            ImGui::Text(
                "模式        %s",
                state.performance.lod_mode.c_str()
            );
            ImGui::Text(
                "目标帧率    %.0f",
                state.render_settings.target_fps
            );
        }
        ImGui::End();
    }
}

} // namespace

void UiRoot::build_default_layout(const gs3d::app::AppState& state)
{
    const std::uint32_t signature = visible_view_signature(state);
    if (dock_layout_initialized_ &&
        dock_layout_signature_ == signature) {
        return;
    }

    const ImGuiID dockspace_id =
        ImGui::GetID("GeoScatter3D.DockSpace");
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(
        dockspace_id,
        ImGuiDockNodeFlags_DockSpace
    );
    ImGui::DockBuilderSetNodeSize(
        dockspace_id,
        ImGui::GetMainViewport()->WorkSize
    );
    const float work_width = std::max(1.0f, ImGui::GetMainViewport()->WorkSize.x);
    const float left_ratio = std::clamp(
        LayoutMetrics::kDockLeftWidth / work_width,
        0.12f,
        0.24f
    );
    const float right_ratio = std::clamp(
        LayoutMetrics::kDockRightWidth / work_width,
        0.14f,
        0.27f
    );

    const bool has_left_panels =
        state.panels.dataset ||
        state.panels.tile_inspector ||
        state.panels.lod_view ||
        state.panels.navigation_map;
    const bool has_right_panels =
        state.panels.render_settings ||
        state.panels.performance ||
        state.panels.debug_log;

    ImGuiID center_id = dockspace_id;
    ImGuiID left_id = 0;
    ImGuiID right_id = 0;
    if (has_left_panels) {
        left_id = ImGui::DockBuilderSplitNode(
            center_id,
            ImGuiDir_Left,
            left_ratio,
            nullptr,
            &center_id
        );
    }
    if (has_right_panels) {
        right_id = ImGui::DockBuilderSplitNode(
            center_id,
            ImGuiDir_Right,
            right_ratio,
            nullptr,
            &center_id
        );
    }

    // 左侧面板上下切分：上半放项目/瓦片/LOD，下半放导航图。
    // 参照用户手动拖出的布局 (config/imgui_layout.ini)。
    ImGuiID left_top_id = left_id;
    ImGuiID left_bottom_id = 0;
    if (left_id != 0) {
        left_bottom_id = ImGui::DockBuilderSplitNode(
            left_id,
            ImGuiDir_Down,
            0.33f,               // 导航图占左侧 33%
            nullptr,
            &left_top_id
        );

        if (state.panels.dataset) {
            ImGui::DockBuilderDockWindow(kDatasetWindowName, left_top_id);
        }
        if (state.panels.tile_inspector) {
            ImGui::DockBuilderDockWindow(kTileInspectorWindowName, left_top_id);
        }
        if (state.panels.lod_view) {
            ImGui::DockBuilderDockWindow(kLodViewWindowName, left_top_id);
        }
    }
    if (left_bottom_id != 0) {
        ImGui::DockBuilderDockWindow(kNavigationMapWindowName, left_bottom_id);
    }
    if (right_id != 0) {
        if (state.panels.render_settings) {
            ImGui::DockBuilderDockWindow(kRenderSettingsWindowName, right_id);
        }
        if (state.panels.performance) {
            ImGui::DockBuilderDockWindow(kPerformanceWindowName, right_id);
        }
        if (state.panels.debug_log) {
            ImGui::DockBuilderDockWindow(kDebugLogWindowName, right_id);
        }
    }

    for (const auto& view : state.render_views) {
        if (view.visible) {
            ImGui::DockBuilderDockWindow(
                render_view_window_name(view.viewport_index).c_str(),
                center_id
            );
        }
    }

    ImGui::DockBuilderFinish(dockspace_id);
    dock_layout_initialized_ = true;
    dock_layout_signature_ = signature;
}

gs3d::app::UiActions UiRoot::draw(gs3d::app::AppState& state)
{
    gs3d::app::UiActions actions;
    if (ImGui::GetIO().KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_N, false)) {
        show_first_hidden_view(state);
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiWindowFlags host_flags =
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_MenuBar;

    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(0.0f, 0.0f)
    );

    if (ImGui::Begin(kHostWindowName, nullptr, host_flags)) {
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("文件")) {
                if (ImGui::MenuItem("打开数据")) {
                    actions.open_requested = true;
                }
                if (ImGui::MenuItem("截图")) {
                    actions.screenshot_requested = true;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("视图")) {
                const bool has_hidden = std::any_of(
                    state.render_views.begin(),
                    state.render_views.end(),
                    [](const auto& view) {
                        return !view.visible;
                    }
                );
                if (ImGui::MenuItem(
                        "新建视图",
                        "Ctrl+N",
                        false,
                        has_hidden
                    )) {
                    show_first_hidden_view(state);
                }
                ImGui::Separator();
                for (auto& view : state.render_views) {
                    const auto label =
                        "视图 " +
                        std::to_string(view.viewport_index + 1);
                    ImGui::MenuItem(
                        label.c_str(),
                        nullptr,
                        &view.visible
                    );
                }
                ImGui::Separator();
                if (ImGui::MenuItem("恢复默认工作区")) {
                    dock_layout_initialized_ = false;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("窗口")) {
                ImGui::MenuItem(
                    "项目",
                    nullptr,
                    &state.panels.dataset
                );
                ImGui::MenuItem(
                    "属性",
                    nullptr,
                    &state.panels.render_settings
                );
                ImGui::MenuItem(
                    "性能",
                    nullptr,
                    &state.panels.performance
                );
                ImGui::MenuItem(
                    "瓦片",
                    nullptr,
                    &state.panels.tile_inspector
                );
                ImGui::MenuItem(
                    "细节层级",
                    nullptr,
                    &state.panels.lod_view
                );
                ImGui::MenuItem(
                    "日志",
                    nullptr,
                    &state.panels.debug_log
                );
                ImGui::MenuItem(
                    "导航图",
                    nullptr,
                    &state.panels.navigation_map
                );
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("帮助")) {
                ImGui::TextUnformatted(
                    "视图可作为标签页使用，也可拖到其他显示器。"
                );
                ImGui::TextUnformatted(
                    "默认相机相互独立，可在视图工具条启用联动。"
                );
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        ImGui::PushStyleVar(
            ImGuiStyleVar_FramePadding,
            ImVec2(5.0f, 3.0f)
        );
        ImGui::PushStyleVar(
            ImGuiStyleVar_ItemSpacing,
            ImVec2(6.0f, 4.0f)
        );
        ImGui::BeginChild(
            "##TopToolbar",
            ImVec2(0.0f, LayoutMetrics::kTopToolbarHeight),
            false,
            ImGuiWindowFlags_NoScrollbar
        );
        if (ImGui::SmallButton("打开")) {
            actions.open_requested = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("+ 视图")) {
            show_first_hidden_view(state);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("截图")) {
            actions.screenshot_requested = true;
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(176, 182, 192, 150));
        ImGui::TextUnformatted(
            state.dataset.active_dataset.empty()
                ? "未加载数据"
                : state.dataset.active_dataset.c_str()
        );
        ImGui::PopStyleColor();
        ImGui::EndChild();
        ImGui::PopStyleVar(2);

        ImDrawList* host_dl = ImGui::GetWindowDrawList();
        const ImVec2 toolbar_min = ImGui::GetItemRectMin();
        const ImVec2 toolbar_max = ImGui::GetItemRectMax();
        host_dl->AddLine(
            ImVec2(toolbar_min.x, toolbar_max.y),
            ImVec2(toolbar_max.x, toolbar_max.y),
            IM_COL32(92, 98, 108, 64),
            1.0f
        );

        build_default_layout(state);
        const float content_avail_y = ImGui::GetContentRegionAvail().y;
        const float status_h = std::min(
            LayoutMetrics::kStatusBarHeight,
            std::max(0.0f, content_avail_y)
        );
        const float dock_h = std::max(0.0f, content_avail_y - status_h);
        ImGui::DockSpace(
            ImGui::GetID("GeoScatter3D.DockSpace"),
            ImVec2(0.0f, dock_h),
            ImGuiDockNodeFlags_None
        );

        ImGui::BeginChild(
            "##StatusBar",
            ImVec2(0.0f, status_h),
            false,
            ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse
        );
        const ImVec2 status_min = ImGui::GetWindowPos();
        const ImVec2 status_max{
            status_min.x + ImGui::GetWindowSize().x,
            status_min.y + ImGui::GetWindowSize().y
        };
        host_dl->AddLine(
            ImVec2(status_min.x, status_min.y),
            ImVec2(status_max.x, status_min.y),
            IM_COL32(92, 98, 108, 56),
            1.0f
        );
        ImGui::SetCursorPosX(LayoutMetrics::kStatusInsetX);
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(178, 184, 194, 158));
        if (status_font() != nullptr) {
            ImGui::PushFont(status_font());
        }
        ImGui::Text(
            "%.1f FPS    %.2f ms    %llu 点    GPU %.1f MB    %s",
            state.status_bar.fps,
            state.performance.frame_time_ms,
            static_cast<unsigned long long>(
                state.status_bar.visible_points
            ),
            bytes_to_mb(state.status_bar.gpu_memory_bytes),
            state.status_bar.ready_state.c_str()
        );
        if (status_font() != nullptr) {
            ImGui::PopFont();
        }
        ImGui::PopStyleColor();
        ImGui::EndChild();
    }
    ImGui::End();
    ImGui::PopStyleVar(3);

    draw_dataset_panel(state);
    draw_render_settings(state, actions);
    draw_navigation_map(state);

    for (auto& view : state.render_views) {
        if (view.visible) {
            draw_viewport_window(view, actions);
        } else {
            view.detached = false;
            view.render_requested = false;
        }
    }

    draw_auxiliary_panels(state);
    return actions;
}

} // namespace gs3d::ui
