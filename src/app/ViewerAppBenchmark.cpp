#include "app/ViewerApp.hpp"
#include "util/Log.hpp"
#include "app/ViewerAppInternal.hpp"
#include "app/ViewerAppRunState.hpp"

#include "util/PercentileStats.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace gs3d::app {

namespace {

void print_benchmark_percentiles(
    const char* name,
    const std::vector<double>& samples
) {
    if (samples.empty()) {
        gs3d::util::log::benchmark() << "[BENCH] " << name
                  << ": no samples\n";
        return;
    }

    gs3d::util::log::benchmark() << "[BENCH] " << name << "_p50 = "
              << gs3d::util::percentile(samples, 50.0)
              << '\n';
    gs3d::util::log::benchmark() << "[BENCH] " << name << "_p95 = "
              << gs3d::util::percentile(samples, 95.0)
              << '\n';
    gs3d::util::log::benchmark() << "[BENCH] " << name << "_p99 = "
              << gs3d::util::percentile(samples, 99.0)
              << '\n';
}

[[nodiscard]]
const char* present_mode_label(VkPresentModeKHR present_mode) {
    switch (present_mode) {
    case VK_PRESENT_MODE_IMMEDIATE_KHR:
        return "IMMEDIATE";
    case VK_PRESENT_MODE_MAILBOX_KHR:
        return "MAILBOX";
    case VK_PRESENT_MODE_FIFO_KHR:
        return "FIFO";
    default:
        return "OTHER";
    }
}

} // namespace

std::vector<BenchmarkPickScriptQuery> load_benchmark_pick_script(
    const std::filesystem::path& script_path
) {
    std::ifstream in(script_path);
    if (!in) {
        throw std::runtime_error(
            "ViewerApp: failed to open benchmark pick script: " +
            script_path.string()
        );
    }

    std::vector<BenchmarkPickScriptQuery> queries;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(in, line)) {
        ++line_number;
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream iss(line);
        BenchmarkPickScriptQuery query;
        query.query_index = queries.size();
        if (!(iss >> query.mouse_x >> query.mouse_y)) {
            throw std::runtime_error(
                "ViewerApp: invalid benchmark pick script line " +
                std::to_string(line_number)
            );
        }
        queries.push_back(query);
    }

    if (queries.empty()) {
        throw std::runtime_error(
            "ViewerApp: benchmark pick script contains no queries"
        );
    }
    return queries;
}

void write_benchmark_pick_results(
    const std::filesystem::path& output_path,
    const std::vector<BenchmarkPickObservedResult>& results
) {
    std::error_code ec;
    std::filesystem::create_directories(output_path.parent_path(), ec);
    std::ofstream out(output_path, std::ios::trunc);
    if (!out) {
        throw std::runtime_error(
            "ViewerApp: failed to open benchmark pick result path: " +
            output_path.string()
        );
    }

    out << "query_index has_hit gpu_has_hit all_tiles_resident point_id depth x y z value issue_cpu_ms collect_cpu_ms resident_tile_ids\n";
    for (const auto& result : results) {
        std::ostringstream resident_tiles;
        if (result.resident_tile_ids.empty()) {
            resident_tiles << '-';
        } else {
            for (std::size_t i = 0; i < result.resident_tile_ids.size(); ++i) {
                if (i > 0) {
                    resident_tiles << ',';
                }
                resident_tiles << result.resident_tile_ids[i];
            }
        }
        out << result.query_index << ' '
            << (result.has_hit ? 1 : 0) << ' '
            << (result.gpu_has_hit ? 1 : 0) << ' '
            << (result.all_tiles_resident ? 1 : 0) << ' '
            << result.point_id << ' '
            << result.depth << ' '
            << result.x << ' '
            << result.y << ' '
            << result.z << ' '
            << result.value << ' '
            << result.issue_cpu_ms << ' '
            << result.collect_cpu_ms << ' '
            << resident_tiles.str() << '\n';
    }
}

void ViewerApp::print_benchmark_report(
    const BenchmarkFrameSamples& samples,
    VkPresentModeKHR present_mode
) const {
    gs3d::util::log::benchmark() << "[BENCH] frame_count = "
              << samples.wall_frame_times_ms.size() << '\n';
    gs3d::util::log::benchmark() << "[BENCH] present_mode = "
              << present_mode_label(present_mode)
              << '\n';
    print_benchmark_percentiles(
        "wall_frame_ms",
        samples.wall_frame_times_ms
    );
    print_benchmark_percentiles(
        "cpu_frame_ms",
        samples.cpu_frame_times_ms
    );
    print_benchmark_percentiles(
        "gpu_frame_ms",
        samples.gpu_frame_times_ms
    );
    print_benchmark_percentiles(
        "camera_update_ms",
        samples.camera_update_ms
    );
    print_benchmark_percentiles(
        "lod_tile_select_ms",
        samples.lod_tile_select_ms
    );
    print_benchmark_percentiles(
        "cpu_cull_ms",
        samples.cpu_cull_ms
    );
    print_benchmark_percentiles(
        "upload_record_ms",
        samples.upload_record_ms
    );
    print_benchmark_percentiles(
        "draw_record_ms",
        samples.draw_record_ms
    );
    print_benchmark_percentiles(
        "acquire_wait_ms",
        samples.acquire_wait_ms
    );
    print_benchmark_percentiles(
        "frame_fence_wait_ms",
        samples.frame_fence_wait_ms
    );
    print_benchmark_percentiles(
        "upload_fence_wait_ms",
        samples.upload_fence_wait_ms
    );
    gs3d::util::log::benchmark() << "[BENCH] hover_pick = skipped "
              << "(cursor-dependent, not part of fixed benchmark path)\n";
    if (!samples.tile_reload_samples.empty()) {
        std::vector<double> reload_seconds;
        reload_seconds.reserve(samples.tile_reload_samples.size());
        for (std::size_t index = 0;
             index < samples.tile_reload_samples.size();
             ++index) {
            const auto& sample = samples.tile_reload_samples[index];
            reload_seconds.push_back(sample.seconds);
            gs3d::util::log::benchmark()
                << "[BENCH] reload_sample[" << index << "] = seconds="
                << sample.seconds
                << ", selected_tiles=" << sample.selected_tile_count
                << ", required_tiles=" << sample.required_tile_count
                << ", resident_tiles=" << sample.resident_tile_count
                << '\n';
        }
        gs3d::util::log::benchmark() << "[BENCH] reload_latency_seconds_p50 = "
                  << gs3d::util::percentile(
                         reload_seconds, 50.0)
                  << '\n';
        gs3d::util::log::benchmark() << "[BENCH] reload_latency_seconds_p95 = "
                  << gs3d::util::percentile(
                         reload_seconds, 95.0)
                  << '\n';
    } else {
        gs3d::util::log::benchmark()
            << "[BENCH] reload_latency: "
            << "no completed tile uploads captured.\n";
    }
}

} // namespace gs3d::app
