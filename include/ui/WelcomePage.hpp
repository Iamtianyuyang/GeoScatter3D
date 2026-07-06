#pragma once

#include "app/AppState.hpp"
#include "app/UiActions.hpp"

namespace gs3d::ui {

// Draws the startup landing page inside the current ImGui child window.
// Returns true when the user asks to enter the main workspace.
[[nodiscard]]
bool draw_welcome_page(
    const gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    float ui_scale
);

} // namespace gs3d::ui
