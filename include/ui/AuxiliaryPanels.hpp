#pragma once

#include "app/AppState.hpp"
#include "app/UiActions.hpp"

namespace gs3d::ui {

void draw_auxiliary_panels(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions
);

// TIA-159 方向 B：分模式绘制各面板内容（无窗口包装）
void draw_performance_content(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions
);

void draw_tile_detail_collapsible(gs3d::app::AppState& state);

void draw_lod_settings_collapsible(gs3d::app::AppState& state);

} // namespace gs3d::ui
