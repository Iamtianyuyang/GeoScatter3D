#pragma once

#include "core/DatasetDescriptor.hpp"
#include "core/PointData.hpp"

#include <cstdint>
#include <vector>

namespace gs3d::core {

struct SelectionSet {
    std::vector<std::uint64_t> point_ids{};
};

struct FilterState {
    bool enabled = false;
};

struct SceneState {
    const core::DatasetDescriptor* active_dataset = nullptr;
    int active_attribute_index = 0;   // color channel attr_list index
    int active_height_index    = 1;   // height channel attr_list index
    FilterState filters{};
    SelectionSet selection{};
    core::PointBuffer active_points{};
};

} // namespace gs3d::core

// 向后兼容命名空间别名
namespace gs3d::scene {
    using SelectionSet = gs3d::core::SelectionSet;
    using FilterState  = gs3d::core::FilterState;
    using SceneState   = gs3d::core::SceneState;
} // namespace gs3d::scene
