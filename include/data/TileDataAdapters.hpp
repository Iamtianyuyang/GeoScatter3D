#pragma once

#include "core/TileData.hpp"
#include "data/Gs3dTileReader.hpp"

namespace gs3d::data {

[[nodiscard]]
inline gs3d::core::Bounds3f make_bounds3f(
    const Gs3dTileQueryBox& box
) noexcept {
    return {
        box.min_x,
        box.min_y,
        box.min_z,
        box.max_x,
        box.max_y,
        box.max_z
    };
}

[[nodiscard]]
inline Gs3dTileQueryBox make_tile_query_box(
    const gs3d::core::Bounds3f& bounds
) noexcept {
    Gs3dTileQueryBox box;
    box.min_x = bounds.min_x;
    box.min_y = bounds.min_y;
    box.min_z = bounds.min_z;
    box.max_x = bounds.max_x;
    box.max_y = bounds.max_y;
    box.max_z = bounds.max_z;
    return box;
}

[[nodiscard]]
inline gs3d::core::TileIndexView make_tile_index_view(
    const Gs3dTileReader& reader
) {
    gs3d::core::TileIndexView view;

    const auto& header = reader.index_header();
    view.header.tile_size_x = header.tile_size_x;
    view.header.tile_size_y = header.tile_size_y;
    view.header.grid_origin_x = header.grid_origin_x;
    view.header.grid_origin_y = header.grid_origin_y;
    view.header.bbox_min_z = header.bbox_min_z;
    view.header.bbox_max_z = header.bbox_max_z;

    view.records.reserve(reader.records().size());
    for (const auto& record : reader.records()) {
        view.records.push_back({
            record.tile_id,
            record.tile_x,
            record.tile_y,
            record.point_count,
            record.bbox_min_x,
            record.bbox_min_y,
            record.bbox_min_z,
            record.bbox_max_x,
            record.bbox_max_y,
            record.bbox_max_z
        });
    }

    return view;
}

} // namespace gs3d::data
