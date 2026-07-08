#include "app/ViewerAppInternal.hpp"

#include "app/TilePointCache.hpp"
#include "core/PointData.hpp"
#include "data/Gs3dDataset.hpp"
#include "data/Gs3dTileReader.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gs3d::app {

namespace {

[[nodiscard]]
bool same_point_exact(
    const gs3d::data::Gs3dPoint& lhs,
    const gs3d::data::Gs3dPoint& rhs
) noexcept {
    return lhs.x == rhs.x &&
           lhs.y == rhs.y &&
           lhs.z == rhs.z &&
           lhs.value == rhs.value;
}

[[nodiscard]]
std::uint32_t point_tile_coord_runtime(
    float value,
    float origin,
    float tile_size,
    std::uint32_t grid_count
) {
    if (grid_count == 0 || tile_size <= 0.0f) {
        throw std::runtime_error(
            "ViewerApp: invalid tile grid configuration for runtime ids"
        );
    }

    const auto raw = static_cast<std::int64_t>(
        std::floor((value - origin) / tile_size)
    );
    if (raw < 0) {
        return 0;
    }

    const auto upper =
        static_cast<std::int64_t>(grid_count - 1);
    if (raw > upper) {
        return grid_count - 1;
    }

    return static_cast<std::uint32_t>(raw);
}

[[nodiscard]]
gs3d::data::Gs3dPoint to_gs3d_point(
    const gs3d::core::PointRecord& point
) noexcept {
    return {
        point.x,
        point.y,
        point.z,
        point.value
    };
}

} // namespace

[[nodiscard]]
std::vector<std::uint32_t> make_runtime_point_ids(
    std::uint64_t point_count
) {
    if (point_count >
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max() - 1)) {
        throw std::runtime_error(
            "ViewerApp: point_count exceeds runtime point_id range"
        );
    }

    std::vector<std::uint32_t> point_ids(
        static_cast<std::size_t>(point_count)
    );
    for (std::uint64_t i = 0; i < point_count; ++i) {
        point_ids[static_cast<std::size_t>(i)] =
            static_cast<std::uint32_t>(i + 1);
    }
    return point_ids;
}

[[nodiscard]]
std::vector<std::uint32_t> map_subsequence_point_ids(
    const std::vector<gs3d::data::Gs3dPoint>& source_points,
    const std::vector<std::uint32_t>& source_point_ids,
    const std::vector<gs3d::data::Gs3dPoint>& subset_points,
    const char* label
) {
    if (source_points.size() != source_point_ids.size()) {
        throw std::runtime_error(
            "ViewerApp: source point/id array size mismatch"
        );
    }

    std::vector<std::uint32_t> subset_ids;
    subset_ids.reserve(subset_points.size());

    std::size_t source_index = 0;
    for (const auto& point : subset_points) {
        while (source_index < source_points.size() &&
               !same_point_exact(source_points[source_index], point)) {
            ++source_index;
        }

        if (source_index >= source_points.size()) {
            throw std::runtime_error(
                std::string("ViewerApp: failed to map runtime point_id for ") +
                label
            );
        }

        subset_ids.push_back(source_point_ids[source_index]);
        ++source_index;
    }

    return subset_ids;
}

[[nodiscard]]
std::unordered_map<std::uint64_t, std::vector<std::uint32_t>>
build_runtime_tile_point_ids(
    const gs3d::data::Gs3dDataset& dataset,
    const gs3d::data::Gs3dTileReader& tile_reader,
    const std::vector<std::uint32_t>& source_point_ids
) {
    if (dataset.points().size() != source_point_ids.size()) {
        throw std::runtime_error(
            "ViewerApp: source point/id array size mismatch for tiles"
        );
    }

    const auto& header = tile_reader.index_header();
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> tile_ids;
    std::unordered_map<std::uint64_t, std::uint64_t> grid_to_tile_id;
    tile_ids.reserve(tile_reader.records().size());
    grid_to_tile_id.reserve(tile_reader.records().size());
    for (const auto& record : tile_reader.records()) {
        tile_ids.emplace(record.tile_id, std::vector<std::uint32_t>{});
        tile_ids[record.tile_id].reserve(
            static_cast<std::size_t>(record.point_count)
        );
        const auto grid_key =
            static_cast<std::uint64_t>(record.tile_y) *
                static_cast<std::uint64_t>(header.grid_count_x) +
            static_cast<std::uint64_t>(record.tile_x);
        grid_to_tile_id.emplace(grid_key, record.tile_id);
    }

    for (std::size_t i = 0; i < dataset.points().size(); ++i) {
        const auto& point = dataset.points()[i];
        const auto tile_x = point_tile_coord_runtime(
            point.x,
            header.grid_origin_x,
            header.tile_size_x,
            header.grid_count_x
        );
        const auto tile_y = point_tile_coord_runtime(
            point.y,
            header.grid_origin_y,
            header.tile_size_y,
            header.grid_count_y
        );
        const auto tile_id =
            static_cast<std::uint64_t>(tile_y) *
                static_cast<std::uint64_t>(header.grid_count_x) +
            static_cast<std::uint64_t>(tile_x);
        const auto grid_found = grid_to_tile_id.find(tile_id);
        if (grid_found == grid_to_tile_id.end()) {
            continue;
        }
        auto found = tile_ids.find(grid_found->second);
        if (found == tile_ids.end()) {
            continue;
        }
        found->second.push_back(source_point_ids[i]);
    }

    for (const auto& record : tile_reader.records()) {
        const auto found = tile_ids.find(record.tile_id);
        if (found == tile_ids.end() ||
            found->second.size() !=
                static_cast<std::size_t>(record.point_count)) {
            throw std::runtime_error(
                "ViewerApp: runtime tile point_id reconstruction failed"
            );
        }
    }

    return tile_ids;
}

void register_runtime_point_lookup(
    const std::vector<gs3d::data::Gs3dPoint>& points,
    const std::vector<std::uint32_t>& point_ids,
    std::vector<gs3d::data::Gs3dPoint>& points_by_id,
    std::vector<std::uint8_t>& points_valid_by_id
) {
    if (points.size() != point_ids.size()) {
        throw std::runtime_error(
            "ViewerApp: runtime point lookup size mismatch"
        );
    }

    for (std::size_t i = 0; i < points.size(); ++i) {
        const auto point_id = point_ids[i];
        if (point_id == 0 ||
            point_id >= points_by_id.size() ||
            point_id >= points_valid_by_id.size()) {
            throw std::runtime_error(
                "ViewerApp: runtime point lookup id out of range"
            );
        }

        points_by_id[point_id] = points[i];
        points_valid_by_id[point_id] = 1;
    }
}

void register_runtime_tile_point_lookup(
    const std::vector<std::pair<std::uint64_t, SharedTilePoints>>& tiles,
    std::vector<gs3d::data::Gs3dPoint>& points_by_id,
    std::vector<std::uint8_t>& points_valid_by_id
) {
    for (const auto& [tile_id, points] : tiles) {
        static_cast<void>(tile_id);
        if (!points) {
            continue;
        }
        register_runtime_point_lookup(
            points->points,
            points->point_ids,
            points_by_id,
            points_valid_by_id
        );
    }
}

[[nodiscard]]
std::optional<gs3d::data::Gs3dPoint> find_point_by_id_in_views(
    const std::vector<gs3d::core::PointDataView>& candidate_point_sets,
    std::uint32_t point_id
) noexcept {
    if (point_id == 0) {
        return std::nullopt;
    }

    for (const auto& points : candidate_point_sets) {
        if (!points.valid() || points.empty() || !points.has_point_ids()) {
            continue;
        }

        for (std::uint64_t i = 0; i < points.point_count; ++i) {
            if (points.point_id_at(i) != point_id) {
                continue;
            }
            return to_gs3d_point(points.point_at(i));
        }
    }

    return std::nullopt;
}

} // namespace gs3d::app
