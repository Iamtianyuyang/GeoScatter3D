#pragma once

#include "data/format/Manifest.hpp"
#include "data/format/Point.hpp"
#include "data/format/PointChunk.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace gs3d::pipeline {

inline constexpr const char* kPointStageName = "points";

struct PointStageOptions {
    std::uint32_t points_per_chunk = gs3d::data::GS3D_POINT_CHUNK_MAX_POINTS;
    std::array<double, 3> origin{0.0, 0.0, 0.0};
    std::array<double, 3> bounds_min{0.0, 0.0, 0.0};
    std::array<double, 3> bounds_max{0.0, 0.0, 0.0};
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

// Streams points into `points/NNNNNN.chunk` and only records the stage as done
// in manifest.json on finish(), so an interrupted run restarts this stage and
// never mixes two preprocessing runs.
class PointChunkWriter {
public:
    PointChunkWriter(std::filesystem::path bundle_dir, PointStageOptions options);
    ~PointChunkWriter();

    PointChunkWriter(const PointChunkWriter&) = delete;
    PointChunkWriter& operator=(const PointChunkWriter&) = delete;

    void write(std::span<const gs3d::data::Gs3dPoint> points);
    PointStageResult finish();

private:
    void flush_full_chunks();

    std::filesystem::path bundle_dir_;
    PointStageOptions options_;
    std::vector<gs3d::data::Gs3dPoint> pending_;
    std::vector<gs3d::data::ManifestChunk> chunks_;
    std::uint64_t written_points_ = 0;
    bool finished_ = false;
};



// True only when the manifest lists the point stage as done and every listed
// chunk still exists with the recorded size.
bool point_stage_complete(const std::filesystem::path& bundle_dir);

// Full check used when opening a bundle: reads every listed chunk and compares
// its CRC32C with the manifest.
bool verify_point_chunks(const std::filesystem::path& bundle_dir, std::string* error = nullptr);

PointStageResult write_point_stage(const std::filesystem::path& bundle_dir,
                                   std::span<const gs3d::data::Gs3dPoint> points,
                                   const PointStageOptions& options);

std::vector<gs3d::data::Gs3dPoint> read_point_chunk_file(const std::filesystem::path& file);

} // namespace gs3d::pipeline
