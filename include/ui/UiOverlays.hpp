#pragma once

#include "app/AppState.hpp"
#include "app/UiActions.hpp"

namespace gs3d::ui {

void draw_screenshot_notice(
    gs3d::app::AppState& state,
    float ui_scale
);

bool draw_preload_gate_if_active(
    gs3d::app::AppState& state,
    float ui_scale
);

void draw_detached_view_camera_pill(
    gs3d::app::RenderViewState& view,
    gs3d::app::UiActions& actions,
    float canvas_top,
    float canvas_right,
    unsigned int platform_viewport_id,
    float ui_scale
);

} // namespace gs3d::ui
