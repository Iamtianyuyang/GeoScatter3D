#pragma once

#include "app/AppState.hpp"

namespace gs3d::ui {

void draw_navigation_map(
    gs3d::app::AppState& state,
    const char* window_name = nullptr,
    bool* open = nullptr,
    gs3d::app::NavigationMapState* navigation_map = nullptr
);

} // namespace gs3d::ui
