#pragma once

#include "core/TileData.hpp"
#include "data/Gs3dDataset.hpp"
#include "data/Gs3dLodDataset.hpp"
#include "data/Gs3dTileReader.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace gs3d::app {

struct ViewerAppConfig;

// All CPU-side data prepared before Vulkan resources are created. Keeping this
// state together makes the startup path independently testable and prevents
// the render loop from owning data-loading details.
struct ViewerDatasetSession {
    gs3d::data::Gs3dDataset dataset{};
    std::vector<std::uint32_t> full_point_ids{};
    std::optional<gs3d::data::Gs3dTileReader> tile_reader{};
    gs3d::core::TileIndexView tile_index_view{};
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>>
        tile_point_ids_by_tile{};
    gs3d::data::Gs3dLodDataset lod_dataset{};
    std::vector<std::vector<std::uint32_t>> lod_point_ids{};
    std::vector<gs3d::data::Gs3dPoint> runtime_points_by_id{};
    std::vector<std::uint8_t> runtime_points_valid_by_id{};
};

// Reports expected input errors through the standard logger and returns
// std::nullopt; malformed files still surface as exceptions from the readers.
[[nodiscard]] std::optional<ViewerDatasetSession> prepare_viewer_dataset(
    const ViewerAppConfig& config
);

} // namespace gs3d::app
