#pragma once

#include "app/AppState.hpp"
#include "app/UiActions.hpp"

namespace gs3d::ui {

struct ViewportInputRouting {
    bool hovered = false;
    bool active = false;
};

[[nodiscard]]
inline ViewportInputRouting resolve_viewport_input_routing(
    const bool item_hovered,
    const bool item_active,
    const bool platform_window_focused,
    const bool interaction_enabled = true
) noexcept {
    return {
        .hovered =
            interaction_enabled &&
            platform_window_focused &&
            item_hovered,
        .active =
            interaction_enabled &&
            platform_window_focused &&
            item_active,
    };
}

struct ViewportCanvasOptions {
    int workspace_id = 0;
    bool show_info_badge = true;
    bool interaction_enabled = true;
};

} // namespace gs3d::ui
