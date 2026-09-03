#include "app/ViewerCameraFrameSystem.hpp"

#include "app/ViewportCameraSystem.hpp"
#include "camera/CameraHub.hpp"
#include "render/ViewportManager.hpp"

namespace gs3d::app {

InteractionDebouncer::InteractionDebouncer(
    const std::chrono::steady_clock::duration retention
) noexcept
    : retention_(retention)
{
}

bool InteractionDebouncer::update(
    const bool interacting,
    const std::chrono::steady_clock::time_point current_time
) noexcept {
    if (interacting) {
        interacting_until_ = current_time + retention_;
        return true;
    }
    return current_time < interacting_until_;
}

ViewerCameraFrameSystem::ViewerCameraFrameSystem(
    const std::chrono::steady_clock::duration interaction_retention
) noexcept
    : interaction_debouncer_(interaction_retention)
{
}

ViewerCameraFrameResult ViewerCameraFrameSystem::update(
    const ViewerCameraFrameContext& context
) {
    ViewerCameraFrameResult result{
        .streaming_viewport_index = context.streaming_viewport_index
    };

    for (const auto& frame : context.viewport_frames) {
        if (!context.viewport_cameras.contains(frame.index)) {
            continue;
        }

        if ((frame.hovered || frame.active) &&
            result.streaming_viewport_index != frame.index) {
            result.streaming_viewport_index = frame.index;
            result.streaming_viewport_changed = true;
        }

        const auto camera_update = context.viewport_cameras.update(
            frame,
            context.viewport_manager.camera(frame.index)
        );
        result.interacting = result.interacting || camera_update.interacting;
        if (camera_update.camera_changed) {
            result.camera_changed = true;
            result.streaming_viewport_index = frame.index;
            context.camera_hub.propagate(
                frame.index,
                [&](int src, int dst) {
                    context.viewport_cameras.controller(dst)
                        .copy_pivot_from(
                            context.viewport_cameras.controller(src));
                });
        }
    }

    if (context.benchmark_orbit) {
        auto& camera = context.viewport_manager.camera(
            result.streaming_viewport_index
        );
        camera.orbit(0.01f, 0.0f);
        camera.zoom(0.999f);
        context.camera_hub.propagate(
            result.streaming_viewport_index,
            [&](int src, int dst) {
                context.viewport_cameras.controller(dst)
                    .copy_pivot_from(
                        context.viewport_cameras.controller(src));
            });
        result.interacting = true;
        result.camera_changed = true;
    }

    result.interacting = interaction_debouncer_.update(
        result.interacting,
        context.current_time
    );
    return result;
}

} // namespace gs3d::app
