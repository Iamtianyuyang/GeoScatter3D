#pragma once

#include <cstdint>
#include <vector>

namespace gs3d::app {

struct ViewportFrameCmd {
    int           index = 0;
    bool          hovered = false;
    bool          active = false;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    float mouse_delta_x = 0.0f;
    float mouse_delta_y = 0.0f;
    float mouse_wheel = 0.0f;

    // Mouse position in viewport-local pixels (origin top-left), valid
    // whenever `hovered` is true — used for the hover tooltip, which
    // should track the cursor even when not dragging/active.
    float mouse_local_x = 0.0f;
    float mouse_local_y = 0.0f;

    bool rotate = false;
    bool pan = false;

    /*
     * Ctrl+左键拖框完成时为 true，框选矩形以视口本地像素坐标给出
     * （原点在视口左上角，与 MouseRay::from_screen 的 mouse_x/mouse_y
     * 约定一致）。只在松开鼠标的那一帧为 true，下一帧自动复位。
     */
    bool  box_select_completed = false;
    float box_select_min_x = 0.0f;
    float box_select_min_y = 0.0f;
    float box_select_max_x = 0.0f;
    float box_select_max_y = 0.0f;

    [[nodiscard]]
    bool interacting() const noexcept {
        return
            (rotate &&
             (mouse_delta_x != 0.0f || mouse_delta_y != 0.0f)) ||
            (pan &&
             (mouse_delta_x != 0.0f || mouse_delta_y != 0.0f)) ||
            mouse_wheel != 0.0f;
    }
};

struct UiActions {
    bool open_requested = false;
    bool save_requested = false;
    bool add_data_requested = false;
    bool remove_requested = false;
    bool properties_requested = false;
    bool screenshot_requested = false;
    bool preferences_requested = false;
    bool clear_cache_requested = false;
    int reset_camera_index = -1;

    bool  point_size_changed = false;
    float point_size = 1.0f;

    bool color_by_changed = false;
    int  color_by_index = 0;

    std::vector<ViewportFrameCmd> viewport_frames;
};

} // namespace gs3d::app
