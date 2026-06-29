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
    if (step <= 0.0f || !std::isfinite(step)) {
        return ticks;
    }

    // Compute in double to avoid the classic
    // "big absolute value + tiny step" stall: when the cursor is, say,
    // -14544 and the nice step is ~0.002, float's unit-roundoff at
    // that magnitude is already >= step, so a `tick += step` loop
    // never advances and never terminates. Driving iteration by an
    // integer index in double space and projecting back to float is
    // exact for any (first_tick, step) pair produced by nice_step().
    //
    // Cap iteration count defensively as a hard safety net against
    // pathological inputs (e.g. min_value == max_value-ish with a
    // tiny step); step > 0 is enforced above so the loop body must
    // terminate eventually even without the cap.
    constexpr int kMaxTicks = 200;
    const double step_d = static_cast<double>(step);
    const double first_tick_d =
        std::ceil(static_cast<double>(min_value) / step_d) * step_d;
    const double max_value_d = static_cast<double>(max_value);

    int count = static_cast<int>(
        std::ceil((max_value_d - first_tick_d) / step_d));
    if (count < 0) {
        count = 0;
    }
    if (count > kMaxTicks) {
        count = kMaxTicks;
    }

    ticks.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const double v = first_tick_d + static_cast<double>(i) * step_d;
        ticks.push_back(static_cast<float>(v));
    }

    return ticks;
}

} // namespace gs3d::render
