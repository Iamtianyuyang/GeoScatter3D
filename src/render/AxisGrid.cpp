#include "render/AxisGrid.hpp"

#include <cmath>

namespace gs3d::render {

namespace {

// Rounds `raw_step` up to the nearest "nice" value: 1, 2, 5, 10, 20, 50 ...
// times a power of ten. Same family as the existing nice_scale_distance()
// in ViewerApp.cpp (used for the scale bar), generalized for tick steps.
float nice_step(float raw_step) noexcept {
    if (raw_step <= 0.0f) {
        return 1.0f;
    }

    const float magnitude =
        std::pow(10.0f, std::floor(std::log10(static_cast<double>(raw_step))));
    const float normalized = raw_step / magnitude;

    if (normalized <= 1.0f) return magnitude;
    if (normalized <= 2.0f) return 2.0f * magnitude;
    if (normalized <= 5.0f) return 5.0f * magnitude;
    return 10.0f * magnitude;
}

} // namespace

std::vector<float> compute_axis_ticks(
    float min_value,
    float max_value,
    int target_tick_count
) noexcept {
    if (min_value >= max_value || target_tick_count <= 0) {
        return {};
    }

    const float range = max_value - min_value;
    const float step =
        nice_step(range / static_cast<float>(target_tick_count));

    std::vector<float> ticks;
    const float first_tick = std::ceil(min_value / step) * step;

    // Cap iterations defensively — step is always > 0 here, but guards
    // against float-precision edge cases turning this into a long loop.
    constexpr int kMaxTicks = 1000;
    for (float tick = first_tick;
         tick <= max_value + step * 1.0e-4f && static_cast<int>(ticks.size()) < kMaxTicks;
         tick += step) {
        ticks.push_back(tick);
    }

    return ticks;
}

} // namespace gs3d::render
