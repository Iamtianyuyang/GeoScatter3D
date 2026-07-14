#pragma once

#include "app/UiActions.hpp"
#include "app/ViewportInteractionState.hpp"
#include "camera/Camera.hpp"
#include "camera/CameraController.hpp"

#include <cstddef>
#include <vector>

namespace gs3d::app {

struct ViewportCameraUpdate {
    bool interacting = false;
    bool camera_changed = false;
};

// Main-thread owner for per-viewport camera controllers and gesture state.
class ViewportCameraSystem {
public:
    ViewportCameraSystem(
        const gs3d::camera::CameraControllerConfig& controller_config,
        const gs3d::camera::CameraBounds& bounds,
        std::size_t viewport_count
    );

    [[nodiscard]] bool contains(int viewport_index) const noexcept;
    [[nodiscard]] std::size_t viewport_count() const noexcept;
    [[nodiscard]] ViewportCameraUpdate update(
        const ViewportFrameCmd& frame,
        gs3d::camera::Camera& camera
    ) noexcept;
    [[nodiscard]] gs3d::camera::CameraController& controller(int viewport_index);
    [[nodiscard]] std::vector<gs3d::camera::CameraController>& controllers() noexcept;

private:
    ViewportInteractionState interaction_;
    std::vector<gs3d::camera::CameraController> controllers_;
};

} // namespace gs3d::app
