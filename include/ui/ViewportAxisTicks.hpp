#pragma once

#include <cstddef>
#include <vector>

namespace gs3d::ui {

// Builds the unlabelled ticks between major axis ticks. Double precision and
// indexed stepping keep large-coordinate, tiny-step views finite and bounded.
[[nodiscard]] std::vector<float> compute_minor_axis_ticks(
    float major_step,
    float first_major,
    float range_min,
    float range_max
);

// Labels and crosshair values must use the same precision as their axis
// major-step; otherwise a zoomed view presents contradictory coordinates.
[[nodiscard]] int axis_label_precision(float major_step) noexcept;

void format_axis_tick_label(
    char* buffer,
    std::size_t buffer_size,
    float tick,
    double origin_offset,
    float major_step
) noexcept;

} // namespace gs3d::ui
