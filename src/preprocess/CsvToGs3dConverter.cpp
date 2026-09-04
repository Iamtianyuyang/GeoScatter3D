#include "preprocess/CsvToGs3dConverter.hpp"
#include "util/Log.hpp"

#include "data/CsvChunkPlanner.hpp"
#include "data/CsvChunkReader.hpp"
#include "data/CsvSniffer.hpp"
#include "data/Gs3dFormat.hpp"
#include "preprocess/StatisticsPass.hpp"
#include "util/Stopwatch.hpp"
#include "util/ThreadPool.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <system_error>
#include <thread>

namespace gs3d::preprocess {

namespace {

using gs3d::data::CsvPointRecord;
using gs3d::data::Gs3dDataset;
using gs3d::data::Gs3dFormat;
using gs3d::data::Gs3dHeader;
using gs3d::data::Gs3dPoint;

struct BoundsAccum {
    double xmin = std::numeric_limits<double>::max();
    double xmax = std::numeric_limits<double>::lowest();
    double ymin = std::numeric_limits<double>::max();
    double ymax = std::numeric_limits<double>::lowest();
    double zmin = std::numeric_limits<double>::max();
    double zmax = std::numeric_limits<double>::lowest();
    float  value_min = std::numeric_limits<float>::max();
    float  value_max = std::numeric_limits<float>::lowest();
};

std::uint32_t resolve_thread_count(
    std::uint32_t requested_threads,
    std::size_t chunk_count
) noexcept {
    if (chunk_count <= 1) {
        return 1;
    }

    if (requested_threads > 0) {
        return std::min<std::uint32_t>(
            requested_threads,
            static_cast<std::uint32_t>(chunk_count)
        );
    }

    const auto hw = std::thread::hardware_concurrency();
    if (hw <= 1) {
        return 1;
    }

    return std::min<std::uint32_t>(
        static_cast<std::uint32_t>(chunk_count),
        hw
    );
}

[[nodiscard]]
double million_points_per_second(
    std::uint64_t point_count,
    double seconds
) noexcept {
    return seconds > 0.0
        ? static_cast<double>(point_count) / seconds / 1'000'000.0
        : 0.0;
}

[[nodiscard]]
double mebibytes_per_second(
    std::uint64_t byte_count,
    double seconds
) noexcept {
    return seconds > 0.0
        ? static_cast<double>(byte_count) / seconds / (1024.0 * 1024.0)
        : 0.0;
}

void update_bounds(BoundsAccum& b, const CsvPointRecord& rec) noexcept {
    const double x = static_cast<double>(rec.x);
    const double y = static_cast<double>(rec.y);
    const double z = static_cast<double>(rec.z);

    b.xmin = std::min(b.xmin, x);
    b.xmax = std::max(b.xmax, x);
    b.ymin = std::min(b.ymin, y);
    b.ymax = std::max(b.ymax, y);
    b.zmin = std::min(b.zmin, z);
    b.zmax = std::max(b.zmax, z);
    b.value_min = std::min(b.value_min, rec.primary_value);
    b.value_max = std::max(b.value_max, rec.primary_value);
}

void merge_bounds(
    BoundsAccum& out,
    bool& has_bounds,
    const gs3d::data::CsvChunkStatsResult& chunk
) {
    if (chunk.empty) {
        return;
    }

    if (!has_bounds) {
        out.xmin = chunk.xmin;
        out.xmax = chunk.xmax;
        out.ymin = chunk.ymin;
        out.ymax = chunk.ymax;
        out.zmin = chunk.zmin;
        out.zmax = chunk.zmax;
        out.value_min = chunk.value_min;
        out.value_max = chunk.value_max;
        has_bounds = true;
        return;
    }

    out.xmin = std::min(out.xmin, chunk.xmin);
    out.xmax = std::max(out.xmax, chunk.xmax);
    out.ymin = std::min(out.ymin, chunk.ymin);
    out.ymax = std::max(out.ymax, chunk.ymax);
    out.zmin = std::min(out.zmin, chunk.zmin);
    out.zmax = std::max(out.zmax, chunk.zmax);
    out.value_min = std::min(out.value_min, chunk.value_min);
    out.value_max = std::max(out.value_max, chunk.value_max);
}

[[nodiscard]]
Gs3dHeader build_header(
    const BoundsAccum& b,
    std::uint64_t point_count,
    double origin_x,
    double origin_y,
    double origin_z
) {
    auto header = Gs3dFormat::create_empty_header();

    header.point_count       = point_count;
    header.point_data_offset = gs3d::data::GS3D_HEADER_V2_SIZE;

    header.origin_x = origin_x;
    header.origin_y = origin_y;
    header.origin_z = origin_z;

    header.bbox_min_x = static_cast<float>(b.xmin - origin_x);
    header.bbox_min_y = static_cast<float>(b.ymin - origin_y);
    header.bbox_min_z = static_cast<float>(b.zmin - origin_z);

    header.bbox_max_x = static_cast<float>(b.xmax - origin_x);
    header.bbox_max_y = static_cast<float>(b.ymax - origin_y);
    header.bbox_max_z = static_cast<float>(b.zmax - origin_z);

    header.value_min = b.value_min;
    header.value_max = b.value_max;

    return header;
}

void write_gs3d(
    const std::filesystem::path& path,
    const Gs3dHeader& header,
    const std::vector<Gs3dPoint>& points
) {
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            throw std::runtime_error(
                "CsvToGs3dConverter: failed to create directory: " +
                parent.string()
            );
        }
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        throw std::runtime_error(
            "CsvToGs3dConverter: failed to open output file: " + path.string()
        );
    }

    if (!Gs3dFormat::write_header(out, header)) {
        throw std::runtime_error(
            "CsvToGs3dConverter: failed to write .gs3d header"
        );
    }

    if (!Gs3dFormat::write_points(out, points)) {
        throw std::runtime_error(
            "CsvToGs3dConverter: failed to write .gs3d point data"
        );
    }

    out.flush();
    if (!out.good()) {
        throw std::runtime_error(
            "CsvToGs3dConverter: failed to flush output file"
        );
    }
}

} // namespace

CsvToGs3dConverter::CsvToGs3dConverter(
    gs3d::data::CsvReadConfig config,
    gs3d::data::CsvChunkPlanConfig chunk_plan_config
)
    : config_(std::move(config))
    , chunk_plan_config_(std::move(chunk_plan_config))
{
}

std::pair<CsvConvertResult, Gs3dDataset> CsvToGs3dConverter::convert(
    const std::filesystem::path& csv_path,
    const std::filesystem::path& gs3d_path
) const {
    gs3d::data::CsvSniffer sniffer(config_);
    const auto sniff = sniffer.sniff(csv_path);

    const auto chunks = gs3d::data::CsvChunkPlanner::plan(
        csv_path,
        sniff,
        chunk_plan_config_
    );

    if (chunks.size() <= 1) {
        return convert_sequential(csv_path, gs3d_path);
    }

    const auto worker_count = resolve_thread_count(
        chunk_plan_config_.num_threads,
        chunks.size()
    );
    if (worker_count <= 1) {
        return convert_sequential(csv_path, gs3d_path);
    }

    gs3d::util::log::info() << "[CSV] parallel convert: chunks="
              << chunks.size()
              << ", threads="
              << worker_count
              << '\n';

    return convert_parallel(csv_path, gs3d_path, sniff, chunks);
}

std::pair<CsvConvertResult, Gs3dDataset> CsvToGs3dConverter::convert_sequential(
    const std::filesystem::path& csv_path,
    const std::filesystem::path& gs3d_path
) const {
    // ~50 bytes/line for 4-field numeric CSV (x,y,fold,elevation).
    constexpr std::size_t kBytesPerLineEstimate = 50;
    std::error_code size_ec;
    const auto file_bytes = std::filesystem::file_size(csv_path, size_ec);
    const std::size_t estimated_points =
        size_ec ? (1024 * 1024)
                : std::max<std::size_t>(file_bytes / kBytesPerLineEstimate, 1);

    // Phase 1: collect bounds in source double precision.  Large geospatial
    // coordinates must not be narrowed to float before origin subtraction.
    BoundsAccum bounds;
    gs3d::data::CsvStreamReader reader(config_);

    gs3d::util::Stopwatch stats_timer;
    const auto read_stats = reader.read(
        csv_path,
        [&](const CsvPointRecord& rec, std::uint64_t) {
            update_bounds(bounds, rec);
        }
    );
    const double stats_seconds = stats_timer.elapsed_seconds();

    if (read_stats.valid_records == 0) {
        throw std::runtime_error(
            "CsvToGs3dConverter: no valid point records found in " +
            csv_path.string()
        );
    }

    // Phase 2: compute centroid origin (same formula as StatisticsPass).
    const double origin_x = 0.5 * (bounds.xmin + bounds.xmax);
    const double origin_y = 0.5 * (bounds.ymin + bounds.ymax);
    const double origin_z = 0.5 * (bounds.zmin + bounds.zmax);

    // Phase 2: parse again and immediately rebase into the final float
    // representation.  This keeps memory bounded without sacrificing source
    // coordinate precision.
    std::vector<Gs3dPoint> points;
    points.reserve(std::max<std::size_t>(
        estimated_points,
        static_cast<std::size_t>(read_stats.valid_records)
    ));
    gs3d::util::Stopwatch point_timer;
    const auto point_stats = reader.read(
        csv_path,
        [&](const CsvPointRecord& rec, std::uint64_t) {
            points.push_back({
                static_cast<float>(rec.x - origin_x),
                static_cast<float>(rec.y - origin_y),
                static_cast<float>(rec.z - origin_z),
                rec.primary_value
            });
        }
    );
    const double point_seconds = point_timer.elapsed_seconds();

    if (point_stats.valid_records != read_stats.valid_records ||
        point_stats.invalid_records != read_stats.invalid_records) {
        throw std::runtime_error(
            "CsvToGs3dConverter: input changed between statistics and "
            "point conversion passes"
        );
    }

    // Phase 3: build header and validate.
    const auto header = build_header(
        bounds,
        static_cast<std::uint64_t>(points.size()),
        origin_x,
        origin_y,
        origin_z
    );

    if (!Gs3dFormat::is_valid_header(header)) {
        throw std::runtime_error(
            "CsvToGs3dConverter: invalid header: " +
            Gs3dFormat::describe_header_error(header)
        );
    }

    // Phase 4: bulk-write .gs3d (header + all points in one write call).
    gs3d::util::Stopwatch write_timer;
    write_gs3d(gs3d_path, header, points);
    const double write_seconds = write_timer.elapsed_seconds();

    const std::uint64_t output_bytes =
        Gs3dFormat::expected_file_size(header);

    gs3d::util::log::info()
        << "[TIME] csv.stats_seconds = " << stats_seconds << '\n'
        << "[PERF] csv.stats_mpoints_per_second = "
        << million_points_per_second(
               read_stats.valid_records,
               stats_seconds
           )
        << '\n'
        << "[TIME] csv.point_convert_seconds = " << point_seconds << '\n'
        << "[PERF] csv.point_convert_mpoints_per_second = "
        << million_points_per_second(
               point_stats.valid_records,
               point_seconds
           )
        << '\n'
        << "[TIME] csv.write_seconds = " << write_seconds << '\n'
        << "[PERF] csv.write_mib_per_second = "
        << mebibytes_per_second(output_bytes, write_seconds)
        << '\n';

    CsvConvertResult result;
    result.written_points  = static_cast<std::uint64_t>(points.size());
    result.invalid_records = read_stats.invalid_records;
    result.output_file_size = output_bytes;

    // Phase 5: construct Gs3dDataset by moving the point buffer — no file
    // reload required for downstream tile/LOD writing.
    Gs3dDataset dataset(header, std::move(points), gs3d_path);

    return {result, std::move(dataset)};
}

std::pair<CsvConvertResult, Gs3dDataset> CsvToGs3dConverter::convert_parallel(
    const std::filesystem::path& csv_path,
    const std::filesystem::path& gs3d_path,
    const gs3d::data::CsvSniffResult& sniff,
    const std::vector<gs3d::data::CsvByteChunk>& chunks
) const {
    const auto worker_count = resolve_thread_count(
        chunk_plan_config_.num_threads,
        chunks.size()
    );
    gs3d::data::CsvChunkReader reader(config_);
    gs3d::util::ThreadPool pool(worker_count);

    gs3d::util::Stopwatch stats_timer;
    std::vector<std::future<gs3d::data::CsvChunkStatsResult>> stat_futures;
    stat_futures.reserve(chunks.size());

    for (const auto& chunk : chunks) {
        stat_futures.push_back(pool.submit([&, chunk] {
            return reader.parse_chunk_for_stats(
                csv_path,
                sniff,
                chunk
            );
        }));
    }

    BoundsAccum bounds;
    bool has_bounds = false;
    std::uint64_t written_points = 0;
    std::uint64_t invalid_records = 0;
    std::vector<std::uint64_t> chunk_point_counts(chunks.size(), 0);
    std::vector<bool> seen_chunks(chunks.size(), false);

    for (auto& future : stat_futures) {
        auto chunk = future.get();
        if (chunk.chunk_id >= chunks.size() ||
            seen_chunks[chunk.chunk_id]) {
            throw std::runtime_error(
                "CsvToGs3dConverter: invalid parallel chunk plan"
            );
        }
        seen_chunks[chunk.chunk_id] = true;
        written_points += chunk.valid_records;
        invalid_records += chunk.invalid_records;
        merge_bounds(bounds, has_bounds, chunk);
        chunk_point_counts[chunk.chunk_id] = chunk.valid_records;
    }
    const double stats_seconds = stats_timer.elapsed_seconds();

    if (!has_bounds || written_points == 0) {
        throw std::runtime_error(
            "CsvToGs3dConverter: no valid point records found in " +
            csv_path.string()
        );
    }

    const double origin_x = 0.5 * (bounds.xmin + bounds.xmax);
    const double origin_y = 0.5 * (bounds.ymin + bounds.ymax);
    const double origin_z = 0.5 * (bounds.zmin + bounds.zmax);

    StatisticsResult statistics;
    statistics.point_count = written_points;
    statistics.valid_records = written_points;
    statistics.invalid_records = invalid_records;
    statistics.xmin = bounds.xmin;
    statistics.xmax = bounds.xmax;
    statistics.ymin = bounds.ymin;
    statistics.ymax = bounds.ymax;
    statistics.zmin = bounds.zmin;
    statistics.zmax = bounds.zmax;
    statistics.value_min = bounds.value_min;
    statistics.value_max = bounds.value_max;
    statistics.origin_x = origin_x;
    statistics.origin_y = origin_y;
    statistics.origin_z = origin_z;
    statistics.empty = false;

    std::vector<Gs3dPoint> points;
    if (written_points >
        static_cast<std::uint64_t>(points.max_size())) {
        throw std::runtime_error(
            "CsvToGs3dConverter: point count exceeds addressable memory"
        );
    }
    points.resize(static_cast<std::size_t>(written_points));

    std::vector<std::size_t> chunk_offsets(chunks.size(), 0);
    std::size_t next_offset = 0;
    for (std::size_t chunk_id = 0;
         chunk_id < chunk_point_counts.size();
         ++chunk_id) {
        chunk_offsets[chunk_id] = next_offset;
        next_offset += static_cast<std::size_t>(
            chunk_point_counts[chunk_id]
        );
    }

    gs3d::util::log::info()
        << "[CSV] direct final-buffer conversion: bytes="
        << points.size() * sizeof(Gs3dPoint)
        << '\n';

    gs3d::util::Stopwatch point_timer;
    std::vector<
        std::future<gs3d::data::CsvChunkPointWriteResult>
    > point_futures;
    point_futures.reserve(chunks.size());
    for (const auto& chunk : chunks) {
        const auto chunk_id =
            static_cast<std::size_t>(chunk.chunk_id);
        auto* output_begin =
            points.data() + chunk_offsets[chunk_id];
        const auto output_count =
            static_cast<std::size_t>(chunk_point_counts[chunk_id]);

        point_futures.push_back(pool.submit([
            &reader,
            &csv_path,
            &sniff,
            &statistics,
            chunk,
            output_begin,
            output_count
        ] {
            return reader.parse_chunk_into_points(
                csv_path,
                sniff,
                chunk,
                statistics,
                std::span<Gs3dPoint>(
                    output_begin,
                    output_count
                )
            );
        }));
    }

    std::uint64_t second_pass_points = 0;
    std::uint64_t second_pass_invalid = 0;
    for (auto& future : point_futures) {
        auto chunk = future.get();
        second_pass_points += chunk.valid_records;
        second_pass_invalid += chunk.invalid_records;
        if (chunk.valid_records != chunk_point_counts[chunk.chunk_id]) {
            throw std::runtime_error(
                "CsvToGs3dConverter: input changed between parallel "
                "statistics and point conversion passes"
            );
        }
    }
    const double point_seconds = point_timer.elapsed_seconds();

    if (second_pass_points != written_points ||
        second_pass_invalid != invalid_records) {
        throw std::runtime_error(
            "CsvToGs3dConverter: parallel conversion pass counts differ"
        );
    }

    const auto header = build_header(
        bounds,
        static_cast<std::uint64_t>(points.size()),
        origin_x,
        origin_y,
        origin_z
    );

    if (!Gs3dFormat::is_valid_header(header)) {
        throw std::runtime_error(
            "CsvToGs3dConverter: invalid header: " +
            Gs3dFormat::describe_header_error(header)
        );
    }

    gs3d::util::Stopwatch write_timer;
    write_gs3d(gs3d_path, header, points);
    const double write_seconds = write_timer.elapsed_seconds();

    CsvConvertResult result;
    result.written_points = static_cast<std::uint64_t>(points.size());
    result.invalid_records = invalid_records;
    result.output_file_size = Gs3dFormat::expected_file_size(header);

    gs3d::util::log::info()
        << "[TIME] csv.stats_seconds = " << stats_seconds << '\n'
        << "[PERF] csv.stats_mpoints_per_second = "
        << million_points_per_second(written_points, stats_seconds)
        << '\n'
        << "[TIME] csv.point_convert_seconds = "
        << point_seconds
        << '\n'
        << "[PERF] csv.point_convert_mpoints_per_second = "
        << million_points_per_second(
               second_pass_points,
               point_seconds
           )
        << '\n'
        << "[TIME] csv.write_seconds = " << write_seconds << '\n'
        << "[PERF] csv.write_mib_per_second = "
        << mebibytes_per_second(
               result.output_file_size,
               write_seconds
           )
        << '\n';

    Gs3dDataset dataset(header, std::move(points), gs3d_path);
    return {result, std::move(dataset)};
}

} // namespace gs3d::preprocess
