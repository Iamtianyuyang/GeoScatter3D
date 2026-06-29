#pragma once

#include "core/DatasetDescriptor.hpp"
#include "core/PointData.hpp"

#include <cstdint>

namespace gs3d::scene {

struct SelectionSet {
    std::vector<std::uint64_t> point_ids{};
};

struct FilterState {
    bool enabled = false;
};

struct SceneState {
    const core::DatasetDescriptor* active_dataset = nullptr;
    int active_attribute_index = 0;
    FilterState filters{};
    SelectionSet selection{};
    core::PointBuffer active_points{};
};

} // namespace gs3d::scene
