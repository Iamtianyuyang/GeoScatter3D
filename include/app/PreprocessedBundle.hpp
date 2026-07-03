#pragma once

#include "app/AppConfig.hpp"
#include "data/Gs3dDataset.hpp"

#include <filesystem>

namespace gs3d::app {

struct PreprocessedBundlePaths {
    std::filesystem::path bundle_dir;
    std::filesystem::path manifest_path;
    std::filesystem::path gs3d_path;
    std::filesystem::path lod_path;
    std::filesystem::path tile_data_path;
    std::filesystem::path tile_index_path;
    bool lod_enabled = false;
    bool tile_enabled = false;
};

[[nodiscard]]
PreprocessedBundlePaths derive_bundle_paths(
    const std::filesystem::path& source_path
);

[[nodiscard]]
PreprocessedBundlePaths make_bundle_paths(
    const std::filesystem::path& bundle_dir
);

void apply_bundle_paths(
    ViewerAppConfig& viewer,
    const PreprocessedBundlePaths& paths
);

void write_bundle_manifest(
    const PreprocessedBundlePaths& paths,
    const AppConfig& config,
    const gs3d::data::Gs3dDataset& dataset
);

[[nodiscard]]
PreprocessedBundlePaths load_bundle_manifest(
    const std::filesystem::path& bundle_dir
);

} // namespace gs3d::app
