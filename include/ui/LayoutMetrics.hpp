#pragma once

#include "imgui.h"

namespace gs3d::ui {

/*
 * 布局几何样式与度量（与颜色主题完全解耦）。
 * 负责定义界面的几何尺度：圆角、边框宽度、面板切分比例与间距。
 */
struct LayoutGeometry {
    float window_rounding = 6.0f;
    float child_rounding = 4.0f;
    float frame_rounding = 4.0f;
    float popup_rounding = 6.0f;
    float scrollbar_rounding = 6.0f;
    float grab_rounding = 3.0f;
    float tab_rounding = 4.0f;

    float window_border_size = 1.0f;
    float child_border_size = 0.0f;
    float popup_border_size = 1.0f;
    float frame_border_size = 0.0f;
    float tab_border_size = 0.0f;
};

// 应用几何度量到 ImGuiStyle（纯几何，不涉及任何颜色）
void apply_layout_geometry(const LayoutGeometry& geom, float ui_scale);

// 默认桌面端测绘软件几何规范
[[nodiscard]] const LayoutGeometry& default_layout_geometry() noexcept;

namespace LayoutMetrics {
    constexpr float kDockLeftRatio  = 280.0f / 1360.0f;
    constexpr float kDockLeftMinPx  = 260.0f;
    constexpr float kDockLeftMaxPx  = 420.0f;
    constexpr float kDockRightRatio = 280.0f / 1360.0f;
    constexpr float kDockRightMinPx = 260.0f;
    constexpr float kDockRightMaxPx = 380.0f;
    constexpr float kToolsBarHeightBase = 52.0f;
    constexpr float kStatusBarHeightBase  = 26.0f;
    constexpr float kPanelHeaderGap = 8.0f;
    constexpr float kPanelSectionGap = 8.0f;
    constexpr float kPanelInsetX = 10.0f;
} // namespace LayoutMetrics

} // namespace gs3d::ui
