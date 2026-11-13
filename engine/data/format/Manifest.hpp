#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs3d::data {

// Coordinates in the API are always absolute: chunk data is stored relative to
// `origin` and only converted in one place.
struct ManifestCrs {
    std::string authority;
    int code = 0;
    std::string axis_order;
    std::string unit;
    std::string vertical_unit;
    std::string datum;
};

struct ManifestAttribute {
    std::string name;
    std::string type;
    std::string unit;
    bool has_null = false;
    double null_value = 0.0;
};

struct ManifestChunk {
    std::string path;
    std::uint64_t points = 0;
    std::uint64_t bytes = 0;
    std::uint32_t crc32c = 0;
};

struct ManifestJob {
    std::string id;
    std::string source_path;
    std::uint64_t source_bytes = 0;
    std::string source_sha256;
    std::string options_hash;
    std::map<std::string, std::string> stages; // stage name -> "done" / "pending" / ...
};

struct Manifest {
    static constexpr int kFormatVersion = 3;

    int format_version = kFormatVersion;
    int min_reader_version = kFormatVersion;
    std::vector<std::string> required_features;
    ManifestCrs crs;
    std::array<double, 3> origin{0.0, 0.0, 0.0};
    std::array<double, 3> bounds_min{0.0, 0.0, 0.0};
    std::array<double, 3> bounds_max{0.0, 0.0, 0.0};
    std::uint64_t point_count = 0;
    std::uint32_t stride = 16;
    std::vector<ManifestAttribute> attributes;
    std::vector<ManifestChunk> chunks;
    ManifestJob job;
};

std::string serialize_manifest(const Manifest& manifest);

// Throws std::runtime_error when the document is malformed or cannot be read by
// this build (newer min_reader_version, unknown required feature).
Manifest parse_manifest(std::string_view json_text);

// A bundle directory is valid only when manifest.json parses and passes the
// checks above; `load_bundle_manifest` performs both.
Manifest load_bundle_manifest(const std::filesystem::path& bundle_dir);
void save_bundle_manifest(const std::filesystem::path& bundle_dir, const Manifest& manifest);

} // namespace gs3d::data
