#include "app/ViewportResizeScheduler.hpp"

#include <algorithm>

namespace gs3d::app {

ViewportResizeScheduler::ViewportResizeScheduler(double quiet_seconds)
    : quiet_seconds_(std::max(0.0, quiet_seconds))
{
}

void ViewportResizeScheduler::observe(
    int index,
    std::uint32_t width,
    std::uint32_t height,
    double now_seconds
) {
    if (index < 0 || width == 0 || height == 0) {
        return;
    }

    const auto entry_index = static_cast<std::size_t>(index);
    if (entry_index >= entries_.size()) {
        entries_.resize(entry_index + 1);
    }

    auto& entry = entries_[entry_index];
    if (entry.pending &&
        entry.desired_width == width &&
        entry.desired_height == height) {
        return;
    }
    if (!entry.pending &&
        entry.applied_width == width &&
        entry.applied_height == height) {
        return;
    }

    entry.desired_width = width;
    entry.desired_height = height;
    entry.changed_at_seconds = now_seconds;
    entry.pending = true;
}

std::vector<ScheduledViewportResize>
ViewportResizeScheduler::take_ready(double now_seconds)
{
    std::vector<ScheduledViewportResize> ready;
    ready.reserve(entries_.size());

    for (std::size_t i = 0; i < entries_.size(); ++i) {
        auto& entry = entries_[i];
        if (!entry.pending ||
            now_seconds - entry.changed_at_seconds < quiet_seconds_) {
            continue;
        }

        ready.push_back({
            static_cast<int>(i),
            entry.desired_width,
            entry.desired_height
        });
        entry.applied_width = entry.desired_width;
        entry.applied_height = entry.desired_height;
        entry.pending = false;
    }

    return ready;
}

void ViewportResizeScheduler::reset() noexcept
{
    entries_.clear();
}

} // namespace gs3d::app
