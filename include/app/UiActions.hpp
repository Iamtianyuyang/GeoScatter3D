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
    // only when `mouse_on_image` is true. These coordinates are in the
    // framebuffer/image space consumed by MouseRay::from_screen.
    float mouse_local_x = 0.0f;
    float mouse_local_y = 0.0f;
    bool mouse_on_image = false;

    bool rotate = false;
    bool pan = false;
    bool point_double_clicked = false;

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

    bool height_by_changed = false;
    int  height_by_index = 0;
    bool height_exag_changed = false;
    float height_exag = 1.0f;

    bool color_by_changed = false;
    int  color_by_index = 0;

    bool colormap_changed = false;
    int  colormap_index = 0;

    bool  value_clip_changed = false;
    bool  value_clip_enabled = false;
    float value_clip_min = 0.0f;
    float value_clip_max = 1.0f;

    bool point_shape_changed = false;
    int  point_shape = 0;

    std::vector<ViewportFrameCmd> viewport_frames;
};

} // namespace gs3d::app
