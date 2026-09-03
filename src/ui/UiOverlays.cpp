#include "ui/UiOverlays.hpp"

#include "ui/UiFonts.hpp"
#include "ui/Theme.hpp"
#include "ui/UiPalette.hpp"
#include "ui/Widgets.hpp"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace gs3d::ui {

namespace {

constexpr float kMapAxisTopBandBase = 28.0f;
constexpr float kSideMargin = 16.0f;
constexpr float kTopMargin = 12.0f;

constexpr ImGuiWindowFlags kOverlayWindowFlags =
    ImGuiWindowFlags_NoTitleBar |
    ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoScrollbar |
    ImGuiWindowFlags_NoScrollWithMouse |
    ImGuiWindowFlags_NoCollapse |
    ImGuiWindowFlags_AlwaysAutoResize |
    ImGuiWindowFlags_NoSavedSettings |
    ImGuiWindowFlags_NoFocusOnAppearing |
    ImGuiWindowFlags_NoNav |
    ImGuiWindowFlags_NoDocking;

ImFont* bold_font()   { return gs3d::gui::ui_fonts().bold; }
ImFont* medium_font() { return gs3d::gui::ui_fonts().medium; }
ImFont* small_font()  { return gs3d::gui::ui_fonts().small; }

struct ScopedFont {
    explicit ScopedFont(ImFont* f) : pushed_(f != nullptr) {
        if (pushed_) ImGui::PushFont(f);
    }
    ~ScopedFont() {
        if (pushed_) ImGui::PopFont();
    }
    ScopedFont(const ScopedFont&) = delete;
    ScopedFont& operator=(const ScopedFont&) = delete;
private:
    bool pushed_ = false;
};

std::string format_thousands(std::uint64_t value) {
    std::string digits = std::to_string(value);
    std::string out;
    out.reserve(digits.size() + digits.size() / 3);
    const int len = static_cast<int>(digits.size());
    for (int i = 0; i < len; ++i) {
        if (i > 0 && (len - i) % 3 == 0) {
            out.push_back(',');
        }
        out.push_back(digits[static_cast<std::size_t>(i)]);
    }
    return out;
}

ImVec4 glass_bg_vec4() {
    ImVec4 c = palette::kSurface;
    c.w = 0.93f;
    return c;
}

bool theme_is_dark() {
    return theme_tokens(active_theme()).dark;
}

ImVec4 glass_border_vec4() {
    return theme_is_dark()
        ? ImVec4(1.0f, 1.0f, 1.0f, 0.13f)
        : ImVec4(1.0f, 1.0f, 1.0f, 0.65f);
}

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

ImVec2 icon_pt(const ImVec2& c, float k, float x, float y) {
    return ImVec2(c.x + (x - 12.0f) * (k / 24.0f),
                  c.y + (y - 12.0f) * (k / 24.0f));
}

using IconFn = void (*)(ImDrawList*, const ImVec2&, float, ImU32, float);

void icon_reset(ImDrawList* dl, const ImVec2& c, float k, ImU32 col, float th) {
    const float r = k * 0.375f;
    dl->PathArcTo(c, r, 2.6f, 7.6f);
    dl->PathStroke(col, 0, th);
    const ImVec2 tip(c.x + r * std::cos(2.6f), c.y + r * std::sin(2.6f));
    dl->AddLine(tip, ImVec2(tip.x - k * 0.16f, tip.y + k * 0.03f), col, th);
    dl->AddLine(tip, ImVec2(tip.x + k * 0.02f, tip.y + k * 0.17f), col, th);
}

void icon_axis(ImDrawList* dl, const ImVec2& c, float k, ImU32 col, float th) {
    const ImVec2 o = icon_pt(c, k, 6.0f, 18.0f);
    const ImVec2 x = icon_pt(c, k, 19.0f, 18.0f);
    const ImVec2 y = icon_pt(c, k, 6.0f, 5.0f);
    dl->AddLine(o, x, col, th);
    dl->AddLine(o, y, col, th);
    dl->AddLine(x, ImVec2(x.x - k * 0.14f, x.y - k * 0.10f), col, th);
    dl->AddLine(x, ImVec2(x.x - k * 0.14f, x.y + k * 0.10f), col, th);
    dl->AddLine(y, ImVec2(y.x - k * 0.10f, y.y + k * 0.14f), col, th);
    dl->AddLine(y, ImVec2(y.x + k * 0.10f, y.y + k * 0.14f), col, th);
}

void icon_world_axis(ImDrawList* dl, const ImVec2& c, float k, ImU32 col, float th) {
    const ImVec2 o = icon_pt(c, k, 12.0f, 13.0f);
    const ImVec2 x = icon_pt(c, k, 20.0f, 17.5f);
    const ImVec2 y = icon_pt(c, k, 4.0f, 17.5f);
    const ImVec2 z = icon_pt(c, k, 12.0f, 4.5f);
    dl->AddLine(o, x, col, th);
    dl->AddLine(o, y, col, th);
    dl->AddLine(o, z, col, th);
}

void icon_crosshair(ImDrawList* dl, const ImVec2& c, float k, ImU32 col, float th) {
    const float r = k * 0.35f;
    dl->AddCircle(c, r, col, 0, th);
    dl->AddLine(ImVec2(c.x, c.y - r - k * 0.10f),
                ImVec2(c.x, c.y - r * 0.35f), col, th);
    dl->AddLine(ImVec2(c.x, c.y + r * 0.35f),
                ImVec2(c.x, c.y + r + k * 0.10f), col, th);
    dl->AddLine(ImVec2(c.x - r - k * 0.10f, c.y),
                ImVec2(c.x - r * 0.35f, c.y), col, th);
    dl->AddLine(ImVec2(c.x + r * 0.35f, c.y),
                ImVec2(c.x + r + k * 0.10f, c.y), col, th);
}

void icon_crosshair_style(ImDrawList* dl, const ImVec2& c, float k, ImU32 col, float th) {
    icon_crosshair(dl, c, k * 0.85f, col, th);
    dl->AddCircle(c, 1.5f * th, col, 0, th);
}

void icon_expand(ImDrawList* dl, const ImVec2& c, float k, ImU32 col, float th) {
    const float l = k * 0.18f;
    const ImVec2 tl = icon_pt(c, k, 5.0f, 5.0f);
    dl->AddLine(tl, ImVec2(tl.x + l, tl.y), col, th);
    dl->AddLine(tl, ImVec2(tl.x, tl.y + l), col, th);
    const ImVec2 tr = icon_pt(c, k, 19.0f, 5.0f);
    dl->AddLine(tr, ImVec2(tr.x - l, tr.y), col, th);
    dl->AddLine(tr, ImVec2(tr.x, tr.y + l), col, th);
    const ImVec2 bl = icon_pt(c, k, 5.0f, 19.0f);
    dl->AddLine(bl, ImVec2(bl.x + l, bl.y), col, th);
    dl->AddLine(bl, ImVec2(bl.x, bl.y - l), col, th);
    const ImVec2 br = icon_pt(c, k, 19.0f, 19.0f);
    dl->AddLine(br, ImVec2(br.x - l, br.y), col, th);
    dl->AddLine(br, ImVec2(br.x, br.y - l), col, th);
}

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
    if (tooltip && hovered) {
        ImGui::SetTooltip("%s", tooltip);
    }
    return ImGui::IsItemClicked(ImGuiMouseButton_Left);
}

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
                "颜色##CrosshairColor",
                color,
                ImGuiColorEditFlags_NoInputs
            )) {
            view.crosshair_color = ImGui::ColorConvertFloat4ToU32(
                ImVec4(color[0], color[1], color[2], color[3])
            );
        }
        ImGui::SameLine();
        if (widgets::Chip("恢复默认##Crosshair")) {
            view.crosshair_color = IM_COL32(0xF1, 0xC2, 0x1B, 0xFF);
        }
    } else {
        ImGui::TextDisabled("先启用十字准线后可设置颜色");
    }
    ImGui::Separator();
    ImGui::TextUnformatted("拾取准星");
    ImVec4 reticle = ImGui::ColorConvertU32ToFloat4(view.reticle_color);
    float color[4] = {reticle.x, reticle.y, reticle.z, reticle.w};
    if (ImGui::ColorEdit4(
            "颜色##ReticleColor",
            color,
            ImGuiColorEditFlags_NoInputs
        )) {
        view.reticle_color = ImGui::ColorConvertFloat4ToU32(
            ImVec4(color[0], color[1], color[2], color[3])
        );
    }
    ImGui::SameLine();
    if (widgets::Chip("恢复默认##Reticle")) {
        view.reticle_color = IM_COL32(0xF1, 0xC2, 0x1B, 0xFF);
    }
    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Text, to_u32(palette::kTextDim, 175));
    ImGui::TextUnformatted("左键旋转 · 右键平移 · 滚轮缩放 · F 聚焦");
    ImGui::PopStyleColor();
    ImGui::EndPopup();
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
            canvas_right - kSideMargin * s,
            canvas_top + axis_clearance + kTopMargin * s
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

bool draw_preload_gate_if_active(
    gs3d::app::AppState& state,
    float ui_scale
) {
    const auto& progress = state.tile_preload;
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

    for (auto& view : state.render_views) {
        view.render_requested = false;
    }

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
