#pragma once

#include "app/AppState.hpp"
#include "core/DatasetDescriptor.hpp"
#include "data/Gs3dDataset.hpp"

#include <filesystem>
#include <vector>

namespace gs3d::app {

// Builds the UI-facing dataset metadata before any GPU work begins.
[[nodiscard]] gs3d::core::DatasetDescriptor make_viewer_dataset_descriptor(
    const gs3d::data::Gs3dDataset& dataset,
    const std::filesystem::path& source_path,
    const std::vector<AttrDescriptor>& attributes
);

} // namespace gs3d::app
