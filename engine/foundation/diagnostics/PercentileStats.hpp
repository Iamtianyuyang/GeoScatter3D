#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace gs3d::util {

/*
 * Linear-interpolation percentile (same convention as numpy.percentile's
 * default "linear" method). `p` is in [0, 100]. Returns 0.0 for an empty
 * input — callers must check size() themselves if that distinction matters.
 */
[[nodiscard]]
inline double percentile(std::vector<double> samples, double p) noexcept {
    if (samples.empty()) {
        return 0.0;
    }

    std::sort(samples.begin(), samples.end());

    if (samples.size() == 1) {
        return samples.front();
    }

    const double rank =
        (p / 100.0) * static_cast<double>(samples.size() - 1);
    const auto lower_index = static_cast<std::size_t>(rank);
    const auto upper_index =
        std::min(lower_index + 1, samples.size() - 1);
    const double frac = rank - static_cast<double>(lower_index);

    return samples[lower_index] +
        frac * (samples[upper_index] - samples[lower_index]);
}

} // namespace gs3d::util
