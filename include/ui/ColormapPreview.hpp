#pragma once

#include "imgui.h"

namespace gs3d::ui {

/*
 * 色标（colormap）显示名与预览条，供属性面板和悬浮 Dock 的属性卡片
 * 共用。控制色须与 point shader 的色标实现保持一致。
 */

inline constexpr int kColormapCount = 9;

[[nodiscard]]
inline const char* colormap_display_name(int index) {
    static const char* names[kColormapCount] = {
        "地震（蓝-白-红）",
        "Viridis",
        "Jet",
        "Grayscale",
        "Thermal",
        "Coolwarm",
        "Turbo",
        "Plasma",
        "Rainbow256"
    };
    if (index < 0 || index >= kColormapCount) {
        index = 0;
    }
    return names[index];
}

// Five control colours per map, matching the shader. Drawing the preview
// as adjacent horizontal gradients keeps the legend free of the diagonal
// interpolation artefacts caused by a single four-corner rectangle.
struct ColormapPreviewColors {
    ImU32 colors[5];
};

[[nodiscard]]
inline const ColormapPreviewColors& colormap_preview_colors(int index) {
    static const ColormapPreviewColors table[kColormapCount] = {
        // Seismic: navy → blue → ivory → red → maroon
        { { IM_COL32(13, 41, 87, 255), IM_COL32(51, 125, 184, 255),
            IM_COL32(240, 237, 224, 255), IM_COL32(196, 69, 51, 255),
            IM_COL32(110, 13, 20, 255) } },
        { { IM_COL32(68, 1, 84, 255), IM_COL32(72, 36, 117, 255),
            IM_COL32(33, 145, 140, 255), IM_COL32(94, 201, 98, 255),
            IM_COL32(253, 231, 37, 255) } },
        { { IM_COL32(0, 0, 128, 255), IM_COL32(0, 128, 255, 255),
            IM_COL32(0, 255, 255, 255), IM_COL32(255, 255, 0, 255),
            IM_COL32(255, 0, 0, 255) } },
        { { IM_COL32(0, 0, 0, 255), IM_COL32(64, 64, 64, 255),
            IM_COL32(128, 128, 128, 255), IM_COL32(192, 192, 192, 255),
            IM_COL32(255, 255, 255, 255) } },
        { { IM_COL32(0, 0, 0, 255), IM_COL32(128, 0, 0, 255),
            IM_COL32(220, 48, 0, 255), IM_COL32(255, 160, 0, 255),
            IM_COL32(255, 255, 180, 255) } },
        { { IM_COL32(59, 76, 192, 255), IM_COL32(120, 150, 230, 255),
            IM_COL32(245, 245, 245, 255), IM_COL32(230, 120, 120, 255),
            IM_COL32(180, 4, 38, 255) } },
        { { IM_COL32(48, 18, 59, 255), IM_COL32(24, 104, 184, 255),
            IM_COL32(38, 188, 135, 255), IM_COL32(235, 206, 47, 255),
            IM_COL32(122, 4, 3, 255) } },
        { { IM_COL32(13, 8, 135, 255), IM_COL32(84, 3, 160, 255),
            IM_COL32(182, 55, 121, 255), IM_COL32(237, 121, 33, 255),
            IM_COL32(240, 249, 33, 255) } },
        { { IM_COL32(0, 0, 128, 255), IM_COL32(0, 128, 255, 255),
            IM_COL32(0, 255, 128, 255), IM_COL32(255, 255, 0, 255),
            IM_COL32(128, 0, 0, 255) } },
    };
    if (index < 0 || index >= kColormapCount) {
        index = 0;
    }
    return table[index];
}

inline void draw_colormap_preview_bar(
    ImDrawList* dl,
    const ImVec2& min,
    const ImVec2& max,
    int colormap_index
) {
    const auto& cc = colormap_preview_colors(colormap_index);
    const float width = max.x - min.x;
    for (int i = 0; i < 4; ++i) {
        const float x0 = min.x + width * static_cast<float>(i) / 4.0f;
        const float x1 = min.x + width * static_cast<float>(i + 1) / 4.0f;
        dl->AddRectFilledMultiColor(
            ImVec2(x0, min.y),
            ImVec2(x1, max.y),
            cc.colors[i],
            cc.colors[i + 1],
            cc.colors[i + 1],
            cc.colors[i]
        );
    }
}

} // namespace gs3d::ui
