#pragma once

#include "core/PointData.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace gs3d::scene {

struct SelectionSet {
    std::vector<std::uint64_t> point_ids{};
};

struct FilterState {
    bool enabled = false;
    int active_attribute_index = 0;
};

struct DatasetState {
    std::string active_dataset{};
    std::string path{};
    std::string format = "GS3D";
    std::uint64_t point_count = 0;
    std::string file_size = "--";
    std::string bounding_box{};
    std::vector<std::string> dataset_tree{};
    std::vector<std::string> attributes{};
    int active_attribute_index = 0;
};

struct SceneState {
    DatasetState dataset{};
    FilterState filters{};
    SelectionSet selection{};
    core::PointBuffer active_points{};
};

} // namespace gs3d::scene
