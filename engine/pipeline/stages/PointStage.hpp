#pragma once

#include "data/format/Manifest.hpp"
#include "data/format/Point.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace gs3d::pipeline {

inline constexpr const char* kPointStageName = "points";

struct PointStageOptions {
    std::uint32_t points_per_chunk = 65536;
    std::string job_id;
    std::string source_path;
    std::uint64_t source_bytes = 0;
    std::string source_sha256;
    std::string options_hash;
};

struct PointStageResult {
    std::uint64_t chunks = 0;
    std::uint64_t points = 0;
};

// Writes `points/NNNNNN.chunk` files and only then records the stage as done in
// manifest.json, so an interrupted run resumes from the last finished stage and
// never mixes two preprocessing runs.
PointStageResult write_point_stage(const std::filesystem::path& bundle_dir,
                                   std::span<const gs3d::data::Gs3dPoint> points,
                                   const PointStageOptions& options);

// True only when the manifest lists the point stage as done and every listed
// chunk still exists with the recorded size.
bool point_stage_complete(const std::filesystem::path& bundle_dir);

// Full check used when opening a bundle: reads every listed chunk and compares
// its CRC32C with the manifest.
bool verify_point_chunks(const std::filesystem::path& bundle_dir, std::string* error = nullptr);

std::vector<gs3d::data::Gs3dPoint> read_point_chunk_file(const std::filesystem::path& file);

} // namespace gs3d::pipeline
