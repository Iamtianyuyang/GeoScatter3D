#pragma once

#include <vector>

namespace gs3d::render {

/*
 * Computes "nice" axis tick values within [min_value, max_value] — the
 * same algorithm family as D3.js ticks() / matplotlib's MaxNLocator
 * (round step to 1/2/5 * 10^n), so labels read as round numbers instead
 * of arbitrary fractions. `target_tick_count` is a target, not a hard
 * guarantee — the actual count depends on how evenly the range divides.
 *
 * Returns an empty vector if min_value >= max_value or target_tick_count
 * <= 0.
 */
[[nodiscard]]
std::vector<float> compute_axis_ticks(
    float min_value,
    float max_value,
    int target_tick_count = 5
) noexcept;

} // namespace gs3d::render
