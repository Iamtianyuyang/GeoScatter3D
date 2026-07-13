#include "app/ViewportInteractionState.hpp"

#include <algorithm>

namespace gs3d::app {

ViewportInteractionState::ViewportInteractionState(
    const std::size_t viewport_count,
    const float rotate_activation_threshold_px
)
    : rotate_activation_threshold_squared_(
          std::max(rotate_activation_threshold_px, 0.0f) *
          std::max(rotate_activation_threshold_px, 0.0f)
      )
    , previous_rotate_(viewport_count, false)
    , mouse_down_x_(viewport_count, 0.0f)
    , mouse_down_y_(viewport_count, 0.0f)
    , rotation_activated_(viewport_count, false)
{
}

std::size_t ViewportInteractionState::viewport_count() const noexcept
{
    return previous_rotate_.size();
}

void ViewportInteractionState::apply_rotation_gate(
    const ViewportFrameCmd& frame,
    gs3d::camera::CameraInput& input
) noexcept
{
    if (frame.index < 0 ||
        static_cast<std::size_t>(frame.index) >= previous_rotate_.size()) {
        return;
    }

    const auto index = static_cast<std::size_t>(frame.index);
    const bool pressed = frame.rotate && !previous_rotate_[index];
    if (pressed) {
        mouse_down_x_[index] = frame.mouse_local_x;
        mouse_down_y_[index] = frame.mouse_local_y;
        rotation_activated_[index] = false;
    }

    if (frame.rotate) {
        if (!rotation_activated_[index]) {
            const float dx = frame.mouse_local_x - mouse_down_x_[index];
            const float dy = frame.mouse_local_y - mouse_down_y_[index];
            if (dx * dx + dy * dy >= rotate_activation_threshold_squared_) {
                rotation_activated_[index] = true;
                input.rotate_begin = true;
            } else {
                // A press or sub-threshold movement is a click, not a turn.
                input.delta_x = 0.0f;
                input.delta_y = 0.0f;
            }
        }
    } else {
        rotation_activated_[index] = false;
    }

    previous_rotate_[index] = frame.rotate;
}

} // namespace gs3d::app
