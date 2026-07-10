#include "ui/Widgets.hpp"

#include "gui/UiFonts.hpp"
#include "ui/UiPalette.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cstdio>

namespace gs3d::ui::widgets {

namespace {

float uscale()
{
    return gs3d::gui::ui_fonts().ui_scale;
}

// 线性空间插值（palette 值已线性化，直接混即可）。
ImVec4 mix(const ImVec4& a, const ImVec4& b, float t)
{
    return ImVec4(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t
    );
}

ImU32 u32(const ImVec4& c, float alpha = 1.0f)
{
    ImVec4 v = c;
    v.w = alpha;
    return ImGui::ColorConvertFloat4ToU32(v);
}

// 悬停提亮一档（暗主题上加亮、亮主题上也偏亮，均可读）。
ImVec4 hover_lift(const ImVec4& c)
{
    return mix(c, ImVec4(1.0f, 1.0f, 1.0f, c.w), 0.10f);
}

void centered_text(
    ImDrawList* dl,
    const ImVec2& rect_min,
    const ImVec2& rect_max,
    const char* label,
    const char* label_end,
    ImU32 color
)
{
    const ImVec2 ts = ImGui::CalcTextSize(label, label_end);
    dl->AddText(
        ImVec2(
            rect_min.x + (rect_max.x - rect_min.x - ts.x) * 0.5f,
            rect_min.y + (rect_max.y - rect_min.y - ts.y) * 0.5f
        ),
        color,
        label,
        label_end
    );
}

// 输入类控件的状态描边 + 激活焦点环。frame 矩形须由调用方在提交
// 控件前记录（item rect 会把右侧标签算进去，不能直接用）。
void field_decoration(
    ImDrawList* dl,
    const ImVec2& min,
    const ImVec2& max,
    bool hovered,
    bool active
)
{
    const float s = uscale();
    const float r = ImGui::GetStyle().FrameRounding;
    if (active) {
        dl->AddRect(
            ImVec2(min.x - 2.0f * s, min.y - 2.0f * s),
            ImVec2(max.x + 2.0f * s, max.y + 2.0f * s),
            u32(palette::kAccent, 0.30f),
            r + 2.0f * s,
            0,
            2.0f * s
        );
    }
    const ImU32 border = active
        ? u32(palette::kAccent)
        : hovered
            ? u32(palette::kAccent, 0.65f)
            : u32(palette::kBorder);
    dl->AddRect(min, max, border, r, 0, 1.0f * s);
}

} // namespace

bool Button(
    const char* label,
    ButtonVariant variant,
    ImVec2 size_arg,
    bool small
)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) {
        return false;
    }
    const float s = uscale();
    const char* text_end = ImGui::FindRenderedTextEnd(label);
    const ImVec2 ts = ImGui::CalcTextSize(label, text_end);
    const ImVec2 pad = small
        ? ImVec2(11.0f * s, 3.5f * s)
        : ImVec2(16.0f * s, 6.0f * s);
    ImVec2 size(
        size_arg.x != 0.0f ? size_arg.x : ts.x + pad.x * 2.0f,
        size_arg.y != 0.0f ? size_arg.y : ts.y + pad.y * 2.0f
    );

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    ImDrawList* dl = window->DrawList;
    const float r = ImGui::GetStyle().FrameRounding;
    const ImVec2 max(pos.x + size.x, pos.y + size.y);

    ImVec4 bg(0.0f, 0.0f, 0.0f, 0.0f);
    ImU32 border = 0;
    ImVec4 text = palette::kText;

    switch (variant) {
    case ButtonVariant::kPrimary:
        // 底部投影撑出“可按”的厚度，按下时收起（视觉下沉一档）。
        if (!held) {
            dl->AddRectFilled(
                ImVec2(pos.x, pos.y + 2.0f * s),
                ImVec2(max.x, max.y + 2.0f * s),
                u32(palette::kAccentActive, 0.55f),
                r
            );
        }
        bg = held
            ? palette::kAccentActive
            : hovered ? hover_lift(palette::kAccent) : palette::kAccent;
        text = palette::kOnAccent;
        break;
    case ButtonVariant::kSecondary:
        bg = held ? palette::kFrameHover : palette::kSurfaceHover;
        border = u32(
            hovered || held ? palette::kAccent : palette::kBorder);
        text = hovered || held ? palette::kAccent : palette::kText;
        break;
    case ButtonVariant::kGhost:
        if (held) {
            bg = palette::kAccent;
            bg.w = 0.22f;
        } else if (hovered) {
            bg = palette::kAccent;
            bg.w = 0.12f;
        }
        text = palette::kAccent;
        break;
    case ButtonVariant::kDanger:
        if (held) {
            bg = palette::kRed;
            bg.w = 0.20f;
        } else if (hovered) {
            bg = palette::kRed;
            bg.w = 0.12f;
        }
        border = u32(palette::kRed, hovered || held ? 0.85f : 0.45f);
        text = palette::kRed;
        break;
    }

    if (bg.w > 0.0f) {
        dl->AddRectFilled(pos, max, ImGui::ColorConvertFloat4ToU32(bg), r);
    }
    if (border != 0) {
        dl->AddRect(pos, max, border, r, 0, 1.0f * s);
    }
    centered_text(dl, pos, max, label, text_end, u32(text));
    return pressed;
}

bool Chip(const char* label, bool active)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) {
        return false;
    }
    const float s = uscale();
    const char* text_end = ImGui::FindRenderedTextEnd(label);
    const ImVec2 ts = ImGui::CalcTextSize(label, text_end);
    const ImVec2 size(ts.x + 24.0f * s, ts.y + 7.0f * s);

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    ImDrawList* dl = window->DrawList;
    const ImVec2 max(pos.x + size.x, pos.y + size.y);
    const float r = size.y * 0.5f; // 胶囊

    if (active) {
        ImVec4 bg = palette::kAccent;
        bg.w = held ? 0.24f : 0.15f;
        dl->AddRectFilled(pos, max, ImGui::ColorConvertFloat4ToU32(bg), r);
    } else {
        dl->AddRectFilled(
            pos, max,
            u32(held ? palette::kFrameHover : palette::kSurfaceHover), r);
    }
    const bool lit = active || hovered || held;
    dl->AddRect(
        pos, max,
        u32(lit ? palette::kAccent : palette::kBorder), r, 0, 1.0f * s);
    centered_text(
        dl, pos, max, label, text_end,
        u32(lit ? palette::kAccent : palette::kText));
    return pressed;
}

bool Checkbox(const char* label, bool* v)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) {
        return false;
    }
    const float s = uscale();
    const float box = 16.0f * s;
    const float gap = 7.0f * s;
    const char* text_end = ImGui::FindRenderedTextEnd(label);
    const ImVec2 ts = ImGui::CalcTextSize(label, text_end);
    const ImVec2 size(
        box + (ts.x > 0.0f ? gap + ts.x : 0.0f),
        std::max(box, ts.y) + 4.0f * s
    );

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    if (pressed) {
        *v = !*v;
    }

    ImDrawList* dl = window->DrawList;
    const float r = std::min(ImGui::GetStyle().FrameRounding, 4.0f * s);
    const float by = pos.y + (size.y - box) * 0.5f;
    const ImVec2 bmin(pos.x, by);
    const ImVec2 bmax(pos.x + box, by + box);

    if (*v) {
        const ImVec4 fill = held
            ? palette::kAccentActive
            : hovered ? hover_lift(palette::kAccent) : palette::kAccent;
        dl->AddRectFilled(bmin, bmax, u32(fill), r);
        const ImVec2 pts[3] = {
            ImVec2(bmin.x + box * 0.26f, bmin.y + box * 0.54f),
            ImVec2(bmin.x + box * 0.43f, bmin.y + box * 0.70f),
            ImVec2(bmin.x + box * 0.74f, bmin.y + box * 0.33f),
        };
        dl->AddPolyline(
            pts, 3, u32(palette::kOnAccent), ImDrawFlags_None, 2.0f * s);
    } else {
        dl->AddRectFilled(bmin, bmax, u32(palette::kFrame, 0.96f), r);
        dl->AddRect(
            bmin, bmax,
            u32(hovered || held ? palette::kAccent : palette::kBorder),
            r, 0, 1.2f * s);
    }
    if (ts.x > 0.0f) {
        dl->AddText(
            ImVec2(
                pos.x + box + gap,
                pos.y + (size.y - ts.y) * 0.5f
            ),
            u32(palette::kText),
            label,
            text_end
        );
    }
    return pressed;
}

bool SliderFloat(
    const char* label,
    float* v,
    float v_min,
    float v_max,
    const char* format
)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) {
        return false;
    }
    const float s = uscale();
    const float width = ImGui::CalcItemWidth();
    const float h = ImGui::GetFrameHeight();

    char value_buf[32];
    std::snprintf(value_buf, sizeof(value_buf), format, *v);
    const float value_w =
        std::max(ImGui::CalcTextSize("0000.0").x,
                 ImGui::CalcTextSize(value_buf).x);
    const float track_w = std::max(width - value_w - 10.0f * s, 40.0f * s);

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(label, ImVec2(track_w, h));
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    bool changed = false;
    if (held) {
        const float t = std::clamp(
            (ImGui::GetIO().MousePos.x - pos.x) / track_w, 0.0f, 1.0f);
        const float nv = v_min + t * (v_max - v_min);
        if (nv != *v) {
            *v = nv;
            std::snprintf(value_buf, sizeof(value_buf), format, *v);
            changed = true;
        }
    }

    ImDrawList* dl = window->DrawList;
    const float cy = pos.y + h * 0.5f;
    const float half = 2.0f * s;
    const float t_cur = v_max > v_min
        ? std::clamp((*v - v_min) / (v_max - v_min), 0.0f, 1.0f)
        : 0.0f;
    const float thumb_r = 7.0f * s;
    const float tx = pos.x + thumb_r + t_cur * (track_w - thumb_r * 2.0f);

    dl->AddRectFilled(
        ImVec2(pos.x, cy - half),
        ImVec2(pos.x + track_w, cy + half),
        u32(palette::kBorder), half);
    dl->AddRectFilled(
        ImVec2(pos.x, cy - half),
        ImVec2(tx, cy + half),
        u32(palette::kAccent), half);

    const ImVec4 thumb = held
        ? palette::kAccentActive
        : hovered ? hover_lift(palette::kAccent) : palette::kAccent;
    // 主题拇指形状跟随 GrabRounding：碳蓝圆点，石墨小圆角方，琥珀近直角。
    const float grab_r = ImGui::GetStyle().GrabRounding;
    if (grab_r >= 3.5f * s) {
        dl->AddCircleFilled(ImVec2(tx, cy), thumb_r, u32(thumb));
        dl->AddCircle(
            ImVec2(tx, cy), thumb_r, u32(palette::kSurface), 0, 2.0f * s);
    } else {
        const ImVec2 tmin(tx - thumb_r, cy - thumb_r);
        const ImVec2 tmax(tx + thumb_r, cy + thumb_r);
        dl->AddRectFilled(tmin, tmax, u32(thumb), grab_r);
        dl->AddRect(
            tmin, tmax, u32(palette::kSurface), grab_r, 0, 2.0f * s);
    }

    const ImVec2 vts = ImGui::CalcTextSize(value_buf);
    dl->AddText(
        ImVec2(pos.x + width - vts.x, pos.y + (h - vts.y) * 0.5f),
        u32(palette::kText),
        value_buf
    );
    // 数值区也计入布局宽度
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::Dummy(ImVec2(width - track_w, h));
    return changed;
}

bool DragFloat(
    const char* label,
    float* v,
    float v_speed,
    float v_min,
    float v_max,
    const char* format,
    ImGuiSliderFlags flags
)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) {
        return false;
    }
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 max(
        pos.x + ImGui::CalcItemWidth(),
        pos.y + ImGui::GetFrameHeight()
    );
    const bool changed =
        ImGui::DragFloat(label, v, v_speed, v_min, v_max, format, flags);
    field_decoration(
        window->DrawList, pos, max,
        ImGui::IsItemHovered(), ImGui::IsItemActive());
    return changed;
}

bool BeginCombo(const char* label, const char* preview)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) {
        return false;
    }
    const float s = uscale();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 max(
        pos.x + ImGui::CalcItemWidth(),
        pos.y + ImGui::GetFrameHeight()
    );
    ImDrawList* dl = window->DrawList; // 装饰画在父窗口，与弹层无关

    const bool open =
        ImGui::BeginCombo(label, preview, ImGuiComboFlags_NoArrowButton);

    const bool hovered = ImGui::IsMouseHoveringRect(pos, max);
    field_decoration(dl, pos, max, hovered, open);

    // accent 下拉三角（替代原生箭头）
    const float cx = max.x - 13.0f * s;
    const float cy = pos.y + (max.y - pos.y) * 0.5f;
    dl->AddTriangleFilled(
        ImVec2(cx - 4.5f * s, cy - 2.5f * s),
        ImVec2(cx + 4.5f * s, cy - 2.5f * s),
        ImVec2(cx, cy + 3.0f * s),
        u32(palette::kAccent)
    );
    return open;
}

void EndCombo()
{
    ImGui::EndCombo();
}

bool Segmented(
    const char* id,
    const char* const items[],
    int count,
    int* current
)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems || count <= 0) {
        return false;
    }
    const float s = uscale();
    const float pad_x = 13.0f * s;
    const float inner = 2.0f * s;
    const float item_h = ImGui::GetTextLineHeight() + 9.0f * s;

    float total_w = inner;
    for (int i = 0; i < count; ++i) {
        total_w += ImGui::CalcTextSize(items[i]).x + pad_x * 2.0f + inner;
    }

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = window->DrawList;
    const float r = ImGui::GetStyle().FrameRounding + inner;
    const ImVec2 cmax(pos.x + total_w, pos.y + item_h + inner * 2.0f);
    dl->AddRectFilled(pos, cmax, u32(palette::kFrame, 0.96f), r);
    dl->AddRect(pos, cmax, u32(palette::kBorder), r, 0, 1.0f * s);

    bool changed = false;
    ImGui::PushID(id);
    float x = pos.x + inner;
    for (int i = 0; i < count; ++i) {
        const float w = ImGui::CalcTextSize(items[i]).x + pad_x * 2.0f;
        const ImVec2 imin(x, pos.y + inner);
        const ImVec2 imax(x + w, pos.y + inner + item_h);
        ImGui::SetCursorScreenPos(imin);
        ImGui::PushID(i);
        const bool pressed =
            ImGui::InvisibleButton(items[i], ImVec2(w, item_h));
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        if (pressed && *current != i) {
            *current = i;
            changed = true;
        }
        const bool selected = *current == i;
        if (selected) {
            dl->AddRectFilled(
                imin, imax, u32(palette::kAccent),
                ImGui::GetStyle().FrameRounding);
        }
        centered_text(
            dl, imin, imax, items[i], nullptr,
            u32(selected
                    ? palette::kOnAccent
                    : hovered ? palette::kAccent : palette::kTextDim));
        x += w + inner;
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy(ImVec2(total_w, item_h + inner * 2.0f));
    return changed;
}

void Meter(float fraction)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) {
        return;
    }
    const float s = uscale();
    const float w = ImGui::CalcItemWidth();
    const float h = 6.0f * s;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float f = std::clamp(fraction, 0.0f, 1.0f);

    ImDrawList* dl = window->DrawList;
    dl->AddRectFilled(
        pos, ImVec2(pos.x + w, pos.y + h),
        u32(palette::kBorder, 0.60f), h * 0.5f);
    if (f > 0.0f) {
        dl->AddRectFilled(
            pos,
            ImVec2(pos.x + std::max(w * f, h), pos.y + h),
            u32(palette::kAccent), h * 0.5f);
    }
    ImGui::Dummy(ImVec2(w, h));
}

} // namespace gs3d::ui::widgets
