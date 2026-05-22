#include "preprocess/Gs3dWriter.hpp"

#include "data/CsvChunkPlanner.hpp"
#include "data/CsvChunkReader.hpp"
#include "data/CsvSniffer.hpp"
#include "util/ThreadPool.hpp"

#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace gs3d::preprocess {
namespace {

std::uint32_t resolve_thread_count(std::size_t chunk_count) noexcept {
    if (chunk_count <= 1) {
        return 1;
    }

    const auto hw = std::thread::hardware_concurrency();
    if (hw <= 1) {
        return 1;
    }

    return std::min<std::uint32_t>(
        static_cast<std::uint32_t>(chunk_count),
        std::min<std::uint32_t>(hw, 8)
    );
}

} // namespace

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

    gs3d::data::CsvSniffer sniffer(config_);
    const auto sniff = sniffer.sniff(csv_path);

    gs3d::data::CsvChunkPlanConfig plan_config;
    const auto chunks = gs3d::data::CsvChunkPlanner::plan(
        csv_path,
        sniff,
        plan_config
    );

    if (chunks.size() <= 1) {
        return write_sequential(csv_path, output_path, statistics);
    }

    const auto worker_count = resolve_thread_count(chunks.size());
    if (worker_count <= 1) {
        return write_sequential(csv_path, output_path, statistics);
    }

    std::cout << "[CSV] parallel write: chunks="
              << chunks.size()
              << ", threads="
              << worker_count
              << '\n';

    return write_parallel(csv_path, output_path, statistics);
}

Gs3dWriteResult Gs3dWriter::write_sequential(
    const std::filesystem::path& csv_path,
    const std::filesystem::path& output_path,
    const StatisticsResult& statistics
) const {
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

Gs3dWriteResult Gs3dWriter::write_parallel(
    const std::filesystem::path& csv_path,
    const std::filesystem::path& output_path,
    const StatisticsResult& statistics
) const {
    gs3d::data::CsvSniffer sniffer(config_);
    const auto sniff = sniffer.sniff(csv_path);

    gs3d::data::CsvChunkPlanConfig plan_config;
    const auto chunks = gs3d::data::CsvChunkPlanner::plan(
        csv_path,
        sniff,
        plan_config
    );

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

    const auto worker_count = resolve_thread_count(chunks.size());
    gs3d::data::CsvChunkReader reader(config_);
    gs3d::util::ThreadPool pool(worker_count);

    std::vector<std::future<gs3d::data::CsvChunkPointResult>> futures;
    futures.reserve(chunks.size());

    for (const auto& chunk : chunks) {
        futures.push_back(pool.submit([&, chunk] {
            return reader.parse_chunk_for_points(
                csv_path,
                sniff,
                chunk,
                statistics
            );
        }));
    }

    std::vector<gs3d::data::CsvChunkPointResult> chunk_results(
        chunks.size()
    );
    for (auto& future : futures) {
        auto result = future.get();
        chunk_results[result.chunk_id] = std::move(result);
    }

    Gs3dWriteResult summary;
    summary.expected_points = statistics.point_count;

    for (const auto& chunk_result : chunk_results) {
        if (!chunk_result.points.empty()) {
            out.write(
                reinterpret_cast<const char*>(chunk_result.points.data()),
                static_cast<std::streamsize>(
                    chunk_result.points.size() *
                    sizeof(gs3d::data::Gs3dPoint)
                )
            );

            if (!out.good()) {
                throw std::runtime_error(
                    "Gs3dWriter: failed to write GS3D point chunk"
                );
            }
        }

        summary.written_points += chunk_result.valid_records;
        summary.invalid_records += chunk_result.invalid_records;
    }

    if (summary.written_points != summary.expected_points) {
        throw std::runtime_error(
            "Gs3dWriter: written point count does not match statistics"
        );
    }

    out.flush();
    if (!out.good()) {
        throw std::runtime_error("Gs3dWriter: failed to flush output file");
    }

    summary.output_file_size =
        gs3d::data::Gs3dFormat::expected_file_size(header);

    return summary;
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
