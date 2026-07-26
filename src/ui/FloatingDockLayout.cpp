#include "ui/FloatingDockLayout.hpp"

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
    const float size = std::max(0.0f, available_width);
    NavigationPreviewLayout layout{
        .container = {x, y, size, size},
        .image = {x, y, size, size},
    };
    if (size <= 0.0f ||
        texture_width <= 0.0f ||
        texture_height <= 0.0f) {
        return layout;
    }

    const float texture_aspect = texture_width / texture_height;
    if (texture_aspect >= 1.0f) {
        layout.image.height = size / texture_aspect;
        layout.image.y += (size - layout.image.height) * 0.5f;
    } else {
        layout.image.width = size * texture_aspect;
        layout.image.x += (size - layout.image.width) * 0.5f;
    }
    return layout;
}

} // namespace gs3d::ui
