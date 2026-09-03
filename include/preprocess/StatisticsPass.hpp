#pragma once

#include "data/CsvStreamReader.hpp"
#include "data/DataSchema.hpp"

#include <cstdint>
#include <filesystem>

namespace gs3d::data {

struct CsvChunkStatsResult;

} // namespace gs3d::data

namespace gs3d::preprocess {

using StatisticsResult = gs3d::data::StatisticsResult;

class StatisticsPass {
public:
    explicit StatisticsPass(gs3d::data::CsvReadConfig config = {});

    [[nodiscard]]
    StatisticsResult run(const std::filesystem::path& csv_path) const;

private:
    gs3d::data::CsvReadConfig config_;

private:
    [[nodiscard]]
    StatisticsResult run_sequential(
        const std::filesystem::path& csv_path
    ) const;

    [[nodiscard]]
    StatisticsResult run_parallel(
        const std::filesystem::path& csv_path
    ) const;

    static void update_bounds(
        StatisticsResult& result,
        const gs3d::data::CsvPointRecord& record
    );

    static void merge_chunk_stats(
        StatisticsResult& out,
        const gs3d::data::CsvChunkStatsResult& chunk
    );

    static void finalize_origin(StatisticsResult& result);
};

} // namespace gs3d::preprocess
