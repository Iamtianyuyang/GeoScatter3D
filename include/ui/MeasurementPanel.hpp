#pragma once

#include "app/AppState.hpp"

namespace gs3d::ui {

void draw_measurement_panel(
    gs3d::app::AppState& state,
    const char* window_name = nullptr,
    bool* open = nullptr,
    gs3d::app::MeasurementManager* measurement = nullptr
);

} // namespace gs3d::ui
