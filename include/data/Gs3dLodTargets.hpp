#pragma once

#include <cstdint>
#include <vector>

namespace gs3d::data {

/*
 * Resolves Gs3dLodBuildConfig::target_point_counts from either explicit
 * absolute counts or ratios of the source dataset's point count. Ratios
 * let the LOD ladder scale automatically with dataset size instead of
 * needing a manual re-tune every time (e.g. 33M-point test data vs a
 * 1e8-point dataset would otherwise need different absolute targets).
 *
 * `target_point_ratios` takes priority when non-empty. Each ratio is
 * clamped to >= 0 and the resulting count to >= 1 (a target of 0 points
 * is not a valid LOD level).
 */
[[nodiscard]]
inline std::vector<std::uint64_t> resolve_lod_target_point_counts(
    std::uint64_t source_point_count,
    const std::vector<double>& target_point_ratios,
    const std::vector<std::uint64_t>& explicit_target_point_counts
) {
    if (target_point_ratios.empty()) {
        return explicit_target_point_counts;
    }

    std::vector<std::uint64_t> resolved;
    resolved.reserve(target_point_ratios.size());

    for (const double ratio : target_point_ratios) {
        const double clamped_ratio = ratio < 0.0 ? 0.0 : ratio;
        const double raw_count =
            clamped_ratio * static_cast<double>(source_point_count);
        resolved.push_back(
            raw_count < 1.0 ? 1ull : static_cast<std::uint64_t>(raw_count)
        );
    }

    return resolved;
}

} // namespace gs3d::data
