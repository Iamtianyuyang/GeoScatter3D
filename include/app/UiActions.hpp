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

    bool rotate = false;
    bool pan = false;

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
