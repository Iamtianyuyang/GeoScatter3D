#include "ui/ViewportAxisTicks.hpp"

#include <algorithm>
#include <cmath>

namespace gs3d::ui {

std::vector<float> compute_minor_axis_ticks(
    const float major_step,
    const float first_major,
    const float range_min,
    const float range_max
) {
    std::vector<float> minors;
    if (major_step <= 0.0f || !std::isfinite(major_step)) {
        return minors;
    }
    constexpr int kMinorPerMajor = 4;
    const float minor_step =
        major_step / static_cast<float>(kMinorPerMajor);
    if (minor_step <= 0.0f || !std::isfinite(minor_step)) {
        return minors;
    }

    constexpr int kMaxMinorsPerSide = 200;
    const double minor_step_d = static_cast<double>(minor_step);
    const double first_major_d = static_cast<double>(first_major);
    const double range_min_d = static_cast<double>(range_min);
    const double range_max_d = static_cast<double>(range_max);
    const double end_threshold = minor_step_d * 0.5;

    int left_count = static_cast<int>(
        std::floor((first_major_d - range_min_d) / minor_step_d)
    );
    left_count = std::clamp(left_count, 0, kMaxMinorsPerSide);
    minors.reserve(static_cast<std::size_t>(
        left_count + kMaxMinorsPerSide
    ));
    for (int index = 1; index <= left_count; ++index) {
        const double tick = first_major_d -
            static_cast<double>(index) * minor_step_d;
        if (tick <= range_min_d + end_threshold) {
            break;
        }
        minors.push_back(static_cast<float>(tick));
    }

    int right_count = static_cast<int>(
        std::floor((range_max_d - first_major_d) / minor_step_d)
    );
    right_count = std::clamp(right_count, 0, kMaxMinorsPerSide);
    for (int index = 1; index <= right_count; ++index) {
        const double tick = first_major_d +
            static_cast<double>(index) * minor_step_d;
        if (tick >= range_max_d - end_threshold) {
            break;
        }
        minors.push_back(static_cast<float>(tick));
    }

    constexpr std::size_t kMaxMinorCount =
        static_cast<std::size_t>(2 * kMaxMinorsPerSide);
    if (minors.size() > kMaxMinorCount) {
        minors.resize(kMaxMinorCount);
    }
    return minors;
}

} // namespace gs3d::ui
