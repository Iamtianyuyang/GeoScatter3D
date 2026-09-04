#include "preprocess/StatisticsPass.hpp"
#include "util/Log.hpp"

#include "data/CsvChunkReader.hpp"
#include "data/CsvChunkPlanner.hpp"
#include "data/CsvSniffer.hpp"
#include "util/ThreadPool.hpp"

#include <algorithm>
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
        hw
    );
}

} // namespace

StatisticsPass::StatisticsPass(gs3d::data::CsvReadConfig config)
    : config_(std::move(config))
{
}

StatisticsResult StatisticsPass::run(
    const std::filesystem::path& csv_path
) const {
    try {
        return run_parallel(csv_path);
    } catch (const std::runtime_error&) {
        throw;
    } catch (...) {
        return run_sequential(csv_path);
    }
}

StatisticsResult StatisticsPass::run_sequential(
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

StatisticsResult StatisticsPass::run_parallel(
    const std::filesystem::path& csv_path
) const {
    gs3d::data::CsvSniffer sniffer(config_);
    const auto sniff = sniffer.sniff(csv_path);

    gs3d::data::CsvChunkPlanConfig plan_config;
    const auto chunks =
        gs3d::data::CsvChunkPlanner::plan(
            csv_path,
            sniff,
            plan_config
        );

    if (chunks.size() <= 1) {
        return run_sequential(csv_path);
    }

    const auto worker_count = resolve_thread_count(chunks.size());
    if (worker_count <= 1) {
        return run_sequential(csv_path);
    }

    gs3d::util::log::info() << "[CSV] parallel statistics: chunks="
              << chunks.size()
              << ", threads="
              << worker_count
              << '\n';

    gs3d::data::CsvChunkReader reader(config_);
    gs3d::util::ThreadPool pool(worker_count);

    std::vector<std::future<gs3d::data::CsvChunkStatsResult>> futures;
    futures.reserve(chunks.size());

    for (const auto& chunk : chunks) {
        futures.push_back(pool.submit([&, chunk] {
            return reader.parse_chunk_for_stats(
                csv_path,
                sniff,
                chunk
            );
        }));
    }

    StatisticsResult result;
    for (auto& future : futures) {
        merge_chunk_stats(result, future.get());
    }

    result.point_count = result.valid_records;
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

void StatisticsPass::merge_chunk_stats(
    StatisticsResult& out,
    const gs3d::data::CsvChunkStatsResult& chunk
) {
    out.total_lines += chunk.total_lines;
    out.data_lines += chunk.data_lines;
    out.valid_records += chunk.valid_records;
    out.invalid_records += chunk.invalid_records;

    if (chunk.empty) {
        return;
    }

    if (out.empty) {
        out.xmin = chunk.xmin;
        out.xmax = chunk.xmax;
        out.ymin = chunk.ymin;
        out.ymax = chunk.ymax;
        out.zmin = chunk.zmin;
        out.zmax = chunk.zmax;
        out.value_min = chunk.value_min;
        out.value_max = chunk.value_max;
        out.empty = false;
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

void StatisticsPass::finalize_origin(StatisticsResult& result) {
    result.origin_x = 0.5 * (result.xmin + result.xmax);
    result.origin_y = 0.5 * (result.ymin + result.ymax);
    result.origin_z = 0.5 * (result.zmin + result.zmax);
}

} // namespace gs3d::preprocess
