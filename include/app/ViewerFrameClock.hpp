#pragma once

#include <chrono>

namespace gs3d::app {

struct ViewerFrameClockTick {
    double delta_seconds = 0.0;
    float smoothed_fps = 0.0f;
};

// Owns the frame-to-frame timing state used by both telemetry and adaptive
// LOD. The injected clock origin makes its smoothing behavior unit-testable.
class ViewerFrameClock {
public:
    explicit ViewerFrameClock(
        std::chrono::steady_clock::time_point initial_time =
            std::chrono::steady_clock::now()
    ) noexcept;

    [[nodiscard]] ViewerFrameClockTick tick(
        std::chrono::steady_clock::time_point current_time
    ) noexcept;

    [[nodiscard]] static double estimate_render_work_milliseconds(
        double elapsed_seconds,
        double acquire_wait_milliseconds
    ) noexcept;

private:
    std::chrono::steady_clock::time_point previous_time_;
    float smoothed_fps_ = 0.0f;
};

} // namespace gs3d::app
