#include "app/ViewerFrameClock.hpp"

#include <algorithm>
#include <cmath>

namespace gs3d::app {

ViewerFrameClock::ViewerFrameClock(
    const std::chrono::steady_clock::time_point initial_time
) noexcept
    : previous_time_(initial_time)
{
}

ViewerFrameClockTick ViewerFrameClock::tick(
    const std::chrono::steady_clock::time_point current_time
) noexcept
{
    const double delta_seconds = std::chrono::duration<double>(
        current_time - previous_time_
    ).count();
    previous_time_ = current_time;

    if (delta_seconds > 0.0 && std::isfinite(delta_seconds)) {
        const float frame_fps = static_cast<float>(1.0 / delta_seconds);
        smoothed_fps_ = smoothed_fps_ > 0.0f
            ? smoothed_fps_ * 0.9f + frame_fps * 0.1f
            : frame_fps;
    }

    return {delta_seconds, smoothed_fps_};
}

double ViewerFrameClock::estimate_render_work_milliseconds(
    const double elapsed_seconds,
    const double acquire_wait_milliseconds
) noexcept
{
    if (!std::isfinite(elapsed_seconds) || elapsed_seconds <= 0.0) {
        return 0.0;
    }

    const double elapsed_milliseconds = elapsed_seconds * 1000.0;
    const double acquire_wait =
        std::isfinite(acquire_wait_milliseconds) &&
        acquire_wait_milliseconds > 0.0
            ? acquire_wait_milliseconds
            : 0.0;
    return std::max(0.0, elapsed_milliseconds - acquire_wait);
}

} // namespace gs3d::app
