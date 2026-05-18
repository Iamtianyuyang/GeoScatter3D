#pragma once

#include "data/CsvStreamReader.hpp"

#include <cstdint>
#include <filesystem>

namespace gs3d::preprocess {

struct StatisticsResult {
    std::uint64_t point_count = 0;
    std::uint64_t total_lines = 0;
    std::uint64_t data_lines = 0;
    std::uint64_t valid_records = 0;
    std::uint64_t invalid_records = 0;

    double xmin = 0.0;
    double xmax = 0.0;

    double ymin = 0.0;
    double ymax = 0.0;

    double zmin = 0.0;
    double zmax = 0.0;

    float value_min = 0.0f;
    float value_max = 0.0f;

    double origin_x = 0.0;
    double origin_y = 0.0;
    double origin_z = 0.0;

    bool empty = true;
};

class StatisticsPass {
public:
    explicit StatisticsPass(gs3d::data::CsvReadConfig config = {});

    [[nodiscard]]
    StatisticsResult run(const std::filesystem::path& csv_path) const;

private:
    gs3d::data::CsvReadConfig config_;

private:
    static void update_bounds(
        StatisticsResult& result,
        const gs3d::data::CsvPointRecord& record
    );

    static void finalize_origin(StatisticsResult& result);
};

} // namespace gs3d::preprocess