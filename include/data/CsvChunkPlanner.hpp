#pragma once

#include "data/CsvSniffer.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace gs3d::data {

struct CsvByteChunk {
    std::uint32_t chunk_id = 0;

    std::uint64_t nominal_begin = 0;
    std::uint64_t nominal_end = 0;

    std::uint64_t aligned_begin = 0;
    std::uint64_t aligned_end = 0;
};

struct CsvChunkPlanConfig {
    std::uint64_t target_chunk_bytes = 16ull * 1024ull * 1024ull;
    std::uint32_t num_threads = 0;
    std::uint64_t min_parallel_file_bytes = 64ull * 1024ull * 1024ull;
};

class CsvChunkPlanner {
public:
    [[nodiscard]]
    static std::vector<CsvByteChunk> plan(
        const std::filesystem::path& path,
        const CsvSniffResult& sniff,
        const CsvChunkPlanConfig& config
    );
};

} // namespace gs3d::data
