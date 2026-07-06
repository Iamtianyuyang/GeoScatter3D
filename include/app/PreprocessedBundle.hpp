#pragma once

#include "app/AppConfig.hpp"
#include "app/MeasurementManager.hpp"
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

// ── analysis.toml persistence ─────────────────────────────────────────────

// Load measurements from bundle_dir/analysis.toml into the manager.
// Does nothing if the file does not exist (new project → empty list).
void load_analysis(
    const std::filesystem::path& bundle_dir,
    MeasurementManager& measurement
);

// Write all current measurements to bundle_dir/analysis.toml.
// Creates the bundle directory if it does not exist.
void save_analysis(
    const std::filesystem::path& bundle_dir,
    const MeasurementManager& measurement
);

} // namespace gs3d::app
