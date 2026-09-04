#include "ui/layouts/analysis_rail/AnalysisRailLayout.hpp"

#include <algorithm>

namespace gs3d::ui {

AnalysisRailLayout compute_analysis_rail_layout(
    const float work_width,
    const float work_height,
    const float ui_scale,
    const float drawer_fraction
) noexcept {
    const float scale = std::max(0.5f, ui_scale);
    const float width = std::max(0.0f, work_width);
    const float height = std::max(0.0f, work_height);
    const float rail = std::min(width, 54.0f * scale);
    const float drawer = std::min(
        std::max(0.0f, width - rail),
        272.0f * scale * std::clamp(drawer_fraction, 0.0f, 1.0f)
    );
    const float topbar = std::min(height, 50.0f * scale);
    const float status = std::min(
        std::max(0.0f, height - topbar),
        24.0f * scale
    );
    const float center_x = rail + drawer;
    const float center_width = std::max(0.0f, width - center_x);
    const float cards = std::min(
        center_width,
        252.0f * scale
    );
    const float cards_gap = std::min(
        std::max(0.0f, center_width - cards),
        10.0f * scale
    );
    const float viewport_height =
        std::max(0.0f, height - topbar - status);
    return {
        .rail_width = rail,
        .drawer_width = drawer,
        .topbar_height = topbar,
        .cards_width = cards,
        .cards_gap = cards_gap,
        .status_height = status,
        .center_x = center_x,
        .viewport_x = center_x,
        .viewport_y = topbar,
        .viewport_width = std::max(
            0.0f,
            center_width - cards - cards_gap
        ),
        .viewport_height = viewport_height,
        .cards_x = width - cards,
        .status_y = height - status,
    };
}

void draw_analysis_rail_overlay(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    float ui_scale
) {
    (void)draw_analysis_rail_layout(state, actions, ui_scale);
}

} // namespace gs3d::ui
