#include "ui/FloatingDockUi.hpp"

#include "ui/ColormapPreview.hpp"
#include "ui/FloatingDockLayout.hpp"
#include "ui/Theme.hpp"
#include "ui/UiPalette.hpp"
#include "ui/UiRoot.hpp"
#include "ui/ViewportCanvas.hpp"
#include "ui/Widgets.hpp"
#include "ui/WorkspaceManager.hpp"

#include "gui/UiFonts.hpp"
#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace gs3d::ui {

namespace {

// ── 布局常量（基准像素，绘制时乘 ui_scale）─────────────────────────
namespace DockMetrics {
    constexpr float kTopMargin        = 14.0f;
    constexpr float kSideMargin       = 16.0f;
    constexpr float kDockBottomMargin = 16.0f;
    constexpr float kDockPadX         = 9.0f;
    constexpr float kDockPadY         = 7.0f;
    constexpr float kItemPadY         = 7.0f;
    constexpr float kItemMinWidth     = 64.0f;
    constexpr float kItemRounding     = 19.0f;
    constexpr float kIconSize         = 23.0f;
    constexpr float kIconLabelGap     = 3.0f;
    constexpr float kCardWidth        = 360.0f;
    constexpr float kCardBottomGap    = 8.0f;
    constexpr float kCardPad          = 16.0f;
    constexpr float kCardRounding     = 20.0f;
    constexpr float kChipRounding     = 12.0f;
    constexpr float kFabMainSize      = 58.0f;
    constexpr float kFabSmallSize     = 46.0f;
    constexpr float kFabGap           = 14.0f;
    constexpr float kFabDockGap       = 16.0f;
} // namespace DockMetrics

/*
 * 玻璃件配色。ImGui 无 backdrop-filter，按设计文档的降级方案实现：
 * 「半透明表面色 + 1px 高光描边 + 柔和投影」。所有取值必须是函数
 * （palette 会被 apply_theme 整体重写，禁止缓存，见 UiPalette.hpp）。
 */
bool theme_is_dark() {
    return theme_tokens(active_theme()).dark;
}

ImVec4 glass_bg_vec4() {
    ImVec4 c = palette::kSurface;
    c.w = 0.93f;
    return c;
}

ImVec4 glass_border_vec4() {
    return theme_is_dark()
        ? ImVec4(1.0f, 1.0f, 1.0f, 0.13f)
        : ImVec4(1.0f, 1.0f, 1.0f, 0.65f);
}

ImU32 glass_shadow_color(int alpha) {
    return IM_COL32(15, 25, 45, alpha);
}

ImFont* bold_font()   { return gs3d::gui::ui_fonts().bold; }
ImFont* medium_font() { return gs3d::gui::ui_fonts().medium; }
ImFont* small_font()  { return gs3d::gui::ui_fonts().small; }
ImFont* status_font() { return gs3d::gui::ui_fonts().status; }

struct ScopedFont {
    explicit ScopedFont(ImFont* font) : pushed_(font != nullptr) {
        if (pushed_) {
            ImGui::PushFont(font);
        }
    }
    ~ScopedFont() {
        if (pushed_) {
            ImGui::PopFont();
        }
    }
    ScopedFont(const ScopedFont&) = delete;
    ScopedFont& operator=(const ScopedFont&) = delete;

private:
    bool pushed_;
};

// ── 缓动 ────────────────────────────────────────────────────────────
// 近似 CSS cubic-bezier(.2,.9,.3,1.2)：出场带轻微回弹。
float ease_out_back(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const float c1 = 1.2f;
    const float c3 = c1 + 1.0f;
    const float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

/*
 * 每帧共享的绘制上下文。shadow_layer 是全屏沉浸视口窗口的 draw list：
 * 它先于所有悬浮窗提交，往里画的投影天然位于玻璃件之下、点云之上
 * （背景 draw list 会被不透明的视口窗口盖住，不能用）。
 */
struct FrameCtx {
    float s = 1.0f;                     // ui_scale
    ImDrawList* shadow_layer = nullptr;
    ImVec2 work_pos{};
    ImVec2 work_size{};
    float dock_top = 0.0f;              // Dock 胶囊顶边（屏幕坐标）
    float dock_height = 0.0f;
};

bool is_floating_dock_overlay_window(const ImGuiWindow* window)
{
    if (window == nullptr || window->Name == nullptr) {
        return false;
    }
    constexpr char kOverlayPrefix[] = "##FloatingDock";
    return std::strncmp(
        window->Name,
        kOverlayPrefix,
        sizeof(kOverlayPrefix) - 1
    ) == 0;
}

bool pointer_over_floating_dock_overlay()
{
    const ImGuiContext* imgui = ImGui::GetCurrentContext();
    if (imgui == nullptr) {
        return false;
    }
    return is_floating_dock_overlay_window(imgui->HoveredWindow) ||
           is_floating_dock_overlay_window(imgui->ActiveIdWindow);
}

void add_glass_shadow(
    const FrameCtx& ctx,
    const ImVec2& min,
    const ImVec2& max,
    float rounding
) {
    if (ctx.shadow_layer == nullptr) {
        return;
    }
    const float s = ctx.s;
    // 双层扩张的低透明度圆角矩形近似柔影（外层更大更淡）。
    ctx.shadow_layer->AddRectFilled(
        ImVec2(min.x - 2.0f * s, min.y + 1.0f * s),
        ImVec2(max.x + 2.0f * s, max.y + 5.0f * s),
        glass_shadow_color(26),
        rounding + 2.0f * s
    );
    ctx.shadow_layer->AddRectFilled(
        ImVec2(min.x - 6.0f * s, min.y + 3.0f * s),
        ImVec2(max.x + 6.0f * s, max.y + 10.0f * s),
        glass_shadow_color(13),
        rounding + 6.0f * s
    );
}

void add_circle_shadow(
    const FrameCtx& ctx,
    const ImVec2& center,
    float radius,
    ImU32 color
) {
    if (ctx.shadow_layer == nullptr) {
        return;
    }
    ctx.shadow_layer->AddCircleFilled(
        ImVec2(center.x, center.y + 4.0f * ctx.s),
        radius + 4.0f * ctx.s,
        color
    );
}

// 玻璃窗口样式：3 个 StyleVar + 2 个 StyleColor，必须与 pop 配对。
void push_glass_window_style(float rounding, const ImVec2& padding) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, rounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, glass_bg_vec4());
    ImGui::PushStyleColor(ImGuiCol_Border, glass_border_vec4());
}

void pop_glass_window_style() {
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

void push_card_scrollbar_style(const float ui_scale)
{
    ImVec4 grab = palette::kTextFaint;
    grab.w = 0.30f;
    ImVec4 grab_hovered = palette::kAccent;
    grab_hovered.w = 0.46f;
    ImVec4 grab_active = palette::kAccent;
    grab_active.w = 0.66f;

    ImGui::PushStyleVar(
        ImGuiStyleVar_ScrollbarSize,
        std::max(7.0f, 7.0f * ui_scale)
    );
    ImGui::PushStyleVar(
        ImGuiStyleVar_ScrollbarRounding,
        4.0f * ui_scale
    );
    ImGui::PushStyleColor(
        ImGuiCol_ScrollbarBg,
        ImVec4(0.0f, 0.0f, 0.0f, 0.0f)
    );
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, grab);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, grab_hovered);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, grab_active);
}

void pop_card_scrollbar_style()
{
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);
}

constexpr ImGuiWindowFlags kOverlayWindowFlags =
    ImGuiWindowFlags_NoTitleBar |
    ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_AlwaysAutoResize |
    ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoSavedSettings |
    ImGuiWindowFlags_NoScrollbar |
    ImGuiWindowFlags_NoScrollWithMouse |
    ImGuiWindowFlags_NoDocking |
    ImGuiWindowFlags_NoFocusOnAppearing |
    ImGuiWindowFlags_NoNavFocus;

constexpr ImGuiWindowFlags kFloatingPanelWindowFlags =
    ImGuiWindowFlags_NoTitleBar |
    ImGuiWindowFlags_NoSavedSettings |
    ImGuiWindowFlags_NoScrollbar |
    ImGuiWindowFlags_NoScrollWithMouse |
    ImGuiWindowFlags_NoDocking |
    ImGuiWindowFlags_NoFocusOnAppearing |
    ImGuiWindowFlags_NoNavFocus;

// ── 描边图标（24 单位 viewBox，映射到以 c 为中心、边长 k 的方框）──
ImVec2 icon_pt(const ImVec2& c, float k, float x, float y) {
    return ImVec2(
        c.x + (x - 12.0f) / 24.0f * k,
        c.y + (y - 12.0f) / 24.0f * k
    );
}

void icon_stroke(
    ImDrawList* dl,
    const ImVec2& c,
    float k,
    ImU32 col,
    float th,
    const float (*pts)[2],
    int count,
    bool closed = false
) {
    for (int i = 0; i + 1 < count; ++i) {
        dl->AddLine(
            icon_pt(c, k, pts[i][0], pts[i][1]),
            icon_pt(c, k, pts[i + 1][0], pts[i + 1][1]),
            col, th
        );
    }
    if (closed && count >= 2) {
        dl->AddLine(
            icon_pt(c, k, pts[count - 1][0], pts[count - 1][1]),
            icon_pt(c, k, pts[0][0], pts[0][1]),
            col, th
        );
    }
}

using IconFn = void (*)(ImDrawList*, const ImVec2&, float, ImU32, float);

void icon_monitor(ImDrawList* dl, const ImVec2& c, float k, ImU32 col, float th) {
    dl->AddRect(
        icon_pt(c, k, 3.0f, 4.0f), icon_pt(c, k, 21.0f, 17.0f),
        col, k * 0.09f, 0, th
    );
    dl->AddLine(icon_pt(c, k, 8.0f, 21.0f), icon_pt(c, k, 16.0f, 21.0f), col, th);
    dl->AddLine(icon_pt(c, k, 12.0f, 17.0f), icon_pt(c, k, 12.0f, 21.0f), col, th);
}

void icon_ruler(ImDrawList* dl, const ImVec2& c, float k, ImU32 col, float th) {
    static const float body[][2] = {
        {3.0f, 17.0f}, {17.0f, 3.0f}, {21.0f, 7.0f}, {7.0f, 21.0f},
        {3.0f, 21.0f}, {3.0f, 17.0f}
    };
    icon_stroke(dl, c, k, col, th, body, 6);
    dl->AddLine(icon_pt(c, k, 13.0f, 7.0f), icon_pt(c, k, 15.0f, 9.0f), col, th);
    dl->AddLine(icon_pt(c, k, 9.0f, 11.0f), icon_pt(c, k, 11.0f, 13.0f), col, th);
}

void icon_settings(
    ImDrawList* dl,
    const ImVec2& c,
    float k,
    ImU32 col,
    float th
) {
    const float outer_r = k * 0.29f;
    const float tooth_inner_r = k * 0.36f;
    const float tooth_outer_r = k * 0.47f;
    dl->AddCircle(c, outer_r, col, 0, th);
    dl->AddCircle(c, k * 0.10f, col, 0, th);
    for (int i = 0; i < 8; ++i) {
        const float angle =
            static_cast<float>(i) * 3.14159265358979323846f / 4.0f;
        const ImVec2 inner(
            c.x + std::cos(angle) * tooth_inner_r,
            c.y + std::sin(angle) * tooth_inner_r
        );
        const ImVec2 outer(
            c.x + std::cos(angle) * tooth_outer_r,
            c.y + std::sin(angle) * tooth_outer_r
        );
        dl->AddLine(inner, outer, col, th);
    }
}

void icon_layers(ImDrawList* dl, const ImVec2& c, float k, ImU32 col, float th) {
    static const float top[][2] = {
        {12.0f, 3.0f}, {21.0f, 8.0f}, {12.0f, 13.0f}, {3.0f, 8.0f}
    };
    icon_stroke(dl, c, k, col, th, top, 4, true);
    static const float bottom[][2] = {
        {3.0f, 13.0f}, {12.0f, 18.0f}, {21.0f, 13.0f}
    };
    icon_stroke(dl, c, k, col, th, bottom, 3);
}

void icon_sliders(ImDrawList* dl, const ImVec2& c, float k, ImU32 col, float th) {
    dl->AddLine(icon_pt(c, k, 4.0f, 8.0f), icon_pt(c, k, 13.0f, 8.0f), col, th);
    dl->AddLine(icon_pt(c, k, 19.0f, 8.0f), icon_pt(c, k, 20.0f, 8.0f), col, th);
    dl->AddCircle(icon_pt(c, k, 16.0f, 8.0f), k * 0.105f, col, 0, th);
    dl->AddLine(icon_pt(c, k, 4.0f, 16.0f), icon_pt(c, k, 5.0f, 16.0f), col, th);
    dl->AddLine(icon_pt(c, k, 11.0f, 16.0f), icon_pt(c, k, 20.0f, 16.0f), col, th);
    dl->AddCircle(icon_pt(c, k, 8.0f, 16.0f), k * 0.105f, col, 0, th);
}

void icon_reset(ImDrawList* dl, const ImVec2& c, float k, ImU32 col, float th) {
    const float r = k * 0.375f;
    dl->PathArcTo(c, r, 2.6f, 7.6f);
    dl->PathStroke(col, 0, th);
    // 箭头在弧线起点，指向切线方向。
    const ImVec2 tip(c.x + r * std::cos(2.6f), c.y + r * std::sin(2.6f));
    dl->AddLine(tip, ImVec2(tip.x - k * 0.16f, tip.y + k * 0.03f), col, th);
    dl->AddLine(tip, ImVec2(tip.x + k * 0.02f, tip.y + k * 0.17f), col, th);
}

void icon_axis(ImDrawList* dl, const ImVec2& c, float k, ImU32 col, float th) {
    dl->AddLine(icon_pt(c, k, 12.0f, 3.0f), icon_pt(c, k, 12.0f, 21.0f), col, th);
    dl->AddLine(icon_pt(c, k, 3.0f, 12.0f), icon_pt(c, k, 21.0f, 12.0f), col, th);
    dl->AddLine(icon_pt(c, k, 10.0f, 5.5f), icon_pt(c, k, 12.0f, 3.0f), col, th);
    dl->AddLine(icon_pt(c, k, 14.0f, 5.5f), icon_pt(c, k, 12.0f, 3.0f), col, th);
    dl->AddLine(icon_pt(c, k, 18.5f, 10.0f), icon_pt(c, k, 21.0f, 12.0f), col, th);
    dl->AddLine(icon_pt(c, k, 18.5f, 14.0f), icon_pt(c, k, 21.0f, 12.0f), col, th);
}

void icon_world_axis(
    ImDrawList* dl,
    const ImVec2& c,
    float k,
    ImU32 col,
    float th
) {
    static const float outline[][2] = {
        {12.0f, 2.5f}, {3.5f, 7.0f}, {3.5f, 17.0f},
        {12.0f, 21.5f}, {20.5f, 17.0f}, {20.5f, 7.0f}
    };
    icon_stroke(dl, c, k, col, th, outline, 6, true);
    dl->AddLine(
        icon_pt(c, k, 3.5f, 7.0f),
        icon_pt(c, k, 12.0f, 12.0f),
        col,
        th
    );
    dl->AddLine(
        icon_pt(c, k, 20.5f, 7.0f),
        icon_pt(c, k, 12.0f, 12.0f),
        col,
        th
    );
    dl->AddLine(
        icon_pt(c, k, 12.0f, 12.0f),
        icon_pt(c, k, 12.0f, 21.5f),
        col,
        th
    );
}

void icon_crosshair(ImDrawList* dl, const ImVec2& c, float k, ImU32 col, float th) {
    dl->AddCircle(c, k * 0.29f, col, 0, th);
    dl->AddLine(icon_pt(c, k, 12.0f, 2.0f), icon_pt(c, k, 12.0f, 6.0f), col, th);
    dl->AddLine(icon_pt(c, k, 12.0f, 18.0f), icon_pt(c, k, 12.0f, 22.0f), col, th);
    dl->AddLine(icon_pt(c, k, 2.0f, 12.0f), icon_pt(c, k, 6.0f, 12.0f), col, th);
    dl->AddLine(icon_pt(c, k, 18.0f, 12.0f), icon_pt(c, k, 22.0f, 12.0f), col, th);
}

void icon_crosshair_style(
    ImDrawList* dl,
    const ImVec2& c,
    float k,
    ImU32 col,
    float th
) {
    icon_crosshair(dl, c, k, col, th);
    dl->AddCircle(c, k * 0.10f, col, 0, th);
    const float r = k * 0.40f;
    for (int i = 0; i < 8; i += 2) {
        const float a0 = static_cast<float>(i) * 0.785398f;
        const float a1 = static_cast<float>(i + 1) * 0.785398f;
        dl->PathArcTo(c, r, a0, a1, 4);
        dl->PathStroke(col, 0, std::max(1.0f, th * 0.65f));
    }
}

void icon_expand(ImDrawList* dl, const ImVec2& c, float k, ImU32 col, float th) {
    static const float tl[][2] = {{4.0f, 9.0f}, {4.0f, 4.0f}, {9.0f, 4.0f}};
    static const float tr[][2] = {{20.0f, 9.0f}, {20.0f, 4.0f}, {15.0f, 4.0f}};
    static const float bl[][2] = {{4.0f, 15.0f}, {4.0f, 20.0f}, {9.0f, 20.0f}};
    static const float br[][2] = {{20.0f, 15.0f}, {20.0f, 20.0f}, {15.0f, 20.0f}};
    icon_stroke(dl, c, k, col, th, tl, 3);
    icon_stroke(dl, c, k, col, th, tr, 3);
    icon_stroke(dl, c, k, col, th, bl, 3);
    icon_stroke(dl, c, k, col, th, br, 3);
}

void icon_camera(ImDrawList* dl, const ImVec2& c, float k, ImU32 col, float th) {
    dl->AddRect(
        icon_pt(c, k, 3.0f, 6.5f), icon_pt(c, k, 21.0f, 20.0f),
        col, k * 0.12f, 0, th
    );
    dl->AddCircle(icon_pt(c, k, 12.0f, 13.0f), k * 0.15f, col, 0, th);
    static const float notch[][2] = {
        {9.0f, 6.5f}, {10.2f, 4.0f}, {13.8f, 4.0f}, {15.0f, 6.5f}
    };
    icon_stroke(dl, c, k, col, th, notch, 4);
}

void icon_close(ImDrawList* dl, const ImVec2& c, float k, ImU32 col, float th) {
    dl->AddLine(icon_pt(c, k, 7.0f, 7.0f), icon_pt(c, k, 17.0f, 17.0f), col, th);
    dl->AddLine(icon_pt(c, k, 17.0f, 7.0f), icon_pt(c, k, 7.0f, 17.0f), col, th);
}

void icon_area(ImDrawList* dl, const ImVec2& c, float k, ImU32 col, float th) {
    dl->AddLine(icon_pt(c, k, 4.0f, 20.0f), icon_pt(c, k, 20.0f, 4.0f), col, th);
    dl->AddLine(icon_pt(c, k, 4.0f, 20.0f), icon_pt(c, k, 10.0f, 20.0f), col, th);
    dl->AddLine(icon_pt(c, k, 4.0f, 20.0f), icon_pt(c, k, 4.0f, 14.0f), col, th);
}

void icon_height(ImDrawList* dl, const ImVec2& c, float k, ImU32 col, float th) {
    dl->AddLine(icon_pt(c, k, 12.0f, 3.0f), icon_pt(c, k, 12.0f, 21.0f), col, th);
    dl->AddLine(icon_pt(c, k, 8.0f, 7.0f), icon_pt(c, k, 12.0f, 3.0f), col, th);
    dl->AddLine(icon_pt(c, k, 16.0f, 7.0f), icon_pt(c, k, 12.0f, 3.0f), col, th);
    dl->AddLine(icon_pt(c, k, 8.0f, 17.0f), icon_pt(c, k, 12.0f, 21.0f), col, th);
    dl->AddLine(icon_pt(c, k, 16.0f, 17.0f), icon_pt(c, k, 12.0f, 21.0f), col, th);
}

void icon_profile(ImDrawList* dl, const ImVec2& c, float k, ImU32 col, float th) {
    static const float line[][2] = {
        {4.0f, 18.0f}, {10.0f, 12.0f}, {14.0f, 16.0f}, {20.0f, 8.0f}
    };
    icon_stroke(dl, c, k, col, th, line, 4);
}

void icon_angle(ImDrawList* dl, const ImVec2& c, float k, ImU32 col, float th) {
    dl->AddCircle(c, k * 0.33f, col, 0, th);
    dl->AddCircleFilled(c, k * 0.09f, col);
}

void icon_pin(ImDrawList* dl, const ImVec2& c, float k, ImU32 col, float th) {
    static const float drop[][2] = {
        {12.0f, 21.0f}, {6.0f, 13.0f}, {6.5f, 7.5f}, {12.0f, 4.0f},
        {17.5f, 7.5f}, {18.0f, 13.0f}, {12.0f, 21.0f}
    };
    icon_stroke(dl, c, k, col, th, drop, 7);
    dl->AddCircle(icon_pt(c, k, 12.0f, 10.0f), k * 0.10f, col, 0, th);
}

void icon_sight(ImDrawList* dl, const ImVec2& c, float k, ImU32 col, float th) {
    static const float wave[][2] = {
        {3.0f, 12.0f}, {7.0f, 12.0f}, {10.0f, 4.0f}, {14.0f, 20.0f},
        {17.0f, 12.0f}, {21.0f, 12.0f}
    };
    icon_stroke(dl, c, k, col, th, wave, 6);
}

void icon_stats_box(ImDrawList* dl, const ImVec2& c, float k, ImU32 col, float th) {
    // 虚线选框：四边各两段短线。
    const float seg[][4] = {
        {4.0f, 4.0f, 9.0f, 4.0f},   {15.0f, 4.0f, 20.0f, 4.0f},
        {4.0f, 20.0f, 9.0f, 20.0f}, {15.0f, 20.0f, 20.0f, 20.0f},
        {4.0f, 4.0f, 4.0f, 9.0f},   {4.0f, 15.0f, 4.0f, 20.0f},
        {20.0f, 4.0f, 20.0f, 9.0f}, {20.0f, 15.0f, 20.0f, 20.0f},
    };
    for (const auto& l : seg) {
        dl->AddLine(icon_pt(c, k, l[0], l[1]), icon_pt(c, k, l[2], l[3]), col, th);
    }
}

// ── 通用小控件 ──────────────────────────────────────────────────────

void set_tooltip(const char* text) {
    if (text != nullptr && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", text);
    }
}

void toggle_card(
    gs3d::app::DockUiState& dock,
    gs3d::app::DockCard card
) {
    dock.hint_seconds_left = 0.0f;
    if (dock.open_card == card) {
        dock.open_card = gs3d::app::DockCard::kNone;
        return;
    }
    dock.open_card = card;
    dock.anim_card = card;
}

// 圆形图标按钮（视角 pill / FAB 内部使用）。
bool circle_icon_button(
    const char* id,
    float diameter,
    IconFn icon,
    bool active,
    float ui_scale,
    const char* tooltip
) {
    ImGui::InvisibleButton(id, ImVec2(diameter, diameter));
    const ImVec2 rmin = ImGui::GetItemRectMin();
    const ImVec2 center(
        rmin.x + diameter * 0.5f,
        rmin.y + diameter * 0.5f
    );
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (active) {
        dl->AddCircleFilled(center, diameter * 0.5f, to_u32(palette::kAccent, 40));
    } else if (hovered) {
        dl->AddCircleFilled(center, diameter * 0.5f, to_u32(palette::kAccent, 26));
    }
    const ImU32 fg = (active || hovered)
        ? to_u32(palette::kAccent, 255)
        : to_u32(palette::kTextDim, 230);
    icon(dl, center, diameter * 0.48f, fg, 2.0f * ui_scale);
    set_tooltip(tooltip);
    return ImGui::IsItemClicked(ImGuiMouseButton_Left);
}

// Telegram 式开关。旋钮位置经 state storage 做每帧缓动。
bool glass_switch(const char* id, bool* v, float s) {
    const float w = 38.0f * s;
    const float h = 22.0f * s;
    ImGui::InvisibleButton(id, ImVec2(w, h));
    const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    if (clicked) {
        *v = !*v;
    }
    const ImVec2 rmin = ImGui::GetItemRectMin();
    ImGuiStorage* storage = ImGui::GetStateStorage();
    float* t = storage->GetFloatRef(
        ImGui::GetItemID(),
        *v ? 1.0f : 0.0f
    );
    const float dt = std::min(ImGui::GetIO().DeltaTime, 0.05f);
    const float target = *v ? 1.0f : 0.0f;
    if (*t < target) {
        *t = std::min(target, *t + dt * 9.0f);
    } else if (*t > target) {
        *t = std::max(target, *t - dt * 9.0f);
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec4 off_col = theme_is_dark()
        ? ImVec4(0.35f, 0.39f, 0.45f, 1.0f)
        : ImVec4(0.80f, 0.84f, 0.89f, 1.0f);
    const ImVec4 on_col = palette::kAccent;
    const ImVec4 track(
        off_col.x + (on_col.x - off_col.x) * *t,
        off_col.y + (on_col.y - off_col.y) * *t,
        off_col.z + (on_col.z - off_col.z) * *t,
        1.0f
    );
    dl->AddRectFilled(
        rmin,
        ImVec2(rmin.x + w, rmin.y + h),
        ImGui::ColorConvertFloat4ToU32(track),
        h * 0.5f
    );
    const float knob_r = h * 0.5f - 2.0f * s;
    const float knob_x0 = rmin.x + 2.0f * s + knob_r;
    const float knob_x1 = rmin.x + w - 2.0f * s - knob_r;
    const ImVec2 knob(
        knob_x0 + (knob_x1 - knob_x0) * *t,
        rmin.y + h * 0.5f
    );
    dl->AddCircleFilled(
        ImVec2(knob.x, knob.y + 1.0f * s),
        knob_r,
        IM_COL32(0, 0, 0, 50)
    );
    dl->AddCircleFilled(knob, knob_r, IM_COL32(255, 255, 255, 255));
    return clicked;
}

// 卡片里的「标签左、开关右」一行。
bool switch_row(const char* label, bool* v, float s) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    const float switch_w = 38.0f * s;
    const float shift = ImGui::GetContentRegionAvail().x - switch_w;
    if (shift > 0.0f) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + shift);
    }
    std::string id = std::string("##switch_") + label;
    return glass_switch(id.c_str(), v, s);
}

// 「标签左、控件右」行首：输出标签并把光标移到右侧控件起点。
void begin_control_row(const char* label, float control_width) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    const float shift = ImGui::GetContentRegionAvail().x - control_width;
    if (shift > 0.0f) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + shift);
    }
    ImGui::SetNextItemWidth(control_width);
}

// 卡片标题行：标题 + 右侧 ✕。返回 true 表示点击了关闭。
bool card_header(const char* title, float s) {
    bool close_clicked = false;
    {
        ScopedFont font(medium_font());
        ImGui::TextUnformatted(title);
    }
    ImGui::SameLine();
    const float btn = 22.0f * s;
    const float shift = ImGui::GetContentRegionAvail().x - btn;
    if (shift > 0.0f) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + shift);
    }
    ImGui::InvisibleButton("##close_card", ImVec2(btn, btn));
    const ImVec2 rmin = ImGui::GetItemRectMin();
    const ImVec2 center(rmin.x + btn * 0.5f, rmin.y + btn * 0.5f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (ImGui::IsItemHovered()) {
        dl->AddCircleFilled(center, btn * 0.5f, to_u32(palette::kText, 16));
    }
    icon_close(dl, center, btn * 0.62f, to_u32(palette::kTextDim, 220), 1.8f * s);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        close_clicked = true;
    }
    ImGui::Spacing();
    return close_clicked;
}

void card_section_label(const char* label) {
    ScopedFont font(small_font());
    ImGui::PushStyleColor(ImGuiCol_Text, to_u32(palette::kTextFaint, 220));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
}

std::string format_thousands(std::uint64_t value) {
    std::string digits = std::to_string(value);
    std::string out;
    out.reserve(digits.size() + digits.size() / 3);
    int counter = 0;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (counter != 0 && counter % 3 == 0) {
            out.push_back(',');
        }
        out.push_back(*it);
        ++counter;
    }
    std::reverse(out.begin(), out.end());
    return out;
}

void key_value_row(
    const char* label,
    const std::string& value,
    float label_width
) {
    ImGui::PushID(label);
    if (ImGui::BeginTable(
            "##kv",
            2,
            ImGuiTableFlags_SizingStretchProp |
                ImGuiTableFlags_PadOuterX
        )) {
        ImGui::TableSetupColumn(
            "label",
            ImGuiTableColumnFlags_WidthFixed,
            label_width
        );
        ImGui::TableSetupColumn(
            "value",
            ImGuiTableColumnFlags_WidthStretch
        );
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        {
            ScopedFont font(small_font());
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                to_u32(palette::kTextFaint, 230)
            );
            ImGui::TextUnformatted(label);
            ImGui::PopStyleColor();
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::TextWrapped("%s", value.c_str());
        ImGui::EndTable();
    }
    ImGui::PopID();
}

void draw_navigation_map_preview(
    gs3d::app::AppState& state,
    int active_index,
    float s,
    float available_height = 0.0f
) {
    auto& nm = gs3d::app::navigation_map_for_view(state, active_index);
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = available_height > 0.0f
        ? available_height
        : width;
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const auto layout = compute_navigation_preview_layout(
        cursor.x,
        cursor.y,
        width,
        height,
        nm.tex_w,
        nm.tex_h
    );
    const ImVec2 min(
        layout.container.x,
        layout.container.y
    );
    const ImVec2 max(
        min.x + layout.container.width,
        min.y + layout.container.height
    );
    const ImVec2 image_min(layout.image.x, layout.image.y);
    const ImVec2 image_max(
        image_min.x + layout.image.width,
        image_min.y + layout.image.height
    );
    ImGui::InvisibleButton(
        "##DockNavigationMap",
        ImVec2(layout.container.width, layout.container.height)
    );
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(
        min,
        max,
        to_u32(palette::kViewportBg, 255),
        11.0f * s
    );
    dl->PushClipRect(min, max, true);
    if (nm.valid && nm.texture_descriptor != VK_NULL_HANDLE) {
        dl->AddImage(
            static_cast<ImTextureID>(
                reinterpret_cast<ImU64>(nm.texture_descriptor)
            ),
            image_min,
            image_max
        );
    }
    if (nm.view_rect_valid && nm.tex_w > 0.0f && nm.tex_h > 0.0f) {
        const float sx = layout.image.width / nm.tex_w;
        const float sy = layout.image.height / nm.tex_h;
        dl->AddRect(
            ImVec2(
                image_min.x + nm.view_rect_min_x * sx,
                image_min.y + nm.view_rect_min_y * sy
            ),
            ImVec2(
                image_min.x + nm.view_rect_max_x * sx,
                image_min.y + nm.view_rect_max_y * sy
            ),
            to_u32(palette::kRed, 235),
            0.0f,
            0,
            2.0f * s
        );
    }
    dl->PopClipRect();
    dl->AddRect(
        min,
        max,
        to_u32(palette::kBorder, 150),
        11.0f * s
    );
    {
        ScopedFont font(status_font());
        const char* caption = "导航图 · 红框 = 主视图可见 XY 范围";
        const ImVec2 text_size = ImGui::CalcTextSize(caption);
        dl->AddRectFilled(
            ImVec2(min.x + 8.0f * s, max.y - text_size.y - 10.0f * s),
            ImVec2(
                min.x + text_size.x + 16.0f * s,
                max.y - 5.0f * s
            ),
            IM_COL32(16, 22, 34, 190),
            5.0f * s
        );
        dl->AddText(
            ImVec2(min.x + 12.0f * s, max.y - text_size.y - 7.0f * s),
            IM_COL32(235, 240, 247, 235),
            caption
        );
    }
}

void draw_floating_navigation_map(
    const FrameCtx& ctx,
    gs3d::app::AppState& state,
    int active_index
) {
    auto& dock = state.dock_ui;
    if (!dock.navigation_map_floating) {
        return;
    }

    const float s = ctx.s;
    const ImVec2 min_size(300.0f * s, 360.0f * s);
    const ImVec2 max_size(
        std::max(
            min_size.x,
            std::min(720.0f * s, ctx.work_size.x - 32.0f * s)
        ),
        std::max(
            min_size.y,
            std::min(800.0f * s, ctx.work_size.y - 32.0f * s)
        )
    );
    ImGui::SetNextWindowPos(
        ImVec2(
            ctx.work_pos.x + DockMetrics::kSideMargin * s,
            ctx.work_pos.y + 112.0f * s
        ),
        ImGuiCond_Appearing
    );
    ImGui::SetNextWindowSize(
        ImVec2(392.0f * s, 470.0f * s),
        ImGuiCond_Appearing
    );
    ImGui::SetNextWindowSizeConstraints(
        min_size,
        max_size
    );
    push_glass_window_style(
        DockMetrics::kCardRounding * s,
        ImVec2(DockMetrics::kCardPad * s, DockMetrics::kCardPad * s)
    );
    ImGui::PushStyleColor(
        ImGuiCol_ResizeGrip,
        to_u32(palette::kAccent, 72)
    );
    ImGui::PushStyleColor(
        ImGuiCol_ResizeGripHovered,
        to_u32(palette::kAccent, 155)
    );
    ImGui::PushStyleColor(
        ImGuiCol_ResizeGripActive,
        to_u32(palette::kAccent, 230)
    );
    if (ImGui::Begin(
            "##FloatingDockNavigationMap",
            nullptr,
            kFloatingPanelWindowFlags
        )) {
        const ImVec2 wmin = ImGui::GetWindowPos();
        const ImVec2 wmax(
            wmin.x + ImGui::GetWindowSize().x,
            wmin.y + ImGui::GetWindowSize().y
        );
        add_glass_shadow(
            ctx,
            wmin,
            wmax,
            DockMetrics::kCardRounding * s
        );

        if (card_header("导航图", s)) {
            dock.navigation_map_floating = false;
        } else {
            {
                ScopedFont font(small_font());
                ImGui::PushStyleColor(
                    ImGuiCol_Text,
                    to_u32(palette::kTextDim, 205)
                );
                ImGui::Text(
                    "视图 %d · 拖动标题移动 · 拖动右下角缩放",
                    active_index + 1
                );
                ImGui::PopStyleColor();
            }
            ImGui::Spacing();
            const float preview_height = std::max(
                160.0f * s,
                ImGui::GetContentRegionAvail().y - 3.0f * s
            );
            draw_navigation_map_preview(
                state,
                active_index,
                s,
                preview_height
            );
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(3);
    pop_glass_window_style();
}

// ── 视图解析 ────────────────────────────────────────────────────────

// 沉浸布局一次全屏显示一个视图：优先当前活动视图，其次第一个可见
// 视图；都不可见时强制点亮活动视图。
int resolve_immersive_view(gs3d::app::AppState& state) {
    const int count = static_cast<int>(state.render_views.size());
    if (count <= 0) {
        return -1;
    }
    const auto is_candidate = [](const auto& view) {
        return view.visible &&
               !view.detached &&
               !view.force_undock_next_frame;
    };
    int index = std::clamp(state.active_viewport_index, 0, count - 1);
    if (!is_candidate(
            state.render_views[static_cast<std::size_t>(index)])) {
        index = -1;
        for (int i = 0; i < count; ++i) {
            if (is_candidate(
                    state.render_views[static_cast<std::size_t>(i)])) {
                index = i;
                break;
            }
        }
    }
    if (index < 0) {
        for (int i = 0; i < count; ++i) {
            auto& candidate =
                state.render_views[static_cast<std::size_t>(i)];
            if (!candidate.detached &&
                !candidate.force_undock_next_frame) {
                candidate.visible = true;
                index = i;
                break;
            }
        }
    }
    if (index < 0) {
        return -1;
    }
    auto& view = state.render_views[static_cast<std::size_t>(index)];
    state.active_viewport_index = index;
    return index;
}

// ── 渲染设置命令（与 RenderSettingsPanel 的约定一致）────────────────
gs3d::app::RenderSettingsCommand& add_render_settings_command(
    gs3d::app::UiActions& actions,
    int viewport_index
) {
    actions.render_settings_commands.emplace_back();
    auto& command = actions.render_settings_commands.back();
    command.has_viewport_scope = true;
    command.viewport_indices = {viewport_index};
    return command;
}

// ── 悬浮件：左上文件 chip + 性能 HUD ────────────────────────────────

void draw_file_chip(
    const FrameCtx& ctx,
    gs3d::app::AppState& state,
    const bool show_map_axis
) {
    const float s = ctx.s;
    const auto top = compute_floating_dock_top_overlay_layout(
        ctx.work_pos.x,
        ctx.work_pos.y,
        s,
        show_map_axis
    );
    ImGui::SetNextWindowPos(
        ImVec2(
            top.left_x,
            top.primary_y
        ),
        ImGuiCond_Always
    );
    push_glass_window_style(
        DockMetrics::kChipRounding * s,
        ImVec2(14.0f * s, 8.0f * s)
    );
    if (ImGui::Begin("##FloatingDockFileChip", nullptr, kOverlayWindowFlags)) {
        add_glass_shadow(
            ctx,
            ImGui::GetWindowPos(),
            ImVec2(
                ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                ImGui::GetWindowPos().y + ImGui::GetWindowSize().y
            ),
            DockMetrics::kChipRounding * s
        );
        // 呼吸绿点（数据就绪指示）。
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float dot_r = 3.5f * s;
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const float row_h = ImGui::GetFontSize();
        const ImVec2 dot_c(cursor.x + dot_r, cursor.y + row_h * 0.5f);
        dl->AddCircleFilled(dot_c, dot_r + 2.5f * s, to_u32(palette::kGreen, 60));
        dl->AddCircleFilled(dot_c, dot_r, to_u32(palette::kGreen, 255));
        ImGui::Dummy(ImVec2(dot_r * 2.0f + 6.0f * s, row_h));
        ImGui::SameLine();
        {
            ScopedFont font(bold_font());
            ImGui::TextUnformatted(
                state.dataset.active_dataset.empty()
                    ? "未加载数据"
                    : state.dataset.active_dataset.c_str()
            );
        }
        ImGui::SameLine();
        {
            ScopedFont font(small_font());
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                to_u32(palette::kTextDim, 220)
            );
            ImGui::Text(
                "%s 点 · %s",
                format_thousands(state.dataset.point_count).c_str(),
                state.dataset.format.empty()
                    ? "GS3D"
                    : state.dataset.format.c_str()
            );
            ImGui::PopStyleColor();
        }
        const bool hovered = ImGui::IsWindowHovered(
            ImGuiHoveredFlags_AllowWhenBlockedByActiveItem
        );
        if (hovered) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::GetWindowDrawList()->AddRect(
                ImGui::GetWindowPos(),
                ImVec2(
                    ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                    ImGui::GetWindowPos().y + ImGui::GetWindowSize().y
                ),
                to_u32(palette::kAccent, 120),
                DockMetrics::kChipRounding * s,
                0,
                1.0f
            );
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                toggle_card(state.dock_ui, gs3d::app::DockCard::kData);
            }
        }
    }
    ImGui::End();
    pop_glass_window_style();
}

void draw_perf_chip(
    const FrameCtx& ctx,
    gs3d::app::AppState& state,
    const bool show_map_axis
) {
    const float s = ctx.s;
    const auto top = compute_floating_dock_top_overlay_layout(
        ctx.work_pos.x,
        ctx.work_pos.y,
        s,
        show_map_axis
    );
    ImGui::SetNextWindowPos(
        ImVec2(
            top.left_x,
            top.secondary_y
        ),
        ImGuiCond_Always
    );
    // 深色半透明角标（固定深色：它贴在暗色点云画布上）。
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f * s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(11.0f * s, 5.0f * s)
    );
    ImGui::PushStyleColor(
        ImGuiCol_WindowBg,
        ImVec4(0.078f, 0.094f, 0.125f, 0.62f)
    );
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.10f));
    const ImGuiWindowFlags flags = kOverlayWindowFlags;
    if (ImGui::Begin("##FloatingDockPerfChip", nullptr, flags)) {
        ScopedFont font(status_font());
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImVec4(0.86f, 0.89f, 0.94f, 1.0f)
        );
        ImGui::Text(
            "%.0f FPS · %.2f ms · %s 点 · GPU %.1f MB",
            static_cast<double>(state.status_bar.fps),
            static_cast<double>(state.performance.frame_time_ms),
            format_thousands(state.status_bar.visible_points).c_str(),
            static_cast<double>(
                bytes_to_mb(state.status_bar.gpu_memory_bytes)
            )
        );
        ImGui::PopStyleColor();
        if (ImGui::IsWindowHovered(
                ImGuiHoveredFlags_AllowWhenBlockedByActiveItem
            )) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                toggle_card(
                    state.dock_ui,
                    gs3d::app::DockCard::kPerformance
                );
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

// ── 右上视角工具 pill ───────────────────────────────────────────────

bool draw_camera_pill_buttons(
    gs3d::app::RenderViewState& view,
    gs3d::app::UiActions& actions,
    const bool style_active,
    const float s
) {
    const float btn = 36.0f * s;
    if (circle_icon_button(
            "##pill_reset", btn, icon_reset, false, s, "复位视角")) {
        actions.reset_camera_index = view.viewport_index;
    }
    ImGui::SameLine();
    if (circle_icon_button(
            "##pill_axis", btn, icon_axis,
            view.show_map_axis, s, "地图轴")) {
        view.show_map_axis = !view.show_map_axis;
        if (view.show_map_axis) {
            view.show_world_axis = false;
        }
    }
    ImGui::SameLine();
    if (circle_icon_button(
            "##pill_world_axis", btn, icon_world_axis,
            view.show_world_axis, s, "世界轴")) {
        view.show_world_axis = !view.show_world_axis;
        if (view.show_world_axis) {
            view.show_map_axis = false;
            view.show_crosshair = false;
        }
    }
    ImGui::SameLine();
    if (circle_icon_button(
            "##pill_crosshair", btn, icon_crosshair,
            view.show_crosshair, s, "十字准线")) {
        view.show_crosshair = !view.show_crosshair;
        if (view.show_crosshair && !view.show_map_axis) {
            view.show_map_axis = true;
            view.show_world_axis = false;
        }
    }
    ImGui::SameLine();
    const bool style_clicked = circle_icon_button(
        "##pill_crosshair_style",
        btn,
        icon_crosshair_style,
        style_active,
        s,
        "准星样式"
    );
    ImGui::SameLine();
    if (circle_icon_button(
            "##pill_fullscreen", btn, icon_expand,
            false, s, "全屏切换")) {
        actions.toggle_fullscreen_requested = true;
    }
    return style_clicked;
}

void draw_detached_reticle_popup(gs3d::app::RenderViewState& view) {
    if (!ImGui::BeginPopup("##DetachedReticleStyle")) {
        return;
    }
    ImGui::TextUnformatted("十字准线");
    if (view.show_crosshair) {
        ImVec4 crosshair =
            ImGui::ColorConvertU32ToFloat4(view.crosshair_color);
        float color[4] = {
            crosshair.x,
            crosshair.y,
            crosshair.z,
            crosshair.w
        };
        if (ImGui::ColorEdit4(
                "颜色##DetachedCrosshairColor",
                color,
                ImGuiColorEditFlags_NoInputs
            )) {
            view.crosshair_color = ImGui::ColorConvertFloat4ToU32(
                ImVec4(color[0], color[1], color[2], color[3])
            );
        }
        ImGui::SameLine();
        if (widgets::Chip("恢复默认##DetachedCrosshair")) {
            view.crosshair_color =
                IM_COL32(0xF1, 0xC2, 0x1B, 0xFF);
        }
    } else {
        ImGui::TextDisabled("先启用十字准线后可设置颜色");
    }
    ImGui::Separator();
    ImGui::TextUnformatted("拾取准星");
    ImVec4 reticle = ImGui::ColorConvertU32ToFloat4(view.reticle_color);
    float color[4] = {
        reticle.x,
        reticle.y,
        reticle.z,
        reticle.w
    };
    if (ImGui::ColorEdit4(
            "颜色##DetachedReticleColor",
            color,
            ImGuiColorEditFlags_NoInputs
        )) {
        view.reticle_color = ImGui::ColorConvertFloat4ToU32(
            ImVec4(color[0], color[1], color[2], color[3])
        );
    }
    ImGui::SameLine();
    if (widgets::Chip("恢复默认##DetachedReticle")) {
        view.reticle_color =
            IM_COL32(0xF1, 0xC2, 0x1B, 0xFF);
    }
    ImGui::Separator();
    ImGui::TextDisabled("左键旋转 · 右键平移 · 滚轮缩放 · F 聚焦");
    ImGui::EndPopup();
}

void draw_camera_pill(
    const FrameCtx& ctx,
    gs3d::app::RenderViewState& view,
    gs3d::app::UiActions& actions,
    gs3d::app::DockUiState& dock
) {
    const float s = ctx.s;
    const float btn = 36.0f * s;
    const auto top = compute_floating_dock_top_overlay_layout(
        ctx.work_pos.x,
        ctx.work_pos.y,
        s,
        view.show_map_axis
    );
    ImGui::SetNextWindowPos(
        ImVec2(
            ctx.work_pos.x + ctx.work_size.x - DockMetrics::kSideMargin * s,
            top.primary_y
        ),
        ImGuiCond_Always,
        ImVec2(1.0f, 0.0f)
    );
    push_glass_window_style(
        (btn + 10.0f * s) * 0.5f,
        ImVec2(5.0f * s, 5.0f * s)
    );
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(2.0f * s, 0.0f)
    );
    if (ImGui::Begin("##FloatingDockCameraPill", nullptr, kOverlayWindowFlags)) {
        add_glass_shadow(
            ctx,
            ImGui::GetWindowPos(),
            ImVec2(
                ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                ImGui::GetWindowPos().y + ImGui::GetWindowSize().y
            ),
            (btn + 10.0f * s) * 0.5f
        );
        if (draw_camera_pill_buttons(
                view,
                actions,
                dock.open_card == gs3d::app::DockCard::kCrosshairStyle,
                s
            )) {
            toggle_card(dock, gs3d::app::DockCard::kCrosshairStyle);
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
    pop_glass_window_style();
}

// ── 右侧 FAB 组（截图 + 新建测量）──────────────────────────────────

void draw_fabs(
    const FrameCtx& ctx,
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    int active_index
) {
    const float s = ctx.s;
    const float main_d = DockMetrics::kFabMainSize * s;
    const float small_d = DockMetrics::kFabSmallSize * s;
    auto& measurement =
        gs3d::app::measurement_for_view(state, active_index);
    const bool measuring = measurement.measure_mode_active();

    ImGui::SetNextWindowPos(
        ImVec2(
            ctx.work_pos.x + ctx.work_size.x - 18.0f * s,
            ctx.dock_top - DockMetrics::kFabDockGap * s
        ),
        ImGuiCond_Always,
        ImVec2(1.0f, 1.0f)
    );
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(0.0f, DockMetrics::kFabGap * s)
    );
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    if (ImGui::Begin(
            "##FloatingDockFabs",
            nullptr,
            kOverlayWindowFlags | ImGuiWindowFlags_NoBackground
        )) {
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // 截图小圆钮（玻璃底）。
        ImGui::SetCursorPosX((main_d - small_d) * 0.5f);
        ImGui::InvisibleButton("##fab_screenshot", ImVec2(small_d, small_d));
        {
            const ImVec2 rmin = ImGui::GetItemRectMin();
            const ImVec2 center(
                rmin.x + small_d * 0.5f,
                rmin.y + small_d * 0.5f
            );
            add_circle_shadow(ctx, center, small_d * 0.5f, glass_shadow_color(30));
            const bool hovered = ImGui::IsItemHovered();
            dl->AddCircleFilled(
                center, small_d * 0.5f,
                ImGui::ColorConvertFloat4ToU32(glass_bg_vec4())
            );
            dl->AddCircle(
                center, small_d * 0.5f,
                ImGui::ColorConvertFloat4ToU32(glass_border_vec4()),
                0, 1.0f
            );
            if (hovered) {
                dl->AddCircleFilled(
                    center, small_d * 0.5f,
                    to_u32(palette::kAccent, 22)
                );
            }
            icon_camera(
                dl, center, small_d * 0.44f,
                to_u32(palette::kTextDim, 235), 2.0f * s
            );
            set_tooltip("截图（保存当前视口）");
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                actions.screenshot_requested = true;
            }
        }

        // 主 FAB：新建测量 / 退出测量。
        ImGui::InvisibleButton("##fab_measure", ImVec2(main_d, main_d));
        {
            const ImVec2 rmin = ImGui::GetItemRectMin();
            const ImVec2 center(
                rmin.x + main_d * 0.5f,
                rmin.y + main_d * 0.5f
            );
            const bool hovered = ImGui::IsItemHovered();
            const ImVec4 base = measuring ? palette::kYellow : palette::kAccent;
            add_circle_shadow(
                ctx, center, main_d * 0.5f,
                to_u32(base, 90)
            );
            dl->AddCircleFilled(
                center, main_d * 0.5f,
                to_u32(hovered
                    ? (measuring ? palette::kYellow : palette::kAccentActive)
                    : base, 255)
            );
            // 黄色底亮，图标用深色；蓝色底用白。
            const ImU32 fg = measuring
                ? IM_COL32(32, 26, 8, 255)
                : to_u32(palette::kOnAccent, 255);
            if (measuring) {
                icon_close(dl, center, main_d * 0.44f, fg, 2.6f * s);
            } else {
                icon_ruler(dl, center, main_d * 0.48f, fg, 2.4f * s);
            }
            set_tooltip(measuring
                ? "退出测量模式（快捷键 M）"
                : "新建测量：进入测量模式，中键取点（快捷键 M）");
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                measurement.toggle_measure_mode();
                if (!measurement.measure_mode_active()) {
                    measurement.clear_pending();
                }
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

// ── 底部胶囊 Dock ───────────────────────────────────────────────────

struct DockItemDesc {
    const char* id;
    const char* label;
    IconFn icon;          // nullptr = 「我的」头像
    gs3d::app::DockCard card;
};

float dock_item_height(const FrameCtx& ctx) {
    const float s = ctx.s;
    float label_h = ImGui::GetFontSize() * 0.85f;
    if (small_font() != nullptr) {
        // LegacySize = AddFontXXX 传入的加载尺寸（已含 ui_scale）。
        label_h = small_font()->LegacySize;
    }
    return DockMetrics::kItemPadY * 2.0f * s +
           DockMetrics::kIconSize * s +
           DockMetrics::kIconLabelGap * s +
           label_h;
}

void draw_dock_item(
    const FrameCtx& ctx,
    const DockItemDesc& item,
    bool selected,
    int badge,
    gs3d::app::DockUiState& dock
) {
    const float s = ctx.s;
    const float item_h = dock_item_height(ctx);
    float label_w = 0.0f;
    {
        ScopedFont font(small_font());
        label_w = ImGui::CalcTextSize(item.label).x;
    }
    const float item_w = std::max(
        DockMetrics::kItemMinWidth * s,
        label_w + 26.0f * s
    );

    ImGui::InvisibleButton(item.id, ImVec2(item_w, item_h));
    const ImVec2 rmin = ImGui::GetItemRectMin();
    const ImVec2 rmax = ImGui::GetItemRectMax();
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (selected) {
        dl->AddRectFilled(
            rmin, rmax,
            to_u32(palette::kAccent, 36),
            DockMetrics::kItemRounding * s
        );
    } else if (hovered) {
        dl->AddRectFilled(
            rmin, rmax,
            to_u32(palette::kAccent, 16),
            DockMetrics::kItemRounding * s
        );
    }

    const ImU32 fg = (selected || hovered)
        ? to_u32(palette::kAccent, 255)
        : to_u32(palette::kTextDim, 225);
    const ImVec2 icon_center(
        (rmin.x + rmax.x) * 0.5f,
        rmin.y + DockMetrics::kItemPadY * s +
            DockMetrics::kIconSize * s * 0.5f
    );
    item.icon(dl, icon_center, DockMetrics::kIconSize * s, fg, 2.0f * s);

    // 角标（「视图」项 = 已开启视图数）。
    if (badge > 0) {
        char text[12];
        std::snprintf(text, sizeof(text), "%d", badge);
        ScopedFont font(small_font());
        const ImVec2 ts = ImGui::CalcTextSize(text);
        const float bh = 17.0f * s;
        const float bw = std::max(bh, ts.x + 8.0f * s);
        const ImVec2 bc(
            icon_center.x + DockMetrics::kIconSize * s * 0.5f + 3.0f * s,
            icon_center.y - DockMetrics::kIconSize * s * 0.5f
        );
        const ImVec2 bmin(bc.x - bw * 0.5f, bc.y - bh * 0.5f);
        const ImVec2 bmax(bc.x + bw * 0.5f, bc.y + bh * 0.5f);
        // 白描边使角标从玻璃底上浮起（对应 mock 的 2px 白边）。
        dl->AddRectFilled(
            ImVec2(bmin.x - 2.0f * s, bmin.y - 2.0f * s),
            ImVec2(bmax.x + 2.0f * s, bmax.y + 2.0f * s),
            ImGui::ColorConvertFloat4ToU32(glass_bg_vec4()),
            bh
        );
        dl->AddRectFilled(bmin, bmax, to_u32(palette::kAccent, 255), bh);
        dl->AddText(
            ImVec2(bc.x - ts.x * 0.5f, bc.y - ts.y * 0.5f),
            to_u32(palette::kOnAccent, 255),
            text
        );
    }

    {
        ScopedFont font(small_font());
        const ImVec2 ts = ImGui::CalcTextSize(item.label);
        dl->AddText(
            ImVec2(
                (rmin.x + rmax.x) * 0.5f - ts.x * 0.5f,
                icon_center.y + DockMetrics::kIconSize * s * 0.5f +
                    DockMetrics::kIconLabelGap * s
            ),
            fg,
            item.label
        );
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        toggle_card(dock, item.card);
    }
}

void draw_dock(
    const FrameCtx& ctx,
    gs3d::app::AppState& state
) {
    const float s = ctx.s;
    const float item_h = dock_item_height(ctx);
    const float dock_h = item_h + DockMetrics::kDockPadY * 2.0f * s + 2.0f;

    int visible_views = 0;
    for (const auto& view : state.render_views) {
        if (view.visible) {
            ++visible_views;
        }
    }

    ImGui::SetNextWindowPos(
        ImVec2(
            ctx.work_pos.x + ctx.work_size.x * 0.5f,
            ctx.work_pos.y + ctx.work_size.y -
                DockMetrics::kDockBottomMargin * s
        ),
        ImGuiCond_Always,
        ImVec2(0.5f, 1.0f)
    );
    push_glass_window_style(
        dock_h * 0.5f,
        ImVec2(DockMetrics::kDockPadX * s, DockMetrics::kDockPadY * s)
    );
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(2.0f * s, 0.0f)
    );
    if (ImGui::Begin("##FloatingDock", nullptr, kOverlayWindowFlags)) {
        add_glass_shadow(
            ctx,
            ImGui::GetWindowPos(),
            ImVec2(
                ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                ImGui::GetWindowPos().y + ImGui::GetWindowSize().y
            ),
            dock_h * 0.5f
        );
        static const DockItemDesc items[] = {
            {"##dock_views",   "视图", icon_monitor, gs3d::app::DockCard::kViews},
            {"##dock_measure", "测量", icon_ruler,   gs3d::app::DockCard::kMeasure},
            {"##dock_layers",  "图层", icon_layers,  gs3d::app::DockCard::kLayers},
            {"##dock_props",   "属性", icon_sliders, gs3d::app::DockCard::kAppearance},
            {"##dock_settings", "设置", icon_settings, gs3d::app::DockCard::kSettings},
        };
        bool first = true;
        for (const auto& item : items) {
            if (!first) {
                ImGui::SameLine();
            }
            first = false;
            const int badge =
                item.card == gs3d::app::DockCard::kViews ? visible_views : 0;
            draw_dock_item(
                ctx,
                item,
                state.dock_ui.open_card == item.card,
                badge,
                state.dock_ui
            );
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
    pop_glass_window_style();
}

// ── 弹出卡片内容 ────────────────────────────────────────────────────

void draw_card_views(
    const FrameCtx& ctx,
    gs3d::app::AppState& state,
    int active_index
) {
    const float s = ctx.s;
    const float gap = 9.0f * s;
    const float avail = ImGui::GetContentRegionAvail().x;
    const float cell_w = (avail - gap) * 0.5f;
    const float cell_h = 64.0f * s;

    const int count = static_cast<int>(state.render_views.size());
    for (int i = 0; i < count; ++i) {
        auto& view = state.render_views[static_cast<std::size_t>(i)];
        if (i % 2 == 1) {
            ImGui::SameLine(0.0f, gap);
        }
        char cell_id[32];
        std::snprintf(cell_id, sizeof(cell_id), "##view_cell_%d", i);
        ImGui::InvisibleButton(cell_id, ImVec2(cell_w, cell_h));
        const ImVec2 rmin = ImGui::GetItemRectMin();
        const ImVec2 rmax = ImGui::GetItemRectMax();
        const bool hovered = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const bool is_active = i == active_index;

        dl->AddRectFilled(
            rmin, rmax,
            to_u32(palette::kViewportBg, view.visible ? 255 : 140),
            12.0f * s
        );
        // 缩略点云示意（种子随视图变化）。
        dl->PushClipRect(rmin, rmax, true);
        for (int p = 0; p < 26; ++p) {
            const int seed = p * 37 + i * 101;
            const float px = static_cast<float>(seed % 100) / 100.0f;
            const float py = static_cast<float>((seed * 53) % 100) / 100.0f;
            const float depth = static_cast<float>((seed * 17) % 100) / 100.0f;
            dl->AddCircleFilled(
                ImVec2(
                    rmin.x + (rmax.x - rmin.x) * (0.12f + px * 0.76f),
                    rmin.y + (rmax.y - rmin.y) * (0.18f + py * 0.6f)
                ),
                (0.8f + depth * 1.2f) * s,
                IM_COL32(
                    72 + static_cast<int>(depth * 100.0f),
                    130 + static_cast<int>(depth * 70.0f),
                    215,
                    view.visible ? 200 : 110
                )
            );
        }
        dl->PopClipRect();

        if (is_active) {
            dl->AddRect(
                rmin, rmax,
                to_u32(palette::kAccent, 255),
                12.0f * s, 0, 2.0f * s
            );
        } else if (hovered) {
            dl->AddRect(
                rmin, rmax,
                to_u32(palette::kAccent, 130),
                12.0f * s, 0, 1.5f * s
            );
        }

        {
            ScopedFont font(small_font());
            char label[32];
            std::snprintf(label, sizeof(label), "视图 %d", i + 1);
            dl->AddText(
                ImVec2(rmin.x + 7.0f * s, rmax.y - 6.0f * s -
                    ImGui::GetFontSize()),
                IM_COL32(255, 255, 255, 235),
                label
            );
            const char* status = view.detached
                ? "独立窗口"
                : is_active
                    ? "沉浸显示中"
                    : (view.visible ? "就绪" : "未开启");
            const ImVec2 ts = ImGui::CalcTextSize(status);
            dl->AddText(
                ImVec2(rmax.x - ts.x - 7.0f * s, rmin.y + 5.0f * s),
                is_active
                    ? to_u32(palette::kAccent, 255)
                    : IM_COL32(255, 255, 255, 150),
                status
            );
        }
        set_tooltip(
            view.detached
                ? "点击收回独立窗口并切换到该视图"
                : is_active
                    ? "当前沉浸视图"
                    : "点击切换到该视图"
        );

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) &&
            (!is_active || view.detached)) {
            view.visible = true;
            view.detached = false;
            view.force_undock_next_frame = false;
            state.active_viewport_index = i;
        }
    }

    ImGui::Spacing();
    if (active_index >= 0 &&
        active_index < static_cast<int>(state.render_views.size())) {
        auto& active =
            state.render_views[static_cast<std::size_t>(active_index)];
        switch_row("联动相机（同步组）", &active.camera_linked, s);
        if (widgets::Button(
                "弹出当前视图为独立窗口",
                widgets::ButtonVariant::kSecondary,
                ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)
            ) &&
            pop_out_view_window(state, active_index)) {
            state.dock_ui.open_card = gs3d::app::DockCard::kNone;
        }
        ImGui::Spacing();
        const bool navigation_floating =
            state.dock_ui.navigation_map_floating;
        if (widgets::Button(
                navigation_floating
                    ? "收回导航图"
                    : "弹出导航图为悬浮窗口",
                widgets::ButtonVariant::kSecondary,
                ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)
            )) {
            state.dock_ui.navigation_map_floating = !navigation_floating;
        }
        ImGui::Spacing();
        if (state.dock_ui.navigation_map_floating) {
            ScopedFont font(small_font());
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                to_u32(palette::kTextDim, 205)
            );
            ImGui::TextWrapped(
                "导航图已在悬浮窗口中实时显示，可拖动标题区域调整位置。"
            );
            ImGui::PopStyleColor();
        } else {
            draw_navigation_map_preview(state, active_index, s);
        }
        ImGui::Spacing();
    }
    {
        ScopedFont font(small_font());
        ImGui::PushStyleColor(ImGuiCol_Text, to_u32(palette::kTextDim, 190));
        ImGui::TextWrapped(
            "沉浸布局一次全屏显示一个视图，点击卡片即时切换；"
            "独立窗口可自由移动或拖到其他显示器；"
            "多窗格并排请切换到工作台布局（我的 → 布局）。"
        );
        ImGui::PopStyleColor();
    }
}

struct MeasureToolDesc {
    const char* label;
    IconFn icon;
    bool enabled;
};

void draw_card_measure(
    const FrameCtx& ctx,
    gs3d::app::AppState& state,
    int active_index
) {
    const float s = ctx.s;
    auto& measurement =
        gs3d::app::measurement_for_view(state, active_index);

    const MeasureToolDesc tools[] = {
        {"测距",     icon_ruler,     true},
        {"区域统计", icon_stats_box, true},
        {"面积",     icon_area,      false},
        {"高差",     icon_height,    false},
        {"剖面",     icon_profile,   false},
        {"角度",     icon_angle,     false},
        {"标注",     icon_pin,       false},
        {"通视",     icon_sight,     false},
    };
    const float gap = 8.0f * s;
    const float avail = ImGui::GetContentRegionAvail().x;
    const float cell_w = (avail - gap * 3.0f) / 4.0f;
    const float cell_h = 52.0f * s;

    for (int i = 0; i < 8; ++i) {
        const auto& tool = tools[i];
        if (i % 4 != 0) {
            ImGui::SameLine(0.0f, gap);
        }
        char cell_id[32];
        std::snprintf(cell_id, sizeof(cell_id), "##mtool_%d", i);
        ImGui::InvisibleButton(cell_id, ImVec2(cell_w, cell_h));
        const ImVec2 rmin = ImGui::GetItemRectMin();
        const ImVec2 rmax = ImGui::GetItemRectMax();
        const bool hovered = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        bool tool_active = false;
        if (i == 0) {
            tool_active = measurement.measure_mode_active();
        } else if (i == 1) {
            tool_active = true;
        }

        const int bg_alpha =
            tool_active ? 40 : (hovered && tool.enabled ? 30 : 14);
        dl->AddRectFilled(
            rmin, rmax,
            to_u32(palette::kAccent, bg_alpha),
            13.0f * s
        );
        const ImU32 icon_col = tool.enabled
            ? to_u32(palette::kAccent, tool_active ? 255 : 220)
            : to_u32(palette::kTextFaint, 130);
        const ImU32 text_col = tool.enabled
            ? to_u32(palette::kText, 225)
            : to_u32(palette::kTextFaint, 130);
        tool.icon(
            dl,
            ImVec2((rmin.x + rmax.x) * 0.5f, rmin.y + 17.0f * s),
            18.0f * s, icon_col, 1.8f * s
        );
        {
            ScopedFont font(small_font());
            const ImVec2 ts = ImGui::CalcTextSize(tool.label);
            dl->AddText(
                ImVec2(
                    (rmin.x + rmax.x) * 0.5f - ts.x * 0.5f,
                    rmax.y - 6.0f * s - ImGui::GetFontSize()
                ),
                text_col, tool.label
            );
        }

        if (!tool.enabled) {
            set_tooltip("规划中");
            continue;
        }
        if (i == 0) {
            set_tooltip("测量模式：中键取点，两点成线（快捷键 M）");
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                measurement.toggle_measure_mode();
                if (!measurement.measure_mode_active()) {
                    measurement.clear_pending();
                }
            }
        } else if (i == 1) {
            set_tooltip("区域统计：测量模式下 Shift+拖拽框选，"
                        "结果收纳在本卡片底部");
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                if (!measurement.measure_mode_active()) {
                    measurement.toggle_measure_mode();
                }
            }
        }
    }

    ImGui::Spacing();
    // 距离显示模式（3D / 平面 / 两者）。
    {
        static const char* mode_items[] = {"3D", "平面", "两者"};
        int mode = static_cast<int>(measurement.display_mode());
        begin_control_row("距离显示", 158.0f * s);
        if (widgets::Segmented("##DistanceMode", mode_items, 3, &mode)) {
            measurement.set_display_mode(
                static_cast<gs3d::app::DistanceDisplayMode>(mode)
            );
        }
    }

    // 测量列表。
    ImGui::Spacing();
    card_section_label("测量记录");
    const auto& lines = measurement.lines();
    if (lines.empty()) {
        ScopedFont font(small_font());
        ImGui::PushStyleColor(ImGuiCol_Text, to_u32(palette::kTextFaint, 200));
        ImGui::TextUnformatted("暂无测量，进入测量模式后中键取点。");
        ImGui::PopStyleColor();
    } else {
        int remove_index = -1;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const auto& line = lines[i];
            ImGui::PushID(static_cast<int>(i));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            const float row_h = ImGui::GetFontSize();
            dl->AddRectFilled(
                ImVec2(cursor.x, cursor.y + row_h * 0.5f - 5.0f * s),
                ImVec2(cursor.x + 11.0f * s, cursor.y + row_h * 0.5f + 5.0f * s),
                srgb_u32_to_linear(line.color) | 0xFF000000u,
                3.0f * s
            );
            ImGui::Dummy(ImVec2(15.0f * s, row_h));
            ImGui::SameLine();
            ImGui::Text(
                "#%d  %s",
                static_cast<int>(i + 1),
                line.distance_label(measurement.display_mode()).c_str()
            );
            ImGui::SameLine();
            float shift = ImGui::GetContentRegionAvail().x - 88.0f * s;
            if (shift > 0.0f) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + shift);
            }
            if (widgets::Chip(line.fixed ? "已固定" : "固定", line.fixed)) {
                measurement.toggle_fixed(i);
            }
            ImGui::SameLine();
            if (widgets::Chip("删除")) {
                remove_index = static_cast<int>(i);
            }
            ImGui::PopID();
        }
        if (remove_index >= 0) {
            measurement.remove_line(static_cast<std::size_t>(remove_index));
        }
        ImGui::Spacing();
        if (widgets::Button(
                "清除未固定",
                widgets::ButtonVariant::kDanger,
                ImVec2(0.0f, 0.0f),
                true
            )) {
            measurement.remove_all_unfixed();
        }
    }

    ImGui::Spacing();
    card_section_label("区域统计结果");
    const auto& stats =
        gs3d::app::region_stats_for_view(state, active_index);
    if (stats.computing) {
        ImGui::TextUnformatted("正在统计框选区域…");
    } else if (!stats.valid) {
        ScopedFont font(small_font());
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            to_u32(palette::kTextFaint, 210)
        );
        ImGui::TextWrapped(
            "测量模式下按住 Shift + 左键框选区域，结果将在这里更新。"
        );
        ImGui::PopStyleColor();
    } else {
        char value[192];
        key_value_row(
            "框内点数",
            format_thousands(stats.point_count),
            82.0f * s
        );
        std::snprintf(
            value,
            sizeof(value),
            "%.6f – %.6f",
            stats.world_x_min,
            stats.world_x_max
        );
        key_value_row("X 范围", value, 82.0f * s);
        std::snprintf(
            value,
            sizeof(value),
            "%.6f – %.6f",
            stats.world_y_min,
            stats.world_y_max
        );
        key_value_row("Y 范围", value, 82.0f * s);
        std::snprintf(
            value,
            sizeof(value),
            "min %.4g · max %.4g · avg %.4g",
            static_cast<double>(stats.fold_min),
            static_cast<double>(stats.fold_max),
            static_cast<double>(stats.fold_avg)
        );
        key_value_row(
            stats.primary_label.empty()
                ? "主属性"
                : stats.primary_label.c_str(),
            value,
            82.0f * s
        );
        std::snprintf(
            value,
            sizeof(value),
            "min %.4g · max %.4g · avg %.4g",
            static_cast<double>(stats.elev_min),
            static_cast<double>(stats.elev_max),
            static_cast<double>(stats.elev_avg)
        );
        key_value_row(
            stats.secondary_label.empty()
                ? "高程"
                : stats.secondary_label.c_str(),
            value,
            82.0f * s
        );
        if (widgets::Button(
                "复制统计结果",
                widgets::ButtonVariant::kSecondary
            )) {
            char clipboard[768];
            std::snprintf(
                clipboard,
                sizeof(clipboard),
                "框内点数 %llu\nX [%.6f, %.6f]\nY [%.6f, %.6f]\n"
                "%s: min %.6g / max %.6g / avg %.6g\n"
                "%s: min %.6g / max %.6g / avg %.6g",
                static_cast<unsigned long long>(stats.point_count),
                stats.world_x_min,
                stats.world_x_max,
                stats.world_y_min,
                stats.world_y_max,
                stats.primary_label.c_str(),
                static_cast<double>(stats.fold_min),
                static_cast<double>(stats.fold_max),
                static_cast<double>(stats.fold_avg),
                stats.secondary_label.c_str(),
                static_cast<double>(stats.elev_min),
                static_cast<double>(stats.elev_max),
                static_cast<double>(stats.elev_avg)
            );
            ImGui::SetClipboardText(clipboard);
        }
    }
}

void draw_card_layers(
    const FrameCtx& ctx,
    gs3d::app::AppState& state,
    int active_index
) {
    const float s = ctx.s;
    const auto& settings =
        gs3d::app::render_settings_for_view(state, active_index);
    const auto& measurement =
        gs3d::app::measurement_for_view(state, active_index);

    card_section_label("数据集");
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const float row_h = ImGui::GetFontSize();
        dl->AddRectFilled(
            ImVec2(cursor.x, cursor.y + row_h * 0.5f - 5.5f * s),
            ImVec2(cursor.x + 11.0f * s, cursor.y + row_h * 0.5f + 5.5f * s),
            to_u32(palette::kAccent, 255),
            4.0f * s
        );
        ImGui::Dummy(ImVec2(15.0f * s, row_h));
        ImGui::SameLine();
        ImGui::TextUnformatted(
            state.dataset.active_dataset.empty()
                ? "未加载数据"
                : state.dataset.active_dataset.c_str()
        );
        ImGui::SameLine();
        ScopedFont font(small_font());
        ImGui::PushStyleColor(ImGuiCol_Text, to_u32(palette::kTextDim, 200));
        char count_text[64];
        std::snprintf(count_text, sizeof(count_text), "%s 点",
            format_thousands(state.dataset.point_count).c_str());
        const float shift = ImGui::GetContentRegionAvail().x -
            ImGui::CalcTextSize(count_text).x;
        if (shift > 0.0f) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + shift);
        }
        ImGui::TextUnformatted(count_text);
        ImGui::PopStyleColor();
    }
    {
        ScopedFont font(small_font());
        ImGui::PushStyleColor(ImGuiCol_Text, to_u32(palette::kTextFaint, 200));
        ImGui::Text(
            "%s · %s",
            state.dataset.format.empty() ? "GS3D" : state.dataset.format.c_str(),
            state.dataset.file_size.c_str()
        );
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    card_section_label("属性通道");
    const ImVec4 attr_colors[] = {
        palette::kTeal, palette::kVarBlue, palette::kPurple, palette::kOrange
    };
    for (std::size_t i = 0; i < state.dataset.attributes.size(); ++i) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const float row_h = ImGui::GetFontSize();
        dl->AddRectFilled(
            ImVec2(cursor.x, cursor.y + row_h * 0.5f - 5.0f * s),
            ImVec2(cursor.x + 11.0f * s, cursor.y + row_h * 0.5f + 5.0f * s),
            to_u32(attr_colors[i % 4], 255),
            3.0f * s
        );
        ImGui::Dummy(ImVec2(15.0f * s, row_h));
        ImGui::SameLine();
        ImGui::TextUnformatted(state.dataset.attributes[i].c_str());
        const bool is_color =
            static_cast<int>(i) == settings.color_attr_index;
        const bool is_height =
            static_cast<int>(i) == settings.height_attr_index;
        if (is_color || is_height) {
            ImGui::SameLine();
            ScopedFont font(small_font());
            ImGui::PushStyleColor(
                ImGuiCol_Text, to_u32(palette::kAccent, 230));
            ImGui::Text(
                "%s%s%s",
                is_color ? "颜色" : "",
                (is_color && is_height) ? " · " : "",
                is_height ? "高度" : ""
            );
            ImGui::PopStyleColor();
        }
    }

    ImGui::Spacing();
    card_section_label("瓦片与细节层级");
    {
        char tiles[128];
        std::snprintf(
            tiles,
            sizeof(tiles),
            "已加载 %u · 等待 %u",
            state.performance.loaded_tiles,
            state.performance.pending_tiles
        );
        key_value_row("瓦片", tiles, 72.0f * s);
    }
    if (state.dataset.lod_details.empty()) {
        key_value_row(
            "LOD",
            state.performance.lod_mode,
            72.0f * s
        );
    } else {
        for (std::size_t i = 0; i < state.dataset.lod_details.size(); ++i) {
            const std::string label =
                i == 0 ? "LOD" : ("L" + std::to_string(i - 1));
            key_value_row(
                label.c_str(),
                state.dataset.lod_details[i],
                72.0f * s
            );
        }
    }
    {
        const auto& runtime_settings =
            gs3d::app::render_settings_for_view(state, active_index);
        char target[64];
        std::snprintf(
            target,
            sizeof(target),
            "%.0f FPS",
            static_cast<double>(runtime_settings.target_fps)
        );
        key_value_row("目标帧率", target, 72.0f * s);
    }

    ImGui::Spacing();
    card_section_label("测量标注");
    const auto& lines = measurement.lines();
    if (lines.empty()) {
        ScopedFont font(small_font());
        ImGui::PushStyleColor(ImGuiCol_Text, to_u32(palette::kTextFaint, 200));
        ImGui::TextUnformatted("暂无测量标注。");
        ImGui::PopStyleColor();
    } else {
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const auto& line = lines[i];
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            const float row_h = ImGui::GetFontSize();
            dl->AddRectFilled(
                ImVec2(cursor.x, cursor.y + row_h * 0.5f - 5.0f * s),
                ImVec2(cursor.x + 11.0f * s,
                       cursor.y + row_h * 0.5f + 5.0f * s),
                srgb_u32_to_linear(line.color) | 0xFF000000u,
                3.0f * s
            );
            ImGui::Dummy(ImVec2(15.0f * s, row_h));
            ImGui::SameLine();
            ImGui::Text(
                "测距 #%d · %s%s",
                static_cast<int>(i + 1),
                line.distance_label(measurement.display_mode()).c_str(),
                line.fixed ? "（固定）" : ""
            );
        }
    }
}

void draw_card_appearance(
    const FrameCtx& ctx,
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    int active_index
) {
    const float s = ctx.s;
    auto& settings =
        gs3d::app::render_settings_for_view(state, active_index);
    const float ctl_w = 168.0f * s;

    {
        begin_control_row("点大小", ctl_w);
        float point_size = settings.point_size;
        if (widgets::SliderFloat(
                "##DockPointSize", &point_size, 1.0f, 10.0f, "%.1f")) {
            settings.point_size = point_size;
            auto& command =
                add_render_settings_command(actions, active_index);
            command.point_size_changed = true;
            command.point_size = point_size;
        }
    }
    {
        static const char* shape_names[] = {
            "方形", "圆形", "菱形", "三角形"
        };
        int shape = std::clamp(settings.point_shape, 0, 3);
        begin_control_row("点形状", ctl_w);
        if (widgets::BeginCombo(
                "##DockPointShape",
                shape_names[shape]
            )) {
            for (int i = 0; i < 4; ++i) {
                if (ImGui::Selectable(shape_names[i], i == shape)) {
                    settings.point_shape = i;
                    auto& command =
                        add_render_settings_command(actions, active_index);
                    command.point_shape_changed = true;
                    command.point_shape = i;
                }
            }
            widgets::EndCombo();
        }
    }
    {
        begin_control_row("高度缩放", ctl_w);
        float exag = settings.height_exaggeration;
        if (widgets::InputFloat(
                "##DockHeightExag",
                &exag,
                "%.2f"
            )) {
            if (!std::isfinite(exag)) {
                exag = settings.height_exaggeration;
            }
            exag = std::max(exag, 0.01f);
            settings.height_exaggeration = exag;
            auto& command =
                add_render_settings_command(actions, active_index);
            command.height_exag_changed = true;
            command.height_exag = exag;
        }
    }
    {
        begin_control_row("着色字段", ctl_w);
        const auto& options = settings.color_by_options;
        const int current = settings.color_attr_index;
        const char* preview =
            (current >= 0 &&
             current < static_cast<int>(options.size()))
                ? options[static_cast<std::size_t>(current)].c_str()
                : "--";
        if (widgets::BeginCombo("##DockColorBy", preview)) {
            for (std::size_t i = 0; i < options.size(); ++i) {
                if (ImGui::Selectable(
                        options[i].c_str(),
                        static_cast<int>(i) == current
                    )) {
                    settings.color_attr_index = static_cast<int>(i);
                    auto& command =
                        add_render_settings_command(actions, active_index);
                    command.color_by_changed = true;
                    command.color_by_index = static_cast<int>(i);
                }
            }
            widgets::EndCombo();
        }
    }
    {
        begin_control_row("高度字段", ctl_w);
        const auto& options = settings.height_by_options;
        const int current = settings.height_attr_index;
        const char* preview =
            (current >= 0 &&
             current < static_cast<int>(options.size()))
                ? options[static_cast<std::size_t>(current)].c_str()
                : "--";
        if (widgets::BeginCombo("##DockHeightBy", preview)) {
            for (std::size_t i = 0; i < options.size(); ++i) {
                if (ImGui::Selectable(
                        options[i].c_str(),
                        static_cast<int>(i) == current
                    )) {
                    settings.height_attr_index = static_cast<int>(i);
                    auto& command =
                        add_render_settings_command(actions, active_index);
                    command.height_by_changed = true;
                    command.height_by_index = static_cast<int>(i);
                }
            }
            widgets::EndCombo();
        }
    }
    {
        begin_control_row("色标", ctl_w);
        int cmap = settings.colormap_index;
        if (cmap < 0 || cmap >= kColormapCount) {
            cmap = 0;
        }
        if (widgets::BeginCombo(
                "##DockColormap", colormap_display_name(cmap))) {
            for (int i = 0; i < kColormapCount; ++i) {
                if (ImGui::Selectable(
                        colormap_display_name(i), i == cmap)) {
                    settings.colormap_index = i;
                    auto& command =
                        add_render_settings_command(actions, active_index);
                    command.colormap_changed = true;
                    command.colormap_index = i;
                }
            }
            widgets::EndCombo();
        }
    }

    // 色标预览条 + 数据范围。
    {
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float bar_w = ImGui::GetContentRegionAvail().x;
        const float bar_h = 10.0f * s;
        ImGui::InvisibleButton("##DockCmapPreview", ImVec2(bar_w, bar_h));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        draw_colormap_preview_bar(
            dl,
            start,
            ImVec2(start.x + bar_w, start.y + bar_h),
            settings.colormap_index
        );
        ScopedFont font(status_font());
        ImGui::PushStyleColor(ImGuiCol_Text, to_u32(palette::kTextDim, 200));
        char lo[32];
        char hi[32];
        std::snprintf(lo, sizeof(lo), "%.4g",
            static_cast<double>(settings.data_value_min));
        std::snprintf(hi, sizeof(hi), "%.4g",
            static_cast<double>(settings.data_value_max));
        ImGui::TextUnformatted(lo);
        ImGui::SameLine();
        const float shift = ImGui::GetContentRegionAvail().x -
            ImGui::CalcTextSize(hi).x;
        if (shift > 0.0f) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + shift);
        }
        ImGui::TextUnformatted(hi);
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    card_section_label("值域裁切");
    const float data_lo = settings.data_value_min;
    const float data_hi = settings.data_value_max;
    const float data_range = data_hi - data_lo;
    const float step =
        data_range > 0.0f ? data_range * 0.001f : 0.001f;
    bool clip_enabled = settings.value_clip_enabled;
    if (switch_row("启用值域裁切", &clip_enabled, s)) {
        settings.value_clip_enabled = clip_enabled;
        if (clip_enabled) {
            settings.value_clip_min = data_lo;
            settings.value_clip_max = data_hi;
        }
        auto& command =
            add_render_settings_command(actions, active_index);
        command.value_clip_changed = true;
        command.value_clip_enabled = clip_enabled;
        command.value_clip_min = settings.value_clip_min;
        command.value_clip_max = settings.value_clip_max;
    }
    if (settings.value_clip_enabled) {
        float lo = std::clamp(
            settings.value_clip_min,
            data_lo,
            settings.value_clip_max
        );
        float hi = std::clamp(
            settings.value_clip_max,
            lo,
            data_hi
        );
        begin_control_row("下限", ctl_w);
        if (widgets::DragFloat(
                "##DockClipMin",
                &lo,
                step,
                data_lo,
                hi,
                "%.4g"
            )) {
            settings.value_clip_min = lo;
            auto& command =
                add_render_settings_command(actions, active_index);
            command.value_clip_changed = true;
            command.value_clip_enabled = true;
            command.value_clip_min = lo;
            command.value_clip_max = hi;
        }
        begin_control_row("上限", ctl_w);
        if (widgets::DragFloat(
                "##DockClipMax",
                &hi,
                step,
                lo,
                data_hi,
                "%.4g"
            )) {
            settings.value_clip_max = hi;
            auto& command =
                add_render_settings_command(actions, active_index);
            command.value_clip_changed = true;
            command.value_clip_enabled = true;
            command.value_clip_min = lo;
            command.value_clip_max = hi;
        }
    }
}

void draw_card_data(
    const FrameCtx& ctx,
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions
) {
    const float s = ctx.s;
    key_value_row(
        "名称",
        state.dataset.active_dataset.empty()
            ? "未加载数据"
            : state.dataset.active_dataset,
        76.0f * s
    );
    key_value_row(
        "格式",
        state.dataset.format.empty() ? "GS3D" : state.dataset.format,
        76.0f * s
    );
    key_value_row(
        "点数",
        format_thousands(state.dataset.point_count),
        76.0f * s
    );
    key_value_row(
        "大小",
        state.dataset.file_size.empty() ? "--" : state.dataset.file_size,
        76.0f * s
    );
    key_value_row(
        "路径",
        state.dataset.path.empty() ? "--" : state.dataset.path,
        76.0f * s
    );
    key_value_row(
        "包围盒",
        state.dataset.bounding_box.empty()
            ? "--"
            : state.dataset.bounding_box,
        76.0f * s
    );
    std::string attributes;
    for (const auto& attribute : state.dataset.attributes) {
        if (!attributes.empty()) {
            attributes += " · ";
        }
        attributes += attribute;
    }
    key_value_row(
        "属性",
        attributes.empty() ? "--" : attributes,
        76.0f * s
    );

    ImGui::Spacing();
    card_section_label("操作");
    if (widgets::Button(
            "打开数据文件…    Ctrl+O",
            widgets::ButtonVariant::kSecondary,
            ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)
        )) {
        actions.open_requested = true;
        state.dock_ui.open_card = gs3d::app::DockCard::kNone;
    }
    if (widgets::Button(
            "打开 GS3D Bundle 项目…",
            widgets::ButtonVariant::kSecondary,
            ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)
        )) {
        actions.open_bundle_requested = true;
        state.dock_ui.open_card = gs3d::app::DockCard::kNone;
    }
    if (widgets::Button(
            "返回欢迎页",
            widgets::ButtonVariant::kSecondary,
            ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)
        )) {
        actions.show_welcome_requested = true;
        state.dock_ui.open_card = gs3d::app::DockCard::kNone;
    }
}

void draw_card_performance(
    const FrameCtx& ctx,
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    int active_index
) {
    const float s = ctx.s;
    char value[160];
    card_section_label("渲染");
    std::snprintf(
        value,
        sizeof(value),
        "%.1f FPS",
        static_cast<double>(state.performance.fps)
    );
    key_value_row("帧率", value, 92.0f * s);
    std::snprintf(
        value,
        sizeof(value),
        "%.2f ms",
        static_cast<double>(state.performance.frame_time_ms)
    );
    key_value_row("帧耗时", value, 92.0f * s);
    key_value_row(
        "可见点数",
        format_thousands(state.performance.visible_points),
        92.0f * s
    );
    std::snprintf(
        value,
        sizeof(value),
        "%.1f MB",
        static_cast<double>(
            bytes_to_mb(state.performance.gpu_memory_bytes)
        )
    );
    key_value_row("GPU 显存", value, 92.0f * s);

    ImGui::Spacing();
    card_section_label("流式加载");
    std::snprintf(
        value,
        sizeof(value),
        "已加载 %u · 等待 %u",
        state.performance.loaded_tiles,
        state.performance.pending_tiles
    );
    key_value_row("瓦片", value, 92.0f * s);
    const auto& settings =
        gs3d::app::render_settings_for_view(state, active_index);
    key_value_row(
        "GPU 瓦片",
        settings.cache_usage,
        92.0f * s
    );
    key_value_row(
        "CPU 缓存",
        settings.cpu_cache_usage,
        92.0f * s
    );
    if (settings.cache_hit_rate < 0.0f) {
        key_value_row("缓存命中率", "—", 92.0f * s);
    } else {
        std::snprintf(
            value,
            sizeof(value),
            "%.1f%%",
            static_cast<double>(settings.cache_hit_rate)
        );
        key_value_row("缓存命中率", value, 92.0f * s);
    }
    key_value_row("细节层级", state.performance.lod_mode, 92.0f * s);
    ImGui::Spacing();
    if (widgets::Button(
            "清空缓存",
            widgets::ButtonVariant::kDanger,
            ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)
        )) {
        actions.clear_cache_requested = true;
    }
}

bool color_swatch(
    const char* id,
    std::uint32_t color,
    bool selected,
    float s
) {
    const float size = 19.0f * s;
    ImGui::InvisibleButton(id, ImVec2(size, size));
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 center(min.x + size * 0.5f, min.y + size * 0.5f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (selected) {
        dl->AddCircleFilled(
            center,
            size * 0.5f,
            to_u32(palette::kAccent, 255)
        );
    }
    dl->AddCircleFilled(
        center,
        size * (selected ? 0.34f : 0.42f),
        color
    );
    dl->AddCircle(
        center,
        size * (selected ? 0.34f : 0.42f),
        IM_COL32(255, 255, 255, 210),
        0,
        1.0f
    );
    return ImGui::IsItemClicked(ImGuiMouseButton_Left);
}

void crosshair_color_row(
    const char* label,
    const char* id,
    std::uint32_t& value,
    float s
) {
    static constexpr std::uint32_t colors[] = {
        IM_COL32(0xF1, 0xC2, 0x1B, 0xFF),
        IM_COL32(0x2A, 0xAB, 0xEE, 0xFF),
        IM_COL32(0xFF, 0x5E, 0x7E, 0xFF),
        IM_COL32(0x4A, 0xDE, 0x80, 0xFF),
        IM_COL32(0xFF, 0xFF, 0xFF, 0xFF),
    };
    ImGui::PushID(id);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    const float row_width =
        static_cast<float>(std::size(colors)) * 23.0f * s;
    const float shift = ImGui::GetContentRegionAvail().x - row_width;
    if (shift > 0.0f) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + shift);
    }
    for (std::size_t i = 0; i < std::size(colors); ++i) {
        if (i > 0) {
            ImGui::SameLine(0.0f, 4.0f * s);
        }
        ImGui::PushID(static_cast<int>(i));
        if (color_swatch(
                "##swatch",
                colors[i],
                value == colors[i],
                s
            )) {
            value = colors[i];
        }
        ImGui::PopID();
    }
    if (widgets::Chip("恢复默认") &&
        value != colors[0]) {
        value = colors[0];
    }
    ImGui::PopID();
}

void draw_card_crosshair_style(
    const FrameCtx& ctx,
    gs3d::app::RenderViewState& view
) {
    const float s = ctx.s;
    crosshair_color_row(
        "十字准线",
        "crosshair",
        view.crosshair_color,
        s
    );
    crosshair_color_row(
        "拾取准星",
        "reticle",
        view.reticle_color,
        s
    );
    ImGui::Spacing();
    {
        ScopedFont font(small_font());
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            to_u32(palette::kTextFaint, 210)
        );
        ImGui::TextWrapped(
            "左键旋转 · 右键平移 · 滚轮缩放 · F 聚焦"
        );
        ImGui::PopStyleColor();
    }
}

void draw_card_settings(
    const FrameCtx& ctx,
    gs3d::app::AppState& state,
    gs3d::app::RenderViewState& view,
    gs3d::app::UiActions& actions,
    FloatingDockFrameResult& result
) {
    const float s = ctx.s;

    card_section_label("主题");
    for (int i = 0; i < kThemeCount; ++i) {
        const auto id = static_cast<ThemeId>(i);
        const bool selected = active_theme() == id;
        if (ImGui::Selectable(theme_tokens(id).name, selected) &&
            !selected) {
            // 卡片仍处在玻璃样式压栈范围内，立即 apply_theme 会让
            // PopStyleColor 恢复旧主题颜色，与 UiRoot 菜单同理，延迟
            // 到本帧样式全部出栈后由调用方应用。
            result.theme_change_requested = true;
            result.requested_theme = id;
        }
    }

    ImGui::Spacing();
    card_section_label("显示");
    switch_row("性能 HUD", &state.dock_ui.show_perf_hud, s);
    switch_row("视口联动相机", &view.camera_linked, s);
    bool map_axis = view.show_map_axis;
    if (switch_row("地图坐标轴", &map_axis, s)) {
        view.show_map_axis = map_axis;
        if (view.show_map_axis) {
            view.show_world_axis = false;
        }
    }
    bool world_axis = view.show_world_axis;
    if (switch_row("世界轴", &world_axis, s)) {
        view.show_world_axis = world_axis;
        if (view.show_world_axis) {
            view.show_map_axis = false;
            view.show_crosshair = false;
        }
    }

    ImGui::Spacing();
    card_section_label("数据");
    if (widgets::Button(
            "打开数据文件…    Ctrl+O",
            widgets::ButtonVariant::kSecondary,
            ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)
        )) {
        actions.open_requested = true;
        state.dock_ui.open_card = gs3d::app::DockCard::kNone;
    }
    if (widgets::Button(
            "打开 GS3D Bundle 项目…",
            widgets::ButtonVariant::kSecondary,
            ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)
        )) {
        actions.open_bundle_requested = true;
        state.dock_ui.open_card = gs3d::app::DockCard::kNone;
    }
    if (widgets::Button(
            "返回欢迎页",
            widgets::ButtonVariant::kSecondary,
            ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)
        )) {
        actions.show_welcome_requested = true;
        state.dock_ui.open_card = gs3d::app::DockCard::kNone;
    }

    ImGui::Spacing();
    card_section_label("布局");
    if (widgets::Button(
            "方案 A · 专业工作台",
            widgets::ButtonVariant::kSecondary,
            ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)
        )) {
        state.ui_layout_mode = gs3d::app::UiLayoutMode::kWorkbench;
        state.dock_ui.open_card = gs3d::app::DockCard::kNone;
        state.dock_ui.anim_card = gs3d::app::DockCard::kNone;
        state.dock_ui.card_anim = 0.0f;
    }
    if (widgets::Button(
            "方案 C · 暗色分析舱",
            widgets::ButtonVariant::kSecondary,
            ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)
        )) {
        state.ui_layout_mode =
            gs3d::app::UiLayoutMode::kAnalysisRail;
        state.dock_ui.open_card = gs3d::app::DockCard::kNone;
        state.dock_ui.anim_card = gs3d::app::DockCard::kNone;
        state.dock_ui.card_anim = 0.0f;
        result.theme_change_requested = true;
        result.requested_theme = ThemeId::kDeepGraphite;
    }
    const float layout_button_gap = 8.0f * s;
    const float layout_button_width =
        (ImGui::GetContentRegionAvail().x - layout_button_gap) * 0.5f;
    if (widgets::Button(
            "恢复默认工作区",
            widgets::ButtonVariant::kSecondary,
            ImVec2(layout_button_width, 0.0f)
        )) {
        restore_default_workspace(state);
        actions.restore_default_workspace_requested = true;
        state.ui_layout_mode = gs3d::app::UiLayoutMode::kWorkbench;
        state.dock_ui.open_card = gs3d::app::DockCard::kNone;
        state.dock_ui.anim_card = gs3d::app::DockCard::kNone;
        state.dock_ui.card_anim = 0.0f;
    }
    ImGui::SameLine(0.0f, layout_button_gap);
    const bool can_create_workspace = has_hidden_view(state);
    ImGui::BeginDisabled(!can_create_workspace);
    if (widgets::Button(
            "新建工作窗口",
            widgets::ButtonVariant::kSecondary,
            ImVec2(layout_button_width, 0.0f)
        ) &&
        create_workspace_window(state)) {
        state.ui_layout_mode = gs3d::app::UiLayoutMode::kWorkbench;
        state.dock_ui.open_card = gs3d::app::DockCard::kNone;
        state.dock_ui.anim_card = gs3d::app::DockCard::kNone;
        state.dock_ui.card_anim = 0.0f;
    }
    ImGui::EndDisabled();

    ImGui::Spacing();
    card_section_label("日志");
    if (state.debug_log.lines.empty()) {
        ImGui::TextUnformatted("暂无日志。");
    } else {
        const std::size_t first =
            state.debug_log.lines.size() > 3
                ? state.debug_log.lines.size() - 3
                : 0;
        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            to_u32(palette::kFrame, 150)
        );
        ImGui::BeginChild(
            "##DockLogPreview",
            ImVec2(0.0f, 78.0f * s),
            ImGuiChildFlags_Borders,
            ImGuiWindowFlags_NoScrollbar
        );
        {
            ScopedFont font(status_font());
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                to_u32(palette::kTextDim, 220)
            );
            for (std::size_t i = first;
                 i < state.debug_log.lines.size();
                 ++i) {
                ImGui::TextWrapped(
                    "%s",
                    state.debug_log.lines[i].c_str()
                );
            }
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    {
        ScopedFont font(small_font());
        ImGui::PushStyleColor(ImGuiCol_Text, to_u32(palette::kTextFaint, 200));
        ImGui::TextWrapped(
            "Esc 收起卡片 · M 测量模式 · Ctrl+O 打开数据 · "
            "Ctrl+N 新建视图 · Tab 切换着色 · "
            "左键旋转 · 右键平移 · 滚轮缩放"
        );
        ImGui::PopStyleColor();
    }
}

const char* card_title(gs3d::app::DockCard card) {
    switch (card) {
        case gs3d::app::DockCard::kViews:      return "多视口";
        case gs3d::app::DockCard::kMeasure:    return "测量工具";
        case gs3d::app::DockCard::kLayers:     return "图层";
        case gs3d::app::DockCard::kAppearance: return "点云外观";
        case gs3d::app::DockCard::kSettings:   return "设置";
        case gs3d::app::DockCard::kData:        return "数据";
        case gs3d::app::DockCard::kPerformance: return "性能";
        case gs3d::app::DockCard::kCrosshairStyle:
            return "准星样式";
        default:                               return "";
    }
}

bool card_allows_vertical_scroll(const gs3d::app::DockCard card)
{
    switch (card) {
        case gs3d::app::DockCard::kMeasure:
        case gs3d::app::DockCard::kLayers:
        case gs3d::app::DockCard::kAppearance:
        case gs3d::app::DockCard::kSettings:
        case gs3d::app::DockCard::kData:
            return true;
        case gs3d::app::DockCard::kViews:
        case gs3d::app::DockCard::kPerformance:
        case gs3d::app::DockCard::kCrosshairStyle:
        case gs3d::app::DockCard::kNone:
            return false;
    }
    return false;
}

void draw_dock_card(
    const FrameCtx& ctx,
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    int active_index,
    FloatingDockFrameResult& result
) {
    auto& dock = state.dock_ui;
    if (dock.anim_card == gs3d::app::DockCard::kNone ||
        dock.card_anim <= 0.0f) {
        return;
    }
    const float s = ctx.s;
    const bool closing = dock.open_card == gs3d::app::DockCard::kNone;
    const float eased = ease_out_back(dock.card_anim);
    const float alpha = std::clamp(dock.card_anim * 1.4f, 0.0f, 1.0f);
    const auto card = dock.anim_card;
    const bool top_left =
        card == gs3d::app::DockCard::kData ||
        card == gs3d::app::DockCard::kPerformance;
    const bool top_right =
        card == gs3d::app::DockCard::kCrosshairStyle;
    float card_width = DockMetrics::kCardWidth;
    if (card == gs3d::app::DockCard::kMeasure ||
        card == gs3d::app::DockCard::kData ||
        card == gs3d::app::DockCard::kPerformance) {
        card_width = 400.0f;
    } else if (top_right) {
        card_width = 320.0f;
    }

    ImVec2 card_pos;
    ImVec2 card_pivot;
    float max_height = 0.0f;
    if (top_left) {
        const float y_offset =
            card == gs3d::app::DockCard::kData ? 76.0f : 104.0f;
        card_pos = ImVec2(
            ctx.work_pos.x + DockMetrics::kSideMargin * s,
            ctx.work_pos.y + y_offset * s -
                (1.0f - eased) * 8.0f * s
        );
        card_pivot = ImVec2(0.0f, 0.0f);
        max_height = ctx.work_size.y - y_offset * s - 20.0f * s;
    } else if (top_right) {
        const float y_offset = 76.0f;
        card_pos = ImVec2(
            ctx.work_pos.x + ctx.work_size.x -
                DockMetrics::kSideMargin * s,
            ctx.work_pos.y + y_offset * s -
                (1.0f - eased) * 8.0f * s
        );
        card_pivot = ImVec2(1.0f, 0.0f);
        max_height = ctx.work_size.y - y_offset * s - 20.0f * s;
    } else {
        card_pos = ImVec2(
            ctx.work_pos.x + ctx.work_size.x * 0.5f,
            ctx.dock_top - DockMetrics::kCardBottomGap * s +
                (1.0f - eased) * 10.0f * s
        );
        card_pivot = ImVec2(0.5f, 1.0f);
        max_height =
            ctx.dock_top - ctx.work_pos.y - 22.0f * s;
    }
    max_height = std::max(160.0f * s, max_height);
    ImGui::SetNextWindowPos(
        card_pos,
        ImGuiCond_Always,
        card_pivot
    );
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(card_width * s, 0.0f),
        ImVec2(card_width * s, max_height)
    );
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    push_glass_window_style(
        DockMetrics::kCardRounding * s,
        ImVec2(DockMetrics::kCardPad * s, DockMetrics::kCardPad * s)
    );
    const bool allow_vertical_scroll = card_allows_vertical_scroll(card);
    push_card_scrollbar_style(s);
    ImGuiWindowFlags flags = kOverlayWindowFlags;
    if (closing) {
        // 收起动画期间不再拦截输入，点击可直接落到视口。
        flags |= ImGuiWindowFlags_NoInputs;
    }
    if (ImGui::Begin("##FloatingDockCard", nullptr, flags)) {
        const ImVec2 wmin = ImGui::GetWindowPos();
        const ImVec2 wmax(
            wmin.x + ImGui::GetWindowSize().x,
            wmin.y + ImGui::GetWindowSize().y
        );
        add_glass_shadow(ctx, wmin, wmax, DockMetrics::kCardRounding * s);

        if (card_header(card_title(dock.anim_card), s)) {
            dock.open_card = gs3d::app::DockCard::kNone;
        }
        const auto draw_card_body = [&]() {
            switch (dock.anim_card) {
                case gs3d::app::DockCard::kViews:
                    draw_card_views(ctx, state, active_index);
                    break;
                case gs3d::app::DockCard::kMeasure:
                    draw_card_measure(ctx, state, active_index);
                    break;
                case gs3d::app::DockCard::kLayers:
                    draw_card_layers(ctx, state, active_index);
                    break;
                case gs3d::app::DockCard::kAppearance:
                    draw_card_appearance(ctx, state, actions, active_index);
                    break;
                case gs3d::app::DockCard::kSettings:
                    draw_card_settings(
                        ctx,
                        state,
                        state.render_views[
                            static_cast<std::size_t>(active_index)
                        ],
                        actions,
                        result
                    );
                    break;
                case gs3d::app::DockCard::kData:
                    draw_card_data(ctx, state, actions);
                    break;
                case gs3d::app::DockCard::kPerformance:
                    draw_card_performance(
                        ctx,
                        state,
                        actions,
                        active_index
                    );
                    break;
                case gs3d::app::DockCard::kCrosshairStyle:
                    draw_card_crosshair_style(
                        ctx,
                        state.render_views[
                            static_cast<std::size_t>(active_index)
                        ]
                    );
                    break;
                default:
                    break;
            }
        };

        if (allow_vertical_scroll) {
            static std::array<float, 8> measured_body_heights{};
            static gs3d::app::DockCard previous_scroll_card =
                gs3d::app::DockCard::kNone;

            const std::size_t card_index =
                static_cast<std::size_t>(card);
            const float max_body_height = std::max(
                96.0f * s,
                max_height -
                    ImGui::GetCursorPosY() -
                    DockMetrics::kCardPad * s
            );
            const float cached_height =
                measured_body_heights[card_index];
            const float body_height = std::clamp(
                cached_height > 0.0f
                    ? cached_height
                    : max_body_height,
                48.0f * s,
                max_body_height
            );

            ImGui::PushStyleVar(
                ImGuiStyleVar_WindowPadding,
                ImVec2(0.0f, 0.0f)
            );
            if (ImGui::BeginChild(
                    "##FloatingDockCardBody",
                    ImVec2(0.0f, body_height),
                    ImGuiChildFlags_None,
                    ImGuiWindowFlags_NoBackground |
                        ImGuiWindowFlags_NoSavedSettings |
                        ImGuiWindowFlags_NoNavFocus
                )) {
                if (previous_scroll_card != card) {
                    ImGui::SetScrollY(0.0f);
                }
                const float content_start =
                    ImGui::GetCursorPosY() + ImGui::GetScrollY();
                draw_card_body();
                const float content_end =
                    ImGui::GetCursorPosY() + ImGui::GetScrollY();
                measured_body_heights[card_index] = std::max(
                    48.0f * s,
                    content_end - content_start + 4.0f * s
                );
                previous_scroll_card = card;
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
        } else {
            draw_card_body();
        }
    }
    ImGui::End();
    pop_card_scrollbar_style();
    pop_glass_window_style();
    ImGui::PopStyleVar();
}

// ── 引导气泡 ────────────────────────────────────────────────────────

void draw_hint_pill(const FrameCtx& ctx, gs3d::app::DockUiState& dock) {
    if (dock.hint_seconds_left <= 0.0f ||
        dock.open_card != gs3d::app::DockCard::kNone) {
        return;
    }
    const float s = ctx.s;
    const float dt = std::min(ImGui::GetIO().DeltaTime, 0.05f);
    dock.hint_seconds_left = std::max(0.0f, dock.hint_seconds_left - dt);
    const float fade = std::clamp(dock.hint_seconds_left / 0.6f, 0.0f, 1.0f);
    if (fade <= 0.0f) {
        return;
    }
    if (ctx.shadow_layer == nullptr) {
        return;
    }
    const char* text = "点击 Dock 图标，面板以悬浮卡片弹出";
    ImFont* font = small_font() != nullptr
        ? small_font()
        : ImGui::GetFont();
    const float font_size = font == nullptr
        ? ImGui::GetFontSize()
        : font->LegacySize;
    const ImVec2 text_size = font->CalcTextSizeA(
        font_size,
        FLT_MAX,
        0.0f,
        text
    );
    const ImVec2 padding(14.0f * s, 7.0f * s);
    const ImVec2 size(
        text_size.x + padding.x * 2.0f,
        text_size.y + padding.y * 2.0f
    );
    const ImVec2 min(
        ctx.work_pos.x + (ctx.work_size.x - size.x) * 0.5f,
        ctx.dock_top - 10.0f * s - size.y
    );
    const ImVec2 max(min.x + size.x, min.y + size.y);
    ctx.shadow_layer->AddRectFilled(
        min,
        max,
        IM_COL32(20, 24, 32, static_cast<int>(190.0f * fade)),
        size.y * 0.5f
    );
    ctx.shadow_layer->AddText(
        font,
        font_size,
        ImVec2(min.x + padding.x, min.y + padding.y),
        IM_COL32(235, 240, 247, static_cast<int>(255.0f * fade)),
        text
    );
}

} // namespace

void draw_detached_view_camera_pill(
    gs3d::app::RenderViewState& view,
    gs3d::app::UiActions& actions,
    const float canvas_top,
    const float canvas_right,
    const unsigned int platform_viewport_id,
    const float ui_scale
) {
    const float s = std::max(0.5f, ui_scale);
    const float btn = 36.0f * s;
    const float axis_clearance =
        view.show_map_axis ? kMapAxisTopBandBase * s : 0.0f;
    ImGui::SetNextWindowPos(
        ImVec2(
            canvas_right - DockMetrics::kSideMargin * s,
            canvas_top + axis_clearance + DockMetrics::kTopMargin * s
        ),
        ImGuiCond_Always,
        ImVec2(1.0f, 0.0f)
    );
    ImGui::SetNextWindowViewport(platform_viewport_id);
    push_glass_window_style(
        (btn + 10.0f * s) * 0.5f,
        ImVec2(5.0f * s, 5.0f * s)
    );
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(2.0f * s, 0.0f)
    );
    const std::string window_name =
        "##DetachedCameraPill" + std::to_string(view.viewport_index);
    if (ImGui::Begin(
            window_name.c_str(),
            nullptr,
            kOverlayWindowFlags
        )) {
        const bool style_active =
            ImGui::IsPopupOpen("##DetachedReticleStyle");
        if (draw_camera_pill_buttons(
                view,
                actions,
                style_active,
                s
            )) {
            ImGui::OpenPopup("##DetachedReticleStyle");
        }
        draw_detached_reticle_popup(view);
    }
    ImGui::End();
    ImGui::PopStyleVar();
    pop_glass_window_style();
}

void draw_screenshot_notice(
    gs3d::app::AppState& state,
    const float ui_scale
) {
    auto& notice = state.screenshot_notice;
    if (notice.kind == gs3d::app::ScreenshotNoticeKind::kNone ||
        notice.message.empty()) {
        return;
    }
    if (notice.seconds_left > 0.0f) {
        notice.seconds_left -=
            std::min(ImGui::GetIO().DeltaTime, 0.05f);
        if (notice.seconds_left <= 0.0f) {
            notice = {};
            return;
        }
    }

    ImVec4 indicator = palette::kAccent;
    switch (notice.kind) {
    case gs3d::app::ScreenshotNoticeKind::kSaved:
        indicator = palette::kGreen;
        break;
    case gs3d::app::ScreenshotNoticeKind::kCancelled:
        indicator = palette::kGray;
        break;
    case gs3d::app::ScreenshotNoticeKind::kError:
        indicator = palette::kRed;
        break;
    default:
        break;
    }

    const float s = std::max(0.5f, ui_scale);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float notice_width = std::clamp(
        ImGui::CalcTextSize(notice.message.c_str()).x + 58.0f * s,
        190.0f * s,
        520.0f * s
    );
    ImGui::SetNextWindowPos(
        ImVec2(
            viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
            viewport->WorkPos.y + 58.0f * s
        ),
        ImGuiCond_Always,
        ImVec2(0.5f, 0.0f)
    );
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(notice_width, 0.0f),
        ImVec2(notice_width, FLT_MAX)
    );
    push_glass_window_style(
        18.0f * s,
        ImVec2(14.0f * s, 9.0f * s)
    );
    if (ImGui::Begin(
            "##ScreenshotNotice",
            nullptr,
            kOverlayWindowFlags | ImGuiWindowFlags_NoInputs
        )) {
        const float dot_size = 10.0f * s;
        ImGui::Dummy(ImVec2(dot_size, dot_size));
        const ImVec2 dot_min = ImGui::GetItemRectMin();
        const ImVec2 dot_max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(
                (dot_min.x + dot_max.x) * 0.5f,
                (dot_min.y + dot_max.y) * 0.5f
            ),
            dot_size * 0.5f,
            to_u32(indicator)
        );
        ImGui::SameLine();
        ImGui::TextWrapped("%s", notice.message.c_str());
    }
    ImGui::End();
    pop_glass_window_style();
}

// ── 布局入口 ────────────────────────────────────────────────────────

FloatingDockFrameResult draw_floating_dock_layout(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    float ui_scale
) {
    FloatingDockFrameResult result;
    auto& dock = state.dock_ui;
    const float dt = std::min(ImGui::GetIO().DeltaTime, 0.05f);

    FrameCtx ctx;
    ctx.s = ui_scale;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ctx.work_pos = viewport->WorkPos;
    ctx.work_size = viewport->WorkSize;

    const int active_index = resolve_immersive_view(state);

    // ── 卡片开合动画推进 ──
    const bool opening = dock.open_card != gs3d::app::DockCard::kNone;
    if (opening && dock.anim_card != dock.open_card) {
        // 两个卡片间直接切换：内容立即替换，沿用当前展开进度。
        dock.anim_card = dock.open_card;
    }
    constexpr float kCardAnimSpeed = 6.5f;  // ≈0.15s 展开
    dock.card_anim = std::clamp(
        dock.card_anim + (opening ? 1.0f : -1.0f) * dt * kCardAnimSpeed,
        0.0f,
        1.0f
    );
    if (!opening && dock.card_anim <= 0.0f) {
        dock.anim_card = gs3d::app::DockCard::kNone;
    }

    // ── Dock 几何（FAB / 卡片定位依赖，先算好）──
    float item_h;
    {
        // dock_item_height 需要字体度量，无窗口上下文也可用。
        item_h = dock_item_height(ctx);
    }
    ctx.dock_height =
        item_h + DockMetrics::kDockPadY * 2.0f * ctx.s + 2.0f;
    ctx.dock_top = ctx.work_pos.y + ctx.work_size.y -
        DockMetrics::kDockBottomMargin * ctx.s - ctx.dock_height;

    // ── 1. 全屏沉浸视口 ──
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
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin(
            "GeoScatter3D 沉浸视口###FloatingDockHost",
            nullptr,
            host_flags
        )) {
        ctx.shadow_layer = ImGui::GetWindowDrawList();
        if (active_index >= 0) {
            ViewportCanvasOptions options;
            options.workspace_id = 0;
            options.show_info_badge = false;
            options.interaction_enabled =
                floating_dock_allows_viewport_input(
                    dock.open_card != gs3d::app::DockCard::kNone,
                    dock.anim_card != gs3d::app::DockCard::kNone ||
                        dock.card_anim > 0.0f,
                    pointer_over_floating_dock_overlay()
                );
            draw_viewport_canvas(
                state.render_views[static_cast<std::size_t>(active_index)],
                actions,
                options
            );
        }
        // 点击视口空白处收起卡片（悬浮窗被悬停时本窗口不算 hovered）。
        if (dock.open_card != gs3d::app::DockCard::kNone &&
            ImGui::IsWindowHovered() &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            dock.open_card = gs3d::app::DockCard::kNone;
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);

    // 未沉浸显示、也未弹出的视图本帧不渲染。弹出视图稍后由
    // UiRoot 作为独立 ImGui 窗口绘制。
    for (auto& view : state.render_views) {
        if (view.viewport_index != active_index &&
            !view.detached &&
            !view.force_undock_next_frame) {
            view.render_requested = false;
        }
    }

    if (active_index < 0) {
        return result;
    }
    auto& active_view =
        state.render_views[static_cast<std::size_t>(active_index)];

    // ── 2. 悬浮件 ──
    draw_file_chip(ctx, state, active_view.show_map_axis);
    if (dock.show_perf_hud) {
        draw_perf_chip(ctx, state, active_view.show_map_axis);
    }
    draw_camera_pill(ctx, active_view, actions, dock);
    draw_fabs(ctx, state, actions, active_index);
    draw_dock(ctx, state);
    draw_dock_card(ctx, state, actions, active_index, result);
    draw_floating_navigation_map(ctx, state, active_index);
    draw_hint_pill(ctx, dock);

    // ── 3. Esc 收起卡片 ──
    if (dock.open_card != gs3d::app::DockCard::kNone &&
        !ImGui::GetIO().WantTextInput &&
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        dock.open_card = gs3d::app::DockCard::kNone;
    }

    return result;
}

bool draw_preload_gate_if_active(
    gs3d::app::AppState& state,
    float ui_scale
) {
    const auto& progress = state.tile_preload;
    // 显示进度对真实进度做指数趋近，读取→上传阶段切换与逐帧跳变都被
    // 平滑掉。active 边缘复位。
    static float smoothed_fraction = 0.0f;
    static bool was_active = false;
    if (!progress.active) {
        was_active = false;
        return false;
    }
    if (!was_active) {
        smoothed_fraction = 0.0f;
        was_active = true;
    }
    const float s = std::max(0.5f, ui_scale);

    // 门禁期间不渲染任何视口：数据尚未就绪，「加载好了再进程序」。
    for (auto& view : state.render_views) {
        view.render_requested = false;
    }

    // 连续单进度：读取（后台读盘 + 预建显存）约占前 35%，上传占其余。
    // 两个阶段都有真实计数，进度条从头到尾单调前进、无阶段跳变。
    float target_fraction = 0.0f;
    if (progress.total_tiles > 0) {
        const float read_fraction =
            static_cast<float>(progress.read_tiles) /
            static_cast<float>(progress.total_tiles);
        const float upload_fraction =
            static_cast<float>(progress.resident_tiles) /
            static_cast<float>(progress.total_tiles);
        target_fraction = std::clamp(
            0.35f * read_fraction + 0.65f * upload_fraction,
            0.0f, 1.0f);
    }
    const float dt = std::min(ImGui::GetIO().DeltaTime, 0.1f);
    smoothed_fraction +=
        (target_fraction - smoothed_fraction) *
        std::min(1.0f, dt * 6.0f);

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    // 浅色启动页：跟随主题表面色（默认碳蓝主题即白色风格）。
    ImGui::PushStyleColor(ImGuiCol_WindowBg, palette::kBg);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoDocking;
    if (ImGui::Begin("###TilePreloadGate", nullptr, flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 center(
            viewport->Pos.x + viewport->Size.x * 0.5f,
            viewport->Pos.y + viewport->Size.y * 0.5f
        );
        const ImU32 text_main = to_u32(palette::kText, 255);
        const ImU32 text_dim = to_u32(palette::kTextDim, 235);

        // 数据集名 + 阶段标题
        {
            ScopedFont font(bold_font());
            const char* name = state.dataset.active_dataset.empty()
                ? "GeoScatter3D"
                : state.dataset.active_dataset.c_str();
            const ImVec2 ts = ImGui::CalcTextSize(name);
            dl->AddText(
                ImVec2(center.x - ts.x * 0.5f, center.y - 64.0f * s),
                text_main, name);
        }
        {
            ScopedFont font(medium_font());
            const char* title = progress.reading
                ? "正在读取数据文件…"
                : "正在载入显存…";
            const ImVec2 ts = ImGui::CalcTextSize(title);
            dl->AddText(
                ImVec2(center.x - ts.x * 0.5f, center.y - 34.0f * s),
                text_dim, title);
        }

        // 连续进度条（浅灰轨道 + 主题强调色填充，圆角胶囊）。
        const float bar_w = 380.0f * s;
        const float bar_h = 8.0f * s;
        const ImVec2 bar_min(center.x - bar_w * 0.5f, center.y);
        const ImVec2 bar_max(center.x + bar_w * 0.5f, center.y + bar_h);
        dl->AddRectFilled(
            bar_min, bar_max,
            to_u32(palette::kBorder, 255), bar_h * 0.5f);
        const float fill_w = bar_w * smoothed_fraction;
        if (fill_w > bar_h) {
            dl->AddRectFilled(
                bar_min,
                ImVec2(bar_min.x + fill_w, bar_max.y),
                to_u32(palette::kAccent, 255), bar_h * 0.5f);
        }

        // 明细行：总体百分比 + 瓦片与数据量
        {
            ScopedFont font(small_font());
            char detail[160];
            std::snprintf(detail, sizeof(detail),
                "%d%% · %s / %s 瓦片 · %.0f MB",
                static_cast<int>(smoothed_fraction * 100.0f),
                format_thousands(progress.reading
                    ? progress.read_tiles
                    : progress.resident_tiles).c_str(),
                format_thousands(progress.total_tiles).c_str(),
                static_cast<double>(progress.total_bytes) / 1e6);
            const ImVec2 ts = ImGui::CalcTextSize(detail);
            dl->AddText(
                ImVec2(center.x - ts.x * 0.5f,
                       bar_max.y + 14.0f * s),
                text_dim, detail);
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    return true;
}

} // namespace gs3d::ui
