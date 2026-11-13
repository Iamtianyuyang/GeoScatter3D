#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace gs3d::abi {

/// Thrown by a core when a task should stop because the user cancelled it.
class TaskCancelled : public std::runtime_error {
public:
    TaskCancelled() : std::runtime_error("task cancelled") {}
};

/// Told to the work function so it can report progress and notice cancellation.
class ProgressSink {
public:
    virtual ~ProgressSink() = default;
    /// Monotonic 0..1 value; cheap, called from the working thread.
    virtual void report(float progress) = 0;
    virtual bool cancelled() const = 0;
};

/// Everything the UI shows that must be readable without waiting for the GPU.
struct Snapshot {
    std::uint64_t frame_sequence = 0;
    std::uint64_t point_count = 0;
    std::uint32_t viewport_w = 0;
    std::uint32_t viewport_h = 0;
    double camera_pos[3] = {0.0, 0.0, 0.0};
    double camera_target[3] = {0.0, 0.0, 0.0};
    double camera_up[3] = {0.0, 0.0, 1.0};
    double last_render_ms = 0.0;
};

/// The engine operations the ABI exposes. Implementations own all GPU state.
class Core {
public:
    virtual ~Core() = default;

    virtual void load_dataset(const std::string& path, ProgressSink& progress) = 0;
    virtual void export_points(const std::string& path, const std::string& format,
                               ProgressSink& progress) = 0;
    virtual void preprocess(const std::string& json, ProgressSink& progress) = 0;

    virtual void camera(const std::string& json) = 0;
    virtual void render_options(const std::string& json) = 0;
    virtual void pick(const std::string& json) = 0;
    virtual void set_viewport(const std::string& json) = 0;

    [[nodiscard]] virtual Snapshot snapshot() const = 0;
};

} // namespace gs3d::abi
