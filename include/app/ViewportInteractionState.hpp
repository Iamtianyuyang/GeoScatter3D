#pragma once

#include "app/UiActions.hpp"
#include "camera/CameraController.hpp"

#include <cstddef>
#include <vector>

namespace gs3d::app {

/*
 * Per-viewport gesture history for camera rotation.
 *
 * A rotate press is intentionally inert until the cursor has moved far
 * enough to be a drag. The gated deltas are accumulated and replayed when
 * the gesture activates, so click protection does not create cursor lag.
 * The state is main-thread only: UI frames are produced and consumed by the
 * ViewerApp frame loop on that thread.
 */
class ViewportInteractionState {
public:
    explicit ViewportInteractionState(
        std::size_t viewport_count,
        float rotate_activation_threshold_px = 5.0f
    );

    [[nodiscard]]
    std::size_t viewport_count() const noexcept;

    // Adds the rotation activation gate to an otherwise populated input.
    // Frames outside the configured viewport range are ignored.
    void apply_rotation_gate(
        const ViewportFrameCmd& frame,
        gs3d::camera::CameraInput& input
    ) noexcept;

private:
    float rotate_activation_threshold_squared_ = 25.0f;
    std::vector<bool> previous_rotate_;
    std::vector<bool> rotation_origin_valid_;
    std::vector<bool> rotation_activated_;
    std::vector<float> pending_delta_x_;
    std::vector<float> pending_delta_y_;
};

} // namespace gs3d::app
