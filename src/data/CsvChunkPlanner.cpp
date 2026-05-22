#include "data/CsvChunkPlanner.hpp"

#include "data/CsvChunkUtils.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <thread>

namespace gs3d::data {
namespace {

std::uint32_t resolve_thread_count(
    const CsvChunkPlanConfig& config,
    std::uint64_t data_bytes
) noexcept {
    if (data_bytes < config.min_parallel_file_bytes) {
        return 1;
    }

    if (config.num_threads > 0) {
        return config.num_threads;
    }

    const auto hw = std::thread::hardware_concurrency();
    if (hw <= 1) {
        return 1;
    }

    return std::min<std::uint32_t>(hw, 8);
}

} // namespace

std::vector<CsvByteChunk> CsvChunkPlanner::plan(
    const std::filesystem::path& path,
    const CsvSniffResult& sniff,
    const CsvChunkPlanConfig& config
) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error(
            "CsvChunkPlanner: failed to open file: " + path.string()
        );
    }

    const auto total_size = file_size(file);
    const auto data_begin = sniff.header_end_offset;
    if (data_begin >= total_size) {
        return {};
    }

    const auto data_bytes = total_size - data_begin;
    const auto worker_count = resolve_thread_count(config, data_bytes);
    if (worker_count <= 1 || data_bytes <= config.target_chunk_bytes) {
        return {
            CsvByteChunk{
                .chunk_id = 0,
                .nominal_begin = data_begin,
                .nominal_end = total_size,
                .aligned_begin = data_begin,
                .aligned_end = total_size,
            }
        };
    }

    const auto chunk_bytes = std::max<std::uint64_t>(
        1,
        config.target_chunk_bytes
    );

    std::vector<CsvByteChunk> chunks;
    std::uint64_t nominal_begin = data_begin;
    std::uint32_t chunk_id = 0;

    while (nominal_begin < total_size) {
        const auto nominal_end = std::min(
            total_size,
            nominal_begin + chunk_bytes
        );

        CsvByteChunk chunk;
        chunk.chunk_id = chunk_id++;
        chunk.nominal_begin = nominal_begin;
        chunk.nominal_end = nominal_end;
        chunk.aligned_begin = nominal_begin == data_begin
            ? data_begin
            : align_to_next_record_begin(file, nominal_begin);
        chunk.aligned_end = nominal_end >= total_size
            ? total_size
            : extend_to_record_end(file, nominal_end);

        if (chunk.aligned_begin < chunk.aligned_end) {
            chunks.push_back(chunk);
        }

        nominal_begin = nominal_end;
    }

    if (!chunks.empty()) {
        chunks.front().aligned_begin = data_begin;
        chunks.back().aligned_end = total_size;
    }

    return chunks;
}

} // namespace gs3d::data
