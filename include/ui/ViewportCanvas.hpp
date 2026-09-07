#pragma once

#include "app/AppState.hpp"
#include "app/UiActions.hpp"

#include <cstdint>

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

// ─── 视口几何工具（原 UiRoot.hpp，纯逻辑、无 GUI 依赖）──────────────

inline constexpr float kMapAxisTopBandBase = 26.0f;
inline constexpr float kMapAxisLeftBandBase = 46.0f;

struct ViewportScreenRect {
    float min_x = 0.0f;
    float min_y = 0.0f;
    float max_x = 0.0f;
    float max_y = 0.0f;

    [[nodiscard]]
    float width() const noexcept {
        return max_x - min_x;
    }

    [[nodiscard]]
    float height() const noexcept {
        return max_y - min_y;
    }
};

struct ViewportMouseMapping {
    bool mouse_on_image = false;
    float framebuffer_x = 0.0f;
    float framebuffer_y = 0.0f;
    ViewportScreenRect plot_rect{};
};

[[nodiscard]]
inline ViewportScreenRect compute_plot_rect(
    bool show_map_axis,
    const ViewportScreenRect& canvas_rect,
    float ui_scale = 1.0f
) noexcept
{
    ViewportScreenRect plot_rect = canvas_rect;
    if (!show_map_axis) {
        return plot_rect;
    }

    // Outside scientific-style axes: X on top, Y on left.
    // Keep just enough band for outer ticks/labels without growing
    // the bottom/right gutters.
    const float axis_top_h  = kMapAxisTopBandBase * ui_scale;
    const float axis_left_w = kMapAxisLeftBandBase * ui_scale;
    plot_rect.min_x += axis_left_w;
    plot_rect.min_y += axis_top_h;
    return plot_rect;
}

[[nodiscard]]
inline ViewportMouseMapping map_screen_mouse_to_framebuffer(
    float mouse_screen_x,
    float mouse_screen_y,
    const ViewportScreenRect& canvas_rect,
    bool show_map_axis,
    std::uint32_t framebuffer_width,
    std::uint32_t framebuffer_height,
    float ui_scale = 1.0f
) noexcept
{
    ViewportMouseMapping mapping;
    mapping.plot_rect = compute_plot_rect(show_map_axis, canvas_rect, ui_scale);

    const float plot_width = mapping.plot_rect.width();
    const float plot_height = mapping.plot_rect.height();
    if (plot_width <= 0.0f ||
        plot_height <= 0.0f ||
        framebuffer_width == 0 ||
        framebuffer_height == 0) {
        return mapping;
    }

    if (mouse_screen_x < mapping.plot_rect.min_x ||
        mouse_screen_x >= mapping.plot_rect.max_x ||
        mouse_screen_y < mapping.plot_rect.min_y ||
        mouse_screen_y >= mapping.plot_rect.max_y) {
        return mapping;
    }

    const float local_x = mouse_screen_x - mapping.plot_rect.min_x;
    const float local_y = mouse_screen_y - mapping.plot_rect.min_y;
    mapping.mouse_on_image = true;
    mapping.framebuffer_x =
        local_x * static_cast<float>(framebuffer_width) / plot_width;
    mapping.framebuffer_y =
        local_y * static_cast<float>(framebuffer_height) / plot_height;
    return mapping;
}

// Inverse of map_screen_mouse_to_framebuffer: framebuffer pixel coords →
// screen position within the plot_rect.  The caller adds plot_rect.min
// to get absolute screen coordinates.
struct ScreenPoint { float x, y; };

[[nodiscard]]
inline ScreenPoint framebuffer_to_plot_screen(
    float fb_x,
    float fb_y,
    const ViewportScreenRect& canvas_rect,
    bool show_map_axis,
    std::uint32_t framebuffer_width,
    std::uint32_t framebuffer_height,
    float ui_scale = 1.0f
) noexcept
{
    const auto plot_rect = compute_plot_rect(show_map_axis, canvas_rect, ui_scale);
    const float plot_w = plot_rect.width();
    const float plot_h = plot_rect.height();
    if (plot_w <= 0.0f || plot_h <= 0.0f ||
        framebuffer_width == 0 || framebuffer_height == 0) {
        return {plot_rect.min_x, plot_rect.min_y};
    }
    return {
        fb_x * plot_w / static_cast<float>(framebuffer_width),
        fb_y * plot_h / static_cast<float>(framebuffer_height)
    };
}

} // namespace gs3d::ui
