#pragma once

#include <cstdint>
#include <vector>

namespace gs3d::core {

struct Bounds3f {
    float min_x = 0.0f;
    float min_y = 0.0f;
    float min_z = 0.0f;

    float max_x = 0.0f;
    float max_y = 0.0f;
    float max_z = 0.0f;
};

struct TileHeaderView {
    float tile_size_x = 0.0f;
    float tile_size_y = 0.0f;

    float grid_origin_x = 0.0f;
    float grid_origin_y = 0.0f;

    float bbox_min_z = 0.0f;
    float bbox_max_z = 0.0f;
};

struct TileRecordView {
    std::uint64_t tile_id = 0;

    std::uint32_t tile_x = 0;
    std::uint32_t tile_y = 0;

    std::uint64_t point_count = 0;

    float bbox_min_x = 0.0f;
    float bbox_min_y = 0.0f;
    float bbox_min_z = 0.0f;

    float bbox_max_x = 0.0f;
    float bbox_max_y = 0.0f;
    float bbox_max_z = 0.0f;
};

struct TileIndexView {
    TileHeaderView header{};
    std::vector<TileRecordView> records{};
};

} // namespace gs3d::core
