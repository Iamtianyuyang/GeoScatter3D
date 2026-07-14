#include "app/BenchmarkSession.hpp"

namespace gs3d::app {

void BenchmarkFrameSamples::reserve_frames(const std::size_t frame_count)
{
    wall_frame_times_ms.reserve(frame_count);
    cpu_frame_times_ms.reserve(frame_count);
    gpu_frame_times_ms.reserve(frame_count);
    camera_update_ms.reserve(frame_count);
    lod_tile_select_ms.reserve(frame_count);
    cpu_cull_ms.reserve(frame_count);
    upload_record_ms.reserve(frame_count);
    draw_record_ms.reserve(frame_count);
    acquire_wait_ms.reserve(frame_count);
    frame_fence_wait_ms.reserve(frame_count);
    upload_fence_wait_ms.reserve(frame_count);
    tile_reload_samples.reserve(frame_count);
}

BenchmarkSession::BenchmarkSession(
    const bool enabled,
    const std::uint32_t requested_frame_count,
    const std::uint32_t frames_in_flight
)
    : enabled_(enabled)
    , requested_frame_count_(requested_frame_count)
    , frames_in_flight_(frames_in_flight)
    , target_frame_count_(requested_frame_count)
    // Keep two thirds of a normal run stationary.  The scripted camera move
    // exists to trigger a tile reload; it must not dominate the frame-time
    // percentile intended to describe the settled rendering path.
    , orbit_frame_count_(requested_frame_count / 3)
{
    if (enabled_) {
        samples_.reserve_frames(requested_frame_count_);
    }
}

void BenchmarkSession::configure_pick_script(const std::size_t query_count)
{
    if (!enabled_) {
        return;
    }
    target_frame_count_ = static_cast<std::uint32_t>(
        kPickWarmupFrames + query_count + frames_in_flight_ + 1
    );
    samples_.reserve_frames(target_frame_count_);
}

bool BenchmarkSession::enabled() const noexcept
{
    return enabled_;
}

bool BenchmarkSession::should_continue() const noexcept
{
    return !enabled_ || frame_index_ < target_frame_count_;
}

bool BenchmarkSession::should_orbit() const noexcept
{
    return enabled_ && frame_index_ < orbit_frame_count_;
}

bool BenchmarkSession::should_force_tile_reload() const noexcept
{
    // Fire once at the first settled frame, after the scripted camera motion
    // has produced a realistic selection but before the steady-state sample.
    return enabled_ && frame_index_ == orbit_frame_count_;
}

std::uint32_t BenchmarkSession::frame_index() const noexcept
{
    return frame_index_;
}

std::uint64_t BenchmarkSession::app_frame_index() const noexcept
{
    return app_frame_index_;
}

const BenchmarkFrameSamples& BenchmarkSession::samples() const noexcept
{
    return samples_;
}

BenchmarkFrameSamples& BenchmarkSession::samples() noexcept
{
    return samples_;
}

void BenchmarkSession::record_frame(
    const BenchmarkFrameTiming& timing,
    const std::optional<double> gpu_frame_ms
) {
    if (enabled_) {
        samples_.wall_frame_times_ms.push_back(timing.wall_frame_ms);
        samples_.cpu_frame_times_ms.push_back(timing.cpu_frame_ms);
        samples_.camera_update_ms.push_back(timing.camera_update_ms);
        samples_.lod_tile_select_ms.push_back(timing.lod_tile_select_ms);
        samples_.cpu_cull_ms.push_back(timing.cpu_cull_ms);
        samples_.upload_record_ms.push_back(timing.upload_record_ms);
        samples_.draw_record_ms.push_back(timing.draw_record_ms);
        samples_.acquire_wait_ms.push_back(timing.acquire_wait_ms);
        samples_.frame_fence_wait_ms.push_back(timing.frame_fence_wait_ms);
        samples_.upload_fence_wait_ms.push_back(timing.upload_fence_wait_ms);
        if (gpu_frame_ms) {
            samples_.gpu_frame_times_ms.push_back(*gpu_frame_ms);
        }
        ++frame_index_;
    }
    ++app_frame_index_;
}

} // namespace gs3d::app
