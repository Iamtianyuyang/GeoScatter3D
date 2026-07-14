#pragma once

#include "app/UiActions.hpp"

#include <chrono>
#include <vector>

namespace gs3d::app { class ViewportCameraSystem; }
namespace gs3d::camera { class CameraHub; }
namespace gs3d::render { class ViewportManager; }

namespace gs3d::app {

class InteractionDebouncer {
public:
    explicit InteractionDebouncer(
        std::chrono::steady_clock::duration retention
    ) noexcept;

    [[nodiscard]] bool update(
        bool interacting,
        std::chrono::steady_clock::time_point current_time
    ) noexcept;

private:
    std::chrono::steady_clock::duration retention_{};
    std::chrono::steady_clock::time_point interacting_until_{};
};

struct ViewerCameraFrameContext {
    const std::vector<ViewportFrameCmd>& viewport_frames;
    ViewportCameraSystem& viewport_cameras;
    gs3d::render::ViewportManager& viewport_manager;
    gs3d::camera::CameraHub& camera_hub;
    int streaming_viewport_index = 0;
    bool benchmark_orbit = false;
    std::chrono::steady_clock::time_point current_time{};
};

struct ViewerCameraFrameResult {
    int streaming_viewport_index = 0;
    bool streaming_viewport_changed = false;
    bool interacting = false;
    bool camera_changed = false;
};

// Orchestrates per-viewport gestures, linked-camera propagation, benchmark
// orbit motion, and the interaction debounce that prevents tile/LOD flicker.
class ViewerCameraFrameSystem {
public:
    explicit ViewerCameraFrameSystem(
        std::chrono::steady_clock::duration interaction_retention
    ) noexcept;

    [[nodiscard]] ViewerCameraFrameResult update(
        const ViewerCameraFrameContext& context
    );

private:
    InteractionDebouncer interaction_debouncer_;
};

} // namespace gs3d::app
