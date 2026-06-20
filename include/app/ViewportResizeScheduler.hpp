#pragma once

#include <cstdint>
#include <vector>

namespace gs3d::app {

struct ScheduledViewportResize {
    int index = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

class ViewportResizeScheduler {
public:
    explicit ViewportResizeScheduler(double quiet_seconds = 0.15);

    void observe(
        int index,
        std::uint32_t width,
        std::uint32_t height,
        double now_seconds
    );

    [[nodiscard]]
    std::vector<ScheduledViewportResize> take_ready(double now_seconds);

    void reset() noexcept;

private:
    struct Entry {
        std::uint32_t desired_width = 0;
        std::uint32_t desired_height = 0;
        std::uint32_t applied_width = 0;
        std::uint32_t applied_height = 0;
        double changed_at_seconds = 0.0;
        bool pending = false;
    };

    double quiet_seconds_ = 0.15;
    std::vector<Entry> entries_;
};

} // namespace gs3d::app
