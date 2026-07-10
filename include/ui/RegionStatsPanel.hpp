#pragma once

#include "app/AppState.hpp"

namespace gs3d::ui {

void draw_region_stats_panel(
    gs3d::app::AppState& state,
    const char* window_name = nullptr,
    bool* open = nullptr,
    const gs3d::app::RegionStatsResult* region_stats = nullptr
);

} // namespace gs3d::ui
