#pragma once

#include "data/CsvChunkPlanner.hpp"
#include "data/DataSchema.hpp"
#include "data/Gs3dFormat.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace gs3d::data {

enum class CsvParseErrorCode {
    MissingColumn,
    InvalidFloat,
    TooFewFields,
    EmptyLine,
    Unknown
};

struct CsvParseError {
    std::uint32_t chunk_id = 0;
    std::uint64_t byte_offset = 0;
    std::uint64_t line_number = 0;
    CsvParseErrorCode code = CsvParseErrorCode::Unknown;
    std::string line_preview;
};

struct CsvChunkStatsResult {
    std::uint32_t chunk_id = 0;

    std::uint64_t total_lines = 0;
    std::uint64_t data_lines = 0;
    std::uint64_t valid_records = 0;
    std::uint64_t invalid_records = 0;

    bool empty = true;

    double xmin = 0.0;
    double xmax = 0.0;
    double ymin = 0.0;
    double ymax = 0.0;
    double zmin = 0.0;
    double zmax = 0.0;

    float value_min = 0.0f;
    float value_max = 0.0f;

    std::vector<CsvParseError> errors;
};

struct CsvChunkPointResult {
    std::uint32_t chunk_id = 0;
    std::uint64_t valid_records = 0;
    std::uint64_t invalid_records = 0;
    std::vector<Gs3dPoint> points;
    std::vector<CsvParseError> errors;
};

struct CsvChunkPointWriteResult {
    std::uint32_t chunk_id = 0;
    std::uint64_t valid_records = 0;
    std::uint64_t invalid_records = 0;
    std::vector<CsvParseError> errors;
};

struct CsvRawPoint {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    float value = 0.0f;
};

struct CsvChunkBufferedPointResult {
    std::uint32_t chunk_id = 0;

    std::uint64_t valid_records = 0;
    std::uint64_t invalid_records = 0;

    bool empty = true;

    double xmin = 0.0;
    double xmax = 0.0;
    double ymin = 0.0;
    double ymax = 0.0;
    double zmin = 0.0;
    double zmax = 0.0;

    float value_min = 0.0f;
    float value_max = 0.0f;

    std::vector<CsvRawPoint> points;
    std::vector<CsvParseError> errors;
};

struct CsvChunkReaderConfig {
    std::size_t max_error_records = 100;
};

class CsvChunkReader {
public:
    explicit CsvChunkReader(
        CsvReadConfig read_config = {},
        CsvChunkReaderConfig chunk_config = {}
    );

    [[nodiscard]]
    CsvChunkStatsResult parse_chunk_for_stats(
        const std::filesystem::path& path,
        const CsvSniffResult& sniff,
        const CsvByteChunk& chunk
    ) const;

    [[nodiscard]]
    CsvChunkPointResult parse_chunk_for_points(
        const std::filesystem::path& path,
        const CsvSniffResult& sniff,
        const CsvByteChunk& chunk,
        const StatisticsResult& statistics
    ) const;

    [[nodiscard]]
    CsvChunkPointWriteResult parse_chunk_into_points(
        const std::filesystem::path& path,
        const CsvSniffResult& sniff,
        const CsvByteChunk& chunk,
        const StatisticsResult& statistics,
        std::span<Gs3dPoint> output_points
    ) const;

    [[nodiscard]]
    CsvChunkBufferedPointResult parse_chunk_buffered_points(
        const std::filesystem::path& path,
        const CsvSniffResult& sniff,
        const CsvByteChunk& chunk
    ) const;

private:
    CsvReadConfig read_config_;
    CsvChunkReaderConfig chunk_config_;
};

} // namespace gs3d::data
