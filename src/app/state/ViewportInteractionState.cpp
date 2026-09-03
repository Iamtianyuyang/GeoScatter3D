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
    , rotation_origin_valid_(viewport_count, false)
    , rotation_activated_(viewport_count, false)
    , pending_delta_x_(viewport_count, 0.0f)
    , pending_delta_y_(viewport_count, 0.0f)
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
        rotation_origin_valid_[index] = frame.mouse_on_image;
        rotation_activated_[index] = false;
        pending_delta_x_[index] = 0.0f;
        pending_delta_y_[index] = 0.0f;
    }

    if (frame.rotate) {
        if (!rotation_origin_valid_[index]) {
            input.rotate = false;
            input.delta_x = 0.0f;
            input.delta_y = 0.0f;
        } else if (!rotation_activated_[index]) {
            // The press frame can contain MouseDelta accumulated before the
            // button went down. Ignore it, then accumulate actual drag deltas
            // in the same coordinate space consumed by CameraController.
            if (!pressed) {
                pending_delta_x_[index] += input.delta_x;
                pending_delta_y_[index] += input.delta_y;
            }
            const float dx = pending_delta_x_[index];
            const float dy = pending_delta_y_[index];
            if (dx * dx + dy * dy >= rotate_activation_threshold_squared_) {
                rotation_activated_[index] = true;
                input.rotate_begin = true;
                input.delta_x = dx;
                input.delta_y = dy;
                pending_delta_x_[index] = 0.0f;
                pending_delta_y_[index] = 0.0f;
            } else {
                // A press or sub-threshold movement is a click, not a turn.
                input.delta_x = 0.0f;
                input.delta_y = 0.0f;
            }
        }
    } else {
        rotation_origin_valid_[index] = false;
        rotation_activated_[index] = false;
        pending_delta_x_[index] = 0.0f;
        pending_delta_y_[index] = 0.0f;
    }

    previous_rotate_[index] = frame.rotate;
}

} // namespace gs3d::app
