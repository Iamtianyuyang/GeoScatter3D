#include "preprocess/StatisticsPass.hpp"

#include <algorithm>
#include <stdexcept>

namespace gs3d::preprocess {

StatisticsPass::StatisticsPass(gs3d::data::CsvReadConfig config)
    : config_(std::move(config))
{
}

StatisticsResult StatisticsPass::run(
    const std::filesystem::path& csv_path
) const {
    StatisticsResult result;

    gs3d::data::CsvStreamReader reader(config_);

    const auto read_stats = reader.read(
        csv_path,
        [&result](const gs3d::data::CsvPointRecord& record, std::uint64_t) {
            update_bounds(result, record);
        }
    );

    result.total_lines = read_stats.total_lines;
    result.data_lines = read_stats.data_lines;
    result.valid_records = read_stats.valid_records;
    result.invalid_records = read_stats.invalid_records;
    result.point_count = read_stats.valid_records;

    if (result.point_count == 0) {
        throw std::runtime_error(
            "StatisticsPass: no valid point records found"
        );
    }

    finalize_origin(result);

    return result;
}

void StatisticsPass::update_bounds(
    StatisticsResult& result,
    const gs3d::data::CsvPointRecord& record
) {
    const double x = static_cast<double>(record.x);
    const double y = static_cast<double>(record.y);
    const double z = static_cast<double>(record.z);
    const float value = record.primary_value;

    if (result.empty) {
        result.xmin = x;
        result.xmax = x;

        result.ymin = y;
        result.ymax = y;

        result.zmin = z;
        result.zmax = z;

        result.value_min = value;
        result.value_max = value;

        result.empty = false;
        return;
    }

    result.xmin = std::min(result.xmin, x);
    result.xmax = std::max(result.xmax, x);

    result.ymin = std::min(result.ymin, y);
    result.ymax = std::max(result.ymax, y);

    result.zmin = std::min(result.zmin, z);
    result.zmax = std::max(result.zmax, z);

    result.value_min = std::min(result.value_min, value);
    result.value_max = std::max(result.value_max, value);
}

void StatisticsPass::finalize_origin(StatisticsResult& result) {
    result.origin_x = 0.5 * (result.xmin + result.xmax);
    result.origin_y = 0.5 * (result.ymin + result.ymax);
    result.origin_z = 0.5 * (result.zmin + result.zmax);
}

} // namespace gs3d::preprocess