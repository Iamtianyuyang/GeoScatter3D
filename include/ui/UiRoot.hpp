#pragma once

#include "app/AppState.hpp"
#include "app/UiActions.hpp"

struct ImFont;

namespace gs3d::ui {

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
    // Values must match LayoutMetrics::kAxisTopH / kAxisLeftW in
    // ViewportCanvas.cpp.
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
// to get absolute ImGui screen coordinates.
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

// Dock 布局持久化状态与首帧决策（纯逻辑，无 ImGui 依赖，便于单测）：
// 本会话首次构建时若 ini 已恢复出持久化 DockSpace 节点，应采纳用户布局而
// 不重建（TIA-90）。built_once 只记录"首次构建已发生"：运行期"恢复默认
// 工作区"清空 initialized 后不会重新采纳旧布局，而是照常重建默认布局。
struct DockLayoutPersistState {
    // 工作台布局当前是否已生效（恢复默认工作区会清空以触发重建）。
    bool initialized = false;
    bool built_once = false;
    std::uint32_t signature = 0;
};

// 返回 true 表示保留当前布局（含首帧采纳 ini 恢复的持久化布局），
// 本次无需重建默认布局；返回 false 表示调用方应重建。
[[nodiscard]]
inline bool keep_current_dock_layout(
    DockLayoutPersistState& state,
    bool signature_matches,
    bool size_changed_significantly,
    bool persisted_node_available,
    std::uint32_t signature
) noexcept
{
    if (state.initialized && signature_matches &&
        !size_changed_significantly) {
        return true;
    }
    if (!state.built_once) {
        state.built_once = true;
        if (!state.initialized && persisted_node_available) {
            // 首帧采纳 ini 恢复的持久化布局，不再整树重建。
            state.initialized = true;
            state.signature = signature;
            return true;
        }
    }
    return false;
}

// Font helpers used by panel drawing functions.
[[nodiscard]] ::ImFont* panel_title_font();

// Shared panel drawing helpers.
void draw_panel_section_label(const char* label);
[[nodiscard]] float bytes_to_mb(std::uint64_t bytes);

class UiRoot {
public:
    UiRoot() = default;

    [[nodiscard]]
    gs3d::app::UiActions draw(gs3d::app::AppState& state);

private:
    void build_default_layout(const gs3d::app::AppState& state);

    DockLayoutPersistState dock_layout_;
    // Last work size (px) used when building the default dock layout. A large
    // relative change (e.g. maximize/restore) forces a rebuild so the side
    // bars re-apply their ratio-based widths; small resizes leave any
    // user-dragged splitters untouched. Stored as plain floats (not ImVec2)
    // so this header stays free of the imgui.h dependency — several test
    // targets include it without linking ImGui.
    float last_layout_work_w_ = -1.0f;
    float last_layout_work_h_ = -1.0f;
    bool focus_workbench_dataset_ = true;
};

} // namespace gs3d::ui
