#pragma once

#include "data/CsvChunkPlanner.hpp"
#include "data/CsvSniffer.hpp"
#include "data/CsvStreamReader.hpp"
#include "data/Gs3dDataset.hpp"

#include <cstdint>
#include <filesystem>

namespace gs3d::preprocess {

struct CsvConvertResult {
    std::uint64_t written_points  = 0;
    std::uint64_t invalid_records = 0;
    std::uint64_t output_file_size = 0;
};

// Single-pass CSV → .gs3d converter.
//
// Reads the CSV exactly once: accumulates bounding-box statistics and
// buffers all points in memory simultaneously.  After computing the
// centroid origin, it transforms points in-place and writes the .gs3d
// file in a single bulk write.  The resulting Gs3dDataset is returned
// directly from the in-memory buffer (no file reload needed).
class CsvToGs3dConverter {
public:
    explicit CsvToGs3dConverter(
        gs3d::data::CsvReadConfig config = {},
        gs3d::data::CsvChunkPlanConfig chunk_plan_config = {}
    );

    // Returns {CsvConvertResult, Gs3dDataset}.
    // The dataset is ready for tile/LOD writing without any further I/O.
    [[nodiscard]]
    std::pair<CsvConvertResult, gs3d::data::Gs3dDataset> convert(
        const std::filesystem::path& csv_path,
        const std::filesystem::path& gs3d_path
    ) const;

private:
    gs3d::data::CsvReadConfig config_;
    gs3d::data::CsvChunkPlanConfig chunk_plan_config_;

private:
    [[nodiscard]]
    std::pair<CsvConvertResult, gs3d::data::Gs3dDataset> convert_sequential(
        const std::filesystem::path& csv_path,
        const std::filesystem::path& gs3d_path
    ) const;

    // Parallel convert with pre-computed sniff/chunks to avoid double I/O.
    [[nodiscard]]
    std::pair<CsvConvertResult, gs3d::data::Gs3dDataset> convert_parallel(
        const std::filesystem::path& csv_path,
        const std::filesystem::path& gs3d_path,
        const gs3d::data::CsvSniffResult& sniff,
        const std::vector<gs3d::data::CsvByteChunk>& chunks
    ) const;
};

} // namespace gs3d::preprocess
