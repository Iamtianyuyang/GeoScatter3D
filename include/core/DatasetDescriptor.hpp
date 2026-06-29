#pragma once

#include "core/TileData.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace gs3d::core {

struct AttributeDescriptor {
    std::string name{};
};

struct DatasetDescriptor {
    std::string display_name{};
    std::string path{};
    std::string format = "GS3D";
    std::uint64_t point_count = 0;
    std::string file_size = "--";
    Bounds3f bounds{};
    std::vector<std::string> dataset_tree{};
    std::vector<AttributeDescriptor> attributes{};
};

} // namespace gs3d::core
