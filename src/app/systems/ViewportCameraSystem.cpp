#include "app/ViewportCameraSystem.hpp"

#include <stdexcept>

namespace gs3d::app {

ViewportCameraSystem::ViewportCameraSystem(
    const gs3d::camera::CameraControllerConfig& controller_config,
    const gs3d::camera::CameraBounds& bounds,
    const std::size_t viewport_count
)
    : interaction_(viewport_count)
{
    controllers_.reserve(viewport_count);
    for (std::size_t index = 0; index < viewport_count; ++index) {
        controllers_.emplace_back(controller_config);
        controllers_.back().set_bounds(bounds);
    }
}

bool ViewportCameraSystem::contains(const int viewport_index) const noexcept
{
    return viewport_index >= 0 &&
           static_cast<std::size_t>(viewport_index) < controllers_.size();
}

std::size_t ViewportCameraSystem::viewport_count() const noexcept
{
    return controllers_.size();
}

ViewportCameraUpdate ViewportCameraSystem::update(
    const ViewportFrameCmd& frame,
    gs3d::camera::Camera& camera
) noexcept
{
    if (!contains(frame.index)) {
        return {};
    }
    gs3d::camera::CameraInput input{
        .viewport_width = frame.width,
        .viewport_height = frame.height,
        .delta_x = frame.mouse_delta_x,
        .delta_y = frame.mouse_delta_y,
        .scroll_y = frame.mouse_wheel,
        .mouse_x = frame.mouse_local_x,
        .mouse_y = frame.mouse_local_y,
        .mouse_position_valid = frame.mouse_on_image,
        .rotate = frame.rotate,
        .pan = frame.pan
    };
    interaction_.apply_rotation_gate(frame, input);
    return {
        .interacting = input.interacting(),
        .camera_changed = controllers_[static_cast<std::size_t>(frame.index)]
            .update(camera, input)
    };
}

gs3d::camera::CameraController& ViewportCameraSystem::controller(
    const int viewport_index
)
{
    if (!contains(viewport_index)) {
        throw std::out_of_range("viewport camera controller index out of range");
    }
    return controllers_[static_cast<std::size_t>(viewport_index)];
}

std::vector<gs3d::camera::CameraController>&
ViewportCameraSystem::controllers() noexcept
{
    return controllers_;
}

} // namespace gs3d::app
