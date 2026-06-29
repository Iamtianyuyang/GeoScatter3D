#pragma once

#include "core/PointData.hpp"
#include "data/Gs3dDataset.hpp"
#include "data/Gs3dFormat.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gs3d::data {

[[nodiscard]]
inline gs3d::core::PointDataView make_point_data_view(
    const Gs3dPoint* points,
    std::uint64_t point_count,
    const std::uint32_t* point_ids = nullptr
) noexcept {
    auto view = gs3d::core::make_interleaved_point_data_view(
        points,
        point_count,
        sizeof(Gs3dPoint),
        offsetof(Gs3dPoint, x),
        offsetof(Gs3dPoint, y),
        offsetof(Gs3dPoint, z),
        offsetof(Gs3dPoint, value)
    );
    view.point_id = point_ids;
    return view;
}

[[nodiscard]]
inline gs3d::core::PointDataView make_point_data_view(
    const std::vector<Gs3dPoint>& points,
    const std::uint32_t* point_ids = nullptr
) noexcept {
    return make_point_data_view(
        points.data(),
        static_cast<std::uint64_t>(points.size()),
        point_ids
    );
}

[[nodiscard]]
inline gs3d::core::PointDataView make_point_data_view(
    const Gs3dDataset& dataset,
    const std::uint32_t* point_ids = nullptr
) noexcept {
    return make_point_data_view(
        dataset.point_data(),
        dataset.point_count(),
        point_ids
    );
}

[[nodiscard]]
inline gs3d::core::PointBuffer make_point_buffer(
    const Gs3dPoint* points,
    std::uint64_t point_count,
    const std::uint32_t* point_ids = nullptr
) {
    gs3d::core::PointBuffer buffer;
    if (points == nullptr || point_count == 0) {
        return buffer;
    }

    buffer.reserve(static_cast<std::size_t>(point_count));
    for (std::uint64_t i = 0; i < point_count; ++i) {
        buffer.push_back({
            points[i].x,
            points[i].y,
            points[i].z,
            points[i].value,
            point_ids != nullptr ? point_ids[i] : 0u
        });
    }

    return buffer;
}

[[nodiscard]]
inline gs3d::core::PointBuffer make_point_buffer(
    const std::vector<Gs3dPoint>& points,
    const std::uint32_t* point_ids = nullptr
) {
    return make_point_buffer(
        points.data(),
        static_cast<std::uint64_t>(points.size()),
        point_ids
    );
}

[[nodiscard]]
inline gs3d::core::PointBuffer make_point_buffer(
    const Gs3dDataset& dataset,
    const std::uint32_t* point_ids = nullptr
) {
    return make_point_buffer(
        dataset.point_data(),
        dataset.point_count(),
        point_ids
    );
}

} // namespace gs3d::data
