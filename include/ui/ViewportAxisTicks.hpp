#pragma once

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

} // namespace gs3d::ui
