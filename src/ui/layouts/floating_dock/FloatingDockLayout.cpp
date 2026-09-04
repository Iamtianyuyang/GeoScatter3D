#include "ui/layouts/floating_dock/FloatingDockLayout.hpp"
#include "ui/UiRoot.hpp"

#include <algorithm>

namespace gs3d::ui {

FloatingDockTopOverlayLayout compute_floating_dock_top_overlay_layout(
    const float work_x,
    const float work_y,
    const float ui_scale,
    const bool show_map_axis
) noexcept {
    constexpr float kTopMargin = 14.0f;
    constexpr float kSideMargin = 16.0f;
    constexpr float kSecondaryOffset = 48.0f;
    const float scale = std::max(0.0f, ui_scale);
    const float axis_clearance =
        show_map_axis ? kMapAxisTopBandBase : 0.0f;
    const float left_clearance =
        show_map_axis ? kMapAxisLeftBandBase : 0.0f;
    const float primary_y =
        work_y + (axis_clearance + kTopMargin) * scale;
    return {
        .left_x = work_x + (left_clearance + kSideMargin) * scale,
        .primary_y = primary_y,
        .secondary_y = primary_y + kSecondaryOffset * scale,
    };
}

bool floating_dock_allows_viewport_input(
    const bool card_open,
    const bool card_animating,
    const bool pointer_over_overlay
) noexcept {
    return !card_open && !card_animating && !pointer_over_overlay;
}

NavigationPreviewLayout compute_navigation_preview_layout(
    const float x,
    const float y,
    const float available_width,
    const float texture_width,
    const float texture_height
) noexcept {
    return compute_navigation_preview_layout(
        x,
        y,
        available_width,
        available_width,
        texture_width,
        texture_height
    );
}

NavigationPreviewLayout compute_navigation_preview_layout(
    const float x,
    const float y,
    const float available_width,
    const float available_height,
    const float texture_width,
    const float texture_height
) noexcept {
    const float width = std::max(0.0f, available_width);
    const float height = std::max(0.0f, available_height);
    NavigationPreviewLayout layout{
        .container = {x, y, width, height},
        .image = {x, y, width, height},
    };
    if (width <= 0.0f ||
        height <= 0.0f ||
        texture_width <= 0.0f ||
        texture_height <= 0.0f) {
        return layout;
    }

    const float texture_aspect = texture_width / texture_height;
    const float container_aspect = width / height;
    if (texture_aspect >= container_aspect) {
        const float image_height = width / texture_aspect;
        layout.image.height = image_height;
        layout.image.y = y + (height - image_height) * 0.5f;
    } else {
        const float image_width = height * texture_aspect;
        layout.image.width = image_width;
        layout.image.x = x + (width - image_width) * 0.5f;
    }
    return layout;
}

void draw_floating_dock_overlay(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    float ui_scale
) {
    (void)draw_floating_dock_layout(state, actions, ui_scale);
}

} // namespace gs3d::ui
