#pragma once

#include "app/AppState.hpp"
#include "app/UiActions.hpp"
#include "ui/color/Theme.hpp"

namespace gs3d::ui {

struct AnalysisRailLayout {
    float rail_width = 0.0f;
    float drawer_width = 0.0f;
    float topbar_height = 0.0f;
    float cards_width = 0.0f;
    float cards_gap = 0.0f;
    float status_height = 0.0f;
    float center_x = 0.0f;
    float viewport_x = 0.0f;
    float viewport_y = 0.0f;
    float viewport_width = 0.0f;
    float viewport_height = 0.0f;
    float cards_x = 0.0f;
    float status_y = 0.0f;
};

[[nodiscard]]
AnalysisRailLayout compute_analysis_rail_layout(
    float work_width,
    float work_height,
    float ui_scale,
    float drawer_fraction
) noexcept;

// draw_analysis_rail_layout 的帧结果
struct AnalysisRailFrameResult {
    bool theme_change_requested = false;
    ThemeId requested_theme = ThemeId::kDeepGraphite;
};

/*
 * 方案 C · 侧轨抽屉分析舱布局 (Analysis Rail)
 * 左侧 54px 垂直图标导轨 + 点击展开互斥抽屉 + 右侧快捷分析卡片
 */
[[nodiscard]]
AnalysisRailFrameResult draw_analysis_rail_layout(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    float ui_scale
);

void draw_analysis_rail_overlay(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    float ui_scale
);

} // namespace gs3d::ui
