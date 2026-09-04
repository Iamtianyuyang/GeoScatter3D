#pragma once

#include "app/AppState.hpp"
#include "app/UiActions.hpp"
#include "ui/color/Theme.hpp"

namespace gs3d::ui {

struct FloatingDockTopOverlayLayout {
    float left_x = 0.0f;
    float primary_y = 0.0f;
    float secondary_y = 0.0f;
};

struct FloatingDockRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct NavigationPreviewLayout {
    FloatingDockRect container;
    FloatingDockRect image;
};

[[nodiscard]] FloatingDockTopOverlayLayout
compute_floating_dock_top_overlay_layout(
    float work_x,
    float work_y,
    float ui_scale,
    bool show_map_axis
) noexcept;

[[nodiscard]] bool floating_dock_allows_viewport_input(
    bool card_open,
    bool card_animating,
    bool pointer_over_overlay
) noexcept;

[[nodiscard]] NavigationPreviewLayout compute_navigation_preview_layout(
    float x,
    float y,
    float available_width,
    float texture_width,
    float texture_height
) noexcept;

[[nodiscard]] NavigationPreviewLayout compute_navigation_preview_layout(
    float x,
    float y,
    float available_width,
    float available_height,
    float texture_width,
    float texture_height
) noexcept;


void draw_floating_dock_overlay(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    float ui_scale
);

} // namespace gs3d::ui
