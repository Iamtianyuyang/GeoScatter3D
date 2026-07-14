#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace gs3d::app {

// A reload latency is meaningful only with its selected/resident tile counts.
// Keep the workload next to the duration so reports cannot compare different
// active viewports as if they were equivalent cache events.
struct BenchmarkTileReloadSample {
    double seconds = 0.0;
    std::size_t selected_tile_count = 0;
    std::size_t required_tile_count = 0;
    std::size_t resident_tile_count = 0;
};

struct BenchmarkFrameSamples {
    std::vector<double> wall_frame_times_ms;
    std::vector<double> cpu_frame_times_ms;
    std::vector<double> gpu_frame_times_ms;
    std::vector<double> camera_update_ms;
    std::vector<double> lod_tile_select_ms;
    std::vector<double> cpu_cull_ms;
    std::vector<double> upload_record_ms;
    std::vector<double> draw_record_ms;
    std::vector<double> acquire_wait_ms;
    std::vector<double> frame_fence_wait_ms;
    std::vector<double> upload_fence_wait_ms;
    std::vector<BenchmarkTileReloadSample> tile_reload_samples;

    void reserve_frames(std::size_t frame_count);
};

struct BenchmarkFrameTiming {
    double wall_frame_ms = 0.0;
    double cpu_frame_ms = 0.0;
    double camera_update_ms = 0.0;
    double lod_tile_select_ms = 0.0;
    double cpu_cull_ms = 0.0;
    double upload_record_ms = 0.0;
    double draw_record_ms = 0.0;
    double acquire_wait_ms = 0.0;
    double frame_fence_wait_ms = 0.0;
    double upload_fence_wait_ms = 0.0;
};

/*
 * Main-thread benchmark lifecycle.  It deliberately does not know about a
 * window, renderer, or camera: ViewerApp supplies one complete timing sample
 * per frame and keeps ownership of the scripted pick requests.
 */
class BenchmarkSession {
public:
    static constexpr std::uint32_t kPickWarmupFrames = 80;

    BenchmarkSession(
        bool enabled,
        std::uint32_t requested_frame_count,
        std::uint32_t frames_in_flight
    );

    void configure_pick_script(std::size_t query_count);

    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] bool should_continue() const noexcept;
    [[nodiscard]] bool should_orbit() const noexcept;
    [[nodiscard]] bool should_force_tile_reload() const noexcept;
    [[nodiscard]] std::uint32_t frame_index() const noexcept;
    [[nodiscard]] std::uint64_t app_frame_index() const noexcept;
    [[nodiscard]] const BenchmarkFrameSamples& samples() const noexcept;
    [[nodiscard]] BenchmarkFrameSamples& samples() noexcept;

    void record_frame(
        const BenchmarkFrameTiming& timing,
        std::optional<double> gpu_frame_ms
    );

private:
    bool enabled_ = false;
    std::uint32_t requested_frame_count_ = 0;
    std::uint32_t frames_in_flight_ = 0;
    std::uint32_t target_frame_count_ = 0;
    std::uint32_t orbit_frame_count_ = 0;
    std::uint32_t frame_index_ = 0;
    std::uint64_t app_frame_index_ = 0;
    BenchmarkFrameSamples samples_;
};

} // namespace gs3d::app
