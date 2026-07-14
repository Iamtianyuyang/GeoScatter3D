#pragma once

#include "app/AppState.hpp"
#include "core/DatasetDescriptor.hpp"
#include "data/Gs3dLodDataset.hpp"
#include "data/Gs3dTileReader.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace gs3d::app {

// Narrow, CPU-only inputs needed to build the UI-facing state for an already
// loaded dataset. This deliberately does not expose the full viewer config.
struct ViewerAppStateInitializationInput {
    const gs3d::core::DatasetDescriptor& dataset;
    const std::vector<AttrDescriptor>& attributes;
    const gs3d::data::Gs3dLodDataset& lod_dataset;
    const std::optional<gs3d::data::Gs3dTileReader>& tile_reader;
    bool tile_preload_enabled = false;
    std::size_t viewport_count = 1;
    int initial_visible_viewport_count = 1;
    bool benchmark_enabled = false;
};

[[nodiscard]] AppState make_initial_viewer_app_state(
    const ViewerAppStateInitializationInput& input
);

} // namespace gs3d::app
