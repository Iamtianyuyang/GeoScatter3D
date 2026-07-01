#include "ui/UiRoot.hpp"

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
    // 地图轴边距（viewport-local px）
    constexpr float kLeftAxis    = 62.0f;
    constexpr float kBottomAxis  = 40.0f;
    constexpr float kTopPad      = 12.0f;
    constexpr float kRightPad    = 12.0f;
    // 信息 badge
    constexpr float kBadgePadX   = 10.0f;
    constexpr float kBadgePadY   = 8.0f;
    constexpr float kBadgeRound  = 4.0f;
    // 方向指示器 — 右下角，远离边和底部轴
    constexpr float kGizmoRadius = 14.0f;
    constexpr float kGizmoPad    = 12.0f;   // canvas 边缘到 gizmo 中心的距离
    // 比例尺 — 左下角，独立区域，锚定到 canvas（view_rect）
    constexpr float kScaleBarLen = 72.0f;
} // namespace LayoutMetrics

namespace AxisStyle {
    // 暗色地图风格色板 — 克制、低对比、专业
    constexpr ImU32 kFrame       = IM_COL32(110, 115, 125, 130);
    constexpr ImU32 kAxisLine    = IM_COL32(140, 145, 155, 170);
    constexpr ImU32 kTick        = IM_COL32(135, 140, 150, 160);
    constexpr ImU32 kMinorTick   = IM_COL32(105, 110, 120, 95);
    constexpr ImU32 kLabel       = IM_COL32(190, 195, 205, 230);
    constexpr ImU32 kGrid        = IM_COL32(70,  75,  85,  25);
    constexpr ImU32 kScaleLine   = IM_COL32(180, 185, 195, 200);
    constexpr ImU32 kScaleLabel  = IM_COL32(190, 195, 205, 215);
    // 信息 badge
    constexpr ImU32 kBadgeBg     = IM_COL32(14,  15,  18,  185);
    constexpr ImU32 kBadgeText   = IM_COL32(210, 215, 225, 245);
    // 方向指示器
    constexpr ImU32 kGizmoBg     = IM_COL32(16,  18,  22,  200);
    constexpr float kFrameWidth  = 1.0f;
    constexpr float kGridWidth   = 1.0f;

    // 两级刻度：major = 长刻度 + 标签 + 网格，minor = 短刻度（无标签无网格）
    constexpr float kMajorTickLen   = 6.0f;
    constexpr float kMinorTickLen   = 3.0f;
    constexpr float kTickWidth      = 1.0f;
    constexpr int   kMinorPerMajor  = 4;
    constexpr int   kMajorCountMin  = 4;
    constexpr int   kMajorCountMax  = 6;

    // 标签与刻度线的间距
    constexpr float kXTickToLabel = 6.0f;
    constexpr float kYTickToLabel = 7.0f;
} // namespace AxisStyle

} // namespace

namespace {

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
 * 右下角方向指示器：缩小、远离绘区边缘、降低视觉权重。
 */
/*
 * 右下角方向指示器：位置固定在 plot_rect 右下角外侧，但内部三轴方向
 * 由 ViewerApp 从相机 view matrix 每帧实时算出（存于 view.gizmo_*_axis），
 * 所以旋转主视图时 gizmo 同步旋转。
 */
void draw_orientation_gizmo(const gs3d::app::RenderViewState& view,
                            const ImVec2& canvas_min,
                            const ImVec2& canvas_max,
                            const ImVec2& plot_max)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const float r   = LayoutMetrics::kGizmoRadius;
    const float pad = LayoutMetrics::kGizmoPad;
    const ImVec2 origin{
        canvas_max.x - r - pad,
        plot_max.y - r - pad
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
 * 左下角比例尺：独立锚定在 view_rect（canvas）左下保留区，
 * 不依赖 plot_rect 坐标，与底部轴 gutter 完全分离。
 */
void draw_scale_bar(const ImVec2& canvas_min,
                    const ImVec2& canvas_max,
                    const char* label)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const float bar_w  = LayoutMetrics::kScaleBarLen;
    const float margin_bottom = 18.0f;  // 到 canvas 底边的距离
    const float margin_left   = 18.0f;  // 到 canvas 左边的距离
    const float y  = canvas_max.y - margin_bottom;
    const float x0 = canvas_min.x + margin_left;
    const float x1 = x0 + bar_w;

    dl->AddLine({x0, y}, {x1, y}, AxisStyle::kScaleLine, 1.5f);
    dl->AddLine({x0, y - 4.0f}, {x0, y + 1.0f}, AxisStyle::kScaleLine, 1.5f);
    dl->AddLine({x1, y - 4.0f}, {x1, y + 1.0f}, AxisStyle::kScaleLine, 1.5f);
    dl->AddText({x0, y - 18.0f}, AxisStyle::kScaleLabel, label);
}

/*
 * 视图叠加层：信息 badge + 比例尺 + 方向指示器。
 * canvas_* 是整个视口，plot_* 是去除地图轴边距后的实际绘图区。
 */
void draw_viewport_overlay(
    const gs3d::app::RenderViewState& view,
    const ImVec2& canvas_min,
    const ImVec2& canvas_max,
    const ImVec2& plot_min,
    const ImVec2& plot_max)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // ── 左上角信息 badge（半透明圆角矩形，放在绘区内侧）──
    char text[128];
    std::snprintf(text, sizeof(text), "%llu 点  |  %.2f ms",
        static_cast<unsigned long long>(view.points_visible),
        static_cast<double>(view.frame_time_ms));

    const ImVec2 text_sz = ImGui::CalcTextSize(text);
    const float bx0 = plot_min.x + LayoutMetrics::kBadgePadX;
    const float by0 = plot_min.y + LayoutMetrics::kBadgePadY;
    const ImVec2 box_min{bx0, by0};
    const ImVec2 box_max{bx0 + text_sz.x + 16.0f, by0 + text_sz.y + 10.0f};

    dl->AddRectFilled(box_min, box_max, AxisStyle::kBadgeBg, LayoutMetrics::kBadgeRound);
    dl->AddText({bx0 + 8.0f, by0 + 5.0f}, AxisStyle::kBadgeText, text);

    // ── 比例尺和方向指示器 ──
    if (view.show_map_axis) {
        draw_scale_bar(canvas_min, canvas_max, view.scale.c_str());
        draw_orientation_gizmo(view, canvas_min, canvas_max, plot_max);
    } else {
        draw_scale_bar(canvas_min, canvas_max, view.scale.c_str());
        draw_orientation_gizmo(view, canvas_min, canvas_max, canvas_max);
    }
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

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
    ImGui::TextDisabled(
        view.detached ? "独立窗口" : "工作区"
    );
    ImGui::SameLine();
    if (ImGui::SmallButton("适配")) {
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
    ImGui::TextDisabled(
        "左键旋转  右键/中键平移  滚轮缩放  Ctrl+左键拖框放大"
    );
    ImGui::PopStyleVar();
    ImGui::Separator();

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
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    // ── 地图轴模式：点在缩小的 plot_rect 内显示，轴在边距中绘制 ──
    const auto plot_rect = compute_plot_rect(
        view.show_map_axis,
        canvas_rect
    );
    ImVec2 plot_min{plot_rect.min_x, plot_rect.min_y};
    ImVec2 plot_max{plot_rect.max_x, plot_rect.max_y};

    bool using_map_axis = false;
    if (view.show_map_axis) {
        using_map_axis = true;
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

    // ── 地图式坐标轴 — GIS 风格 overlay（屏幕空间固定）────────────
    if (view.show_map_axis) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(canvas_min, canvas_max, true);

        // 绘区外框 — 细线、低对比
        dl->AddRect(plot_min, plot_max, AxisStyle::kFrame,
                    LayoutMetrics::kBadgeRound, 0, AxisStyle::kFrameWidth);

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

        // ---- X 轴（底部）----
        if (x_range > 0.0f) {
            const auto x_major = gs3d::render::compute_axis_ticks(
                view.map_axis_x_min, view.map_axis_x_max, major_cnt);
            const float x_major_step = (x_major.size() >= 2)
                ? (x_major[1] - x_major[0]) : 1.0f;

            // 网格线：只在 major 位置画
            for (const float tick : x_major) {
                const float t = (tick - view.map_axis_x_min) / x_range;
                const float px = plot_min.x + t * (plot_max.x - plot_min.x);
                dl->AddLine(ImVec2(px, plot_min.y), ImVec2(px, plot_max.y),
                            AxisStyle::kGrid, AxisStyle::kGridWidth);
            }

            // Major ticks + 标签
            for (const float tick : x_major) {
                const float t = (tick - view.map_axis_x_min) / x_range;
                const float px = plot_min.x + t * (plot_max.x - plot_min.x);

                dl->AddLine(
                    ImVec2(px, plot_max.y),
                    ImVec2(px, plot_max.y + AxisStyle::kMajorTickLen),
                    AxisStyle::kTick, AxisStyle::kTickWidth);

                char label[32];
                fmt_label(label, sizeof(label), tick,
                    view.map_axis_origin_x, x_major_step);
                const ImVec2 ts = ImGui::CalcTextSize(label);
                dl->AddText(
                    ImVec2(px - ts.x * 0.5f,
                           plot_max.y + AxisStyle::kMajorTickLen +
                           AxisStyle::kXTickToLabel),
                    AxisStyle::kLabel, label);
            }

            // Minor ticks（无标签）
            if (x_major.size() >= 2) {
                const auto x_minors = make_minors(
                    x_major_step, x_major.front(),
                    view.map_axis_x_min, view.map_axis_x_max);
                for (const float tick : x_minors) {
                    const float t = (tick - view.map_axis_x_min) / x_range;
                    const float px = plot_min.x + t * (plot_max.x - plot_min.x);
                    dl->AddLine(
                        ImVec2(px, plot_max.y),
                        ImVec2(px, plot_max.y + AxisStyle::kMinorTickLen),
                        AxisStyle::kMinorTick, AxisStyle::kTickWidth);
                }
            }
        }

        // ---- Y 轴（左侧）----
        if (y_range > 0.0f) {
            const auto y_major = gs3d::render::compute_axis_ticks(
                view.map_axis_y_min, view.map_axis_y_max, major_cnt);
            const float y_major_step = (y_major.size() >= 2)
                ? (y_major[1] - y_major[0]) : 1.0f;

            // 网格线：只在 major 位置画
            for (const float tick : y_major) {
                const float t = (tick - view.map_axis_y_min) / y_range;
                const float py = plot_max.y - t * (plot_max.y - plot_min.y);
                dl->AddLine(ImVec2(plot_min.x, py), ImVec2(plot_max.x, py),
                            AxisStyle::kGrid, AxisStyle::kGridWidth);
            }

            // Major ticks + 标签
            for (const float tick : y_major) {
                const float t = (tick - view.map_axis_y_min) / y_range;
                const float py = plot_max.y - t * (plot_max.y - plot_min.y);

                dl->AddLine(
                    ImVec2(plot_min.x - AxisStyle::kMajorTickLen, py),
                    ImVec2(plot_min.x, py),
                    AxisStyle::kTick, AxisStyle::kTickWidth);

                char label[32];
                fmt_label(label, sizeof(label), tick,
                    view.map_axis_origin_y, y_major_step);
                const ImVec2 ts = ImGui::CalcTextSize(label);
                dl->AddText(
                    ImVec2(plot_min.x - AxisStyle::kMajorTickLen -
                           AxisStyle::kYTickToLabel - ts.x,
                           py - ts.y * 0.5f),
                    AxisStyle::kLabel, label);
            }

            // Minor ticks（无标签）
            if (y_major.size() >= 2) {
                const auto y_minors = make_minors(
                    y_major_step, y_major.front(),
                    view.map_axis_y_min, view.map_axis_y_max);
                for (const float tick : y_minors) {
                    const float t = (tick - view.map_axis_y_min) / y_range;
                    const float py = plot_max.y - t * (plot_max.y - plot_min.y);
                    dl->AddLine(
                        ImVec2(plot_min.x - AxisStyle::kMinorTickLen, py),
                        ImVec2(plot_min.x, py),
                        AxisStyle::kMinorTick, AxisStyle::kTickWidth);
                }
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

    draw_viewport_overlay(view, canvas_min, canvas_max, plot_min, plot_max);


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
            "x: %.2f\ny: %.2f\nfold: %.3f\nelevation: %.2f",
            static_cast<double>(view.hover_x),
            static_cast<double>(view.hover_y),
            static_cast<double>(view.hover_fold),
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
        (ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
         ImGui::IsMouseDown(ImGuiMouseButton_Middle));

    if (box_select_button_down && !view.box_select_dragging) {
        view.box_select_dragging = true;
        view.box_select_start_x = frame.mouse_local_x;
        view.box_select_start_y = frame.mouse_local_y;
    }

    if (view.box_select_dragging) {
        const ImVec2 rect_a(
            canvas_min.x + view.box_select_start_x,
            canvas_min.y + view.box_select_start_y
        );
        const ImVec2 rect_b(io.MousePos.x, io.MousePos.y);
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

    if (ImGui::Begin(kDatasetWindowName, &state.panels.dataset)) {
        ImGui::TextUnformatted(state.dataset.active_dataset.c_str());
        ImGui::TextDisabled(
            "%llu 点  |  %s",
            static_cast<unsigned long long>(state.dataset.point_count),
            state.dataset.file_size.c_str()
        );
        ImGui::Separator();

        ImGui::InputTextWithHint(
            "##DatasetSearch",
            "筛选项目",
            state.dataset.search_text.data(),
            state.dataset.search_text.size()
        );

        if (ImGui::CollapsingHeader(
                "场景",
                ImGuiTreeNodeFlags_DefaultOpen
            )) {
            for (const auto& item : state.dataset.dataset_tree) {
                ImGui::Selectable(item.c_str(), false);
            }
        }

        if (ImGui::CollapsingHeader(
                "属性",
                ImGuiTreeNodeFlags_DefaultOpen
            )) {
            for (const auto& attribute : state.dataset.attributes) {
                ImGui::BulletText("%s", attribute.c_str());
            }
        }

        if (ImGui::CollapsingHeader("文件信息")) {
            ImGui::TextWrapped("路径：%s", state.dataset.path.c_str());
            ImGui::Text("格式：%s", state.dataset.format.c_str());
            ImGui::TextWrapped(
                "包围盒：%s",
                state.dataset.bounding_box.c_str()
            );
        }
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

    if (ImGui::Begin(
            kRenderSettingsWindowName,
            &state.panels.render_settings
        )) {
        ImGui::TextDisabled("点云外观");
        ImGui::Separator();

        float point_size = state.render_settings.point_size;
        if (ImGui::SliderFloat(
                "点大小",
                &point_size,
                1.0f,
                10.0f,
                "%.1f"
            )) {
            state.render_settings.point_size = point_size;
            actions.point_size_changed = true;
            actions.point_size = point_size;
        }

        // ---- 高度来源 ----
        {
            const auto& height_options =
                state.render_settings.height_by_options;
            const char* h_preview = "无";
            if (!height_options.empty()) {
                const int h_idx = std::clamp(
                    state.render_settings.height_attr_index,
                    0,
                    static_cast<int>(height_options.size()) - 1
                );
                h_preview = height_options[
                    static_cast<std::size_t>(h_idx)
                ].c_str();
            }
            if (ImGui::BeginCombo("高度来源", h_preview)) {
                for (std::size_t i = 0; i < height_options.size(); ++i) {
                    const bool selected =
                        static_cast<int>(i) ==
                        state.render_settings.height_attr_index;
                    if (ImGui::Selectable(
                            height_options[i].c_str(),
                            selected
                        )) {
                        state.render_settings.height_attr_index =
                            static_cast<int>(i);
                        actions.height_by_changed = true;
                        actions.height_by_index = static_cast<int>(i);
                    }
                }
                ImGui::EndCombo();
            }
        }

        // ---- 高度夸张 ----
        {
            float exag = state.render_settings.height_exaggeration;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.65f);
            if (ImGui::SliderFloat("高度夸张", &exag, 0.1f, 5.0f, "%.2fx")) {
                state.render_settings.height_exaggeration = exag;
                actions.height_exag_changed = true;
                actions.height_exag = exag;
            }
        }

        const auto& color_options =
            state.render_settings.color_by_options;
        const char* preview = "无";
        if (!color_options.empty()) {
            const int preview_index = std::clamp(
                state.render_settings.color_attr_index,
                0,
                static_cast<int>(color_options.size()) - 1
            );
            preview =
                color_options[
                    static_cast<std::size_t>(preview_index)
                ].c_str();
        }
        if (ImGui::BeginCombo("着色", preview)) {
            for (std::size_t i = 0; i < color_options.size(); ++i) {
                const bool selected =
                    static_cast<int>(i) ==
                    state.render_settings.color_attr_index;
                if (ImGui::Selectable(
                        color_options[i].c_str(),
                        selected
                    )) {
                    state.render_settings.color_attr_index =
                        static_cast<int>(i);
                    actions.color_by_changed = true;
                    actions.color_by_index = static_cast<int>(i);
                }
            }
            ImGui::EndCombo();
        }

        ImGui::TextDisabled("颜色映射");
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        ImGui::InvisibleButton(
            "##ColorMapPreview",
            ImVec2(width, 18.0f)
        );
        ImGui::GetWindowDrawList()->AddRectFilledMultiColor(
            start,
            {start.x + width, start.y + 18.0f},
            IM_COL32(60, 105, 215, 255),
            IM_COL32(55, 190, 175, 255),
            IM_COL32(235, 190, 75, 255),
            IM_COL32(218, 82, 76, 255)
        );

        ImGui::Spacing();
        ImGui::TextDisabled("流式加载");
        ImGui::Separator();
        ImGui::Text(
            "GPU 瓦片  %s",
            state.render_settings.cache_usage.c_str()
        );
        ImGui::Text(
            "CPU 缓存  %s",
            state.render_settings.cpu_cache_usage.c_str()
        );
        ImGui::Text(
            "缓存命中  %.1f%%",
            state.render_settings.cache_hit_rate
        );
        if (ImGui::Button("清空缓存")) {
            actions.clear_cache_requested = true;
        }
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

    const bool has_left_panels =
        state.panels.dataset ||
        state.panels.tile_inspector ||
        state.panels.lod_view;
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
            0.19f,
            nullptr,
            &center_id
        );
    }
    if (has_right_panels) {
        right_id = ImGui::DockBuilderSplitNode(
            center_id,
            ImGuiDir_Right,
            0.24f,
            nullptr,
            &center_id
        );
    }

    if (left_id != 0) {
        if (state.panels.dataset) {
            ImGui::DockBuilderDockWindow(kDatasetWindowName, left_id);
        }
        if (state.panels.tile_inspector) {
            ImGui::DockBuilderDockWindow(kTileInspectorWindowName, left_id);
        }
        if (state.panels.lod_view) {
            ImGui::DockBuilderDockWindow(kLodViewWindowName, left_id);
        }
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
            ImVec2(7.0f, 4.0f)
        );
        ImGui::BeginChild(
            "##TopToolbar",
            ImVec2(0.0f, 34.0f),
            true,
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
        ImGui::TextDisabled(
            "  %s",
            state.dataset.active_dataset.c_str()
        );
        ImGui::EndChild();
        ImGui::PopStyleVar();

        build_default_layout(state);
        constexpr float status_bar_height = 23.0f;
        ImGui::DockSpace(
            ImGui::GetID("GeoScatter3D.DockSpace"),
            ImVec2(
                0.0f,
                std::max(
                    0.0f,
                    ImGui::GetContentRegionAvail().y -
                        status_bar_height
                )
            ),
            ImGuiDockNodeFlags_None
        );

        ImGui::BeginChild(
            "##StatusBar",
            ImVec2(0.0f, status_bar_height),
            false,
            ImGuiWindowFlags_NoScrollbar
        );
        ImGui::TextDisabled(
            "%.1f FPS   %.2f ms   %llu 点   GPU %.1f MB   %s",
            state.status_bar.fps,
            state.performance.frame_time_ms,
            static_cast<unsigned long long>(
                state.status_bar.visible_points
            ),
            bytes_to_mb(state.status_bar.gpu_memory_bytes),
            state.status_bar.ready_state.c_str()
        );
        ImGui::EndChild();
    }
    ImGui::End();
    ImGui::PopStyleVar(3);

    draw_dataset_panel(state);
    draw_render_settings(state, actions);

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
