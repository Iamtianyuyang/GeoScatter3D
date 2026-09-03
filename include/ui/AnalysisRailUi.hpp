#pragma once

#include "app/AppState.hpp"
#include "app/UiActions.hpp"

namespace gs3d::ui {

/*
 * 方案 C · 侧轨抽屉分析舱布局 (Analysis Rail)
 * 左侧 54px 垂直图标导轨 + 点击展开互斥抽屉 + 右侧快捷分析卡片
 */
void draw_analysis_rail_overlay(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    float ui_scale
);

} // namespace gs3d::ui
