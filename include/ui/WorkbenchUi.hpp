#pragma once

#include "app/AppState.hpp"
#include "app/UiActions.hpp"

namespace gs3d::ui {

void draw_workbench_view_controls(
    gs3d::app::AppState& state,
    gs3d::app::RenderViewState& view,
    gs3d::app::UiActions& actions,
    float ui_scale
);

} // namespace gs3d::ui
