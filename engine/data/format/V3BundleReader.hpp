#pragma once

#include "data/format/Gs3dFormat.hpp"
#include "data/format/Manifest.hpp"
#include "data/format/Point.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gs3d::data {

struct V3BundleData {
    Manifest manifest;
    std::vector<Gs3dPoint> points;
};

// A v3 bundle is a directory whose manifest.json declares format version 3.
bool is_v3_bundle(const std::filesystem::path& path);

// Reads every point chunk and verifies it against the manifest CRC. Throws
// std::runtime_error on a missing chunk, size mismatch or checksum failure.
V3BundleData read_v3_bundle(const std::filesystem::path& bundle_dir);

// Same header layout as the legacy reader so the render layer needs no new
// code path while the streaming rewrite is pending.
Gs3dHeader v3_bundle_header(const V3BundleData& data);

} // namespace gs3d::data
