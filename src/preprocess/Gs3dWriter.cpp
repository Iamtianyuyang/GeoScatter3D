#include "preprocess/Gs3dWriter.hpp"

#include <fstream>
#include <stdexcept>

namespace gs3d::preprocess {

Gs3dWriter::Gs3dWriter(gs3d::data::CsvReadConfig config)
    : config_(std::move(config))
{
}

Gs3dWriteResult Gs3dWriter::write(
    const std::filesystem::path& csv_path,
    const std::filesystem::path& output_path,
    const StatisticsResult& statistics
) const {
    if (statistics.point_count == 0) {
        throw std::runtime_error("Gs3dWriter: statistics.point_count is zero");
    }

    std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        throw std::runtime_error(
            "Gs3dWriter: failed to open output file: " + output_path.string()
        );
    }

    const auto header = build_header(statistics);

    out.write(
        reinterpret_cast<const char*>(&header),
        static_cast<std::streamsize>(sizeof(gs3d::data::Gs3dHeader))
    );

    if (!out.good()) {
        throw std::runtime_error("Gs3dWriter: failed to write GS3D header");
    }

    Gs3dWriteResult result;
    result.expected_points = statistics.point_count;

    gs3d::data::CsvStreamReader reader(config_);

    const auto read_stats = reader.read(
        csv_path,
        [&](const gs3d::data::CsvPointRecord& record, std::uint64_t) {
            const auto point = make_point(record, statistics);

            out.write(
                reinterpret_cast<const char*>(&point),
                static_cast<std::streamsize>(sizeof(gs3d::data::Gs3dPoint))
            );

            if (!out.good()) {
                throw std::runtime_error("Gs3dWriter: failed to write GS3D point");
            }

            ++result.written_points;
        }
    );

    result.invalid_records = read_stats.invalid_records;

    if (result.written_points != result.expected_points) {
        throw std::runtime_error(
            "Gs3dWriter: written point count does not match statistics"
        );
    }

    out.flush();
    if (!out.good()) {
        throw std::runtime_error("Gs3dWriter: failed to flush output file");
    }

    const auto expected_size = gs3d::data::Gs3dFormat::expected_file_size(header);
    result.output_file_size = expected_size;

    return result;
}

gs3d::data::Gs3dHeader Gs3dWriter::build_header(
    const StatisticsResult& statistics
) {
    auto header = gs3d::data::Gs3dFormat::create_empty_header();

    header.point_count = statistics.point_count;
    header.point_data_offset = sizeof(gs3d::data::Gs3dHeader);

    header.origin_x = statistics.origin_x;
    header.origin_y = statistics.origin_y;
    header.origin_z = statistics.origin_z;

    header.bbox_min_x = static_cast<float>(statistics.xmin - statistics.origin_x);
    header.bbox_min_y = static_cast<float>(statistics.ymin - statistics.origin_y);
    header.bbox_min_z = static_cast<float>(statistics.zmin - statistics.origin_z);

    header.bbox_max_x = static_cast<float>(statistics.xmax - statistics.origin_x);
    header.bbox_max_y = static_cast<float>(statistics.ymax - statistics.origin_y);
    header.bbox_max_z = static_cast<float>(statistics.zmax - statistics.origin_z);

    header.value_min = statistics.value_min;
    header.value_max = statistics.value_max;

    if (!gs3d::data::Gs3dFormat::is_valid_header(header)) {
        throw std::runtime_error(
            "Gs3dWriter: invalid header: " +
            gs3d::data::Gs3dFormat::describe_header_error(header)
        );
    }

    return header;
}

gs3d::data::Gs3dPoint Gs3dWriter::make_point(
    const gs3d::data::CsvPointRecord& record,
    const StatisticsResult& statistics
) {
    gs3d::data::Gs3dPoint point{};

    point.x = static_cast<float>(
        static_cast<double>(record.x) - statistics.origin_x
    );

    point.y = static_cast<float>(
        static_cast<double>(record.y) - statistics.origin_y
    );

    point.z = static_cast<float>(
        static_cast<double>(record.z) - statistics.origin_z
    );

    point.value = record.primary_value;

    return point;
}

} // namespace gs3d::preprocess