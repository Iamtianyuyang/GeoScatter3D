#include "app/AppConfig.hpp"
#include "app/ViewerApp.hpp"
#include "util/PercentileStats.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, std::string_view name)
{
    if (!condition) {
        std::cerr << "[FAIL] " << name << '\n';
        ++failures;
    }
}

void test_percentile_of_empty_input_is_zero()
{
    expect(
        gs3d::util::percentile({}, 50.0) == 0.0,
        "percentile of empty input is 0.0"
    );
}

void test_percentile_of_single_element_ignores_p()
{
    expect(
        gs3d::util::percentile({42.0}, 0.0) == 42.0,
        "p0 of single-element input is that element"
    );
    expect(
        gs3d::util::percentile({42.0}, 99.0) == 42.0,
        "p99 of single-element input is that element"
    );
}

void test_percentile_p0_and_p100_are_min_and_max()
{
    const std::vector<double> samples = {5.0, 1.0, 3.0, 4.0, 2.0};
    expect(
        gs3d::util::percentile(samples, 0.0) == 1.0,
        "p0 returns the minimum"
    );
    expect(
        gs3d::util::percentile(samples, 100.0) == 5.0,
        "p100 returns the maximum"
    );
}

void test_percentile_p50_is_median_for_odd_count()
{
    const std::vector<double> samples = {3.0, 1.0, 2.0};
    expect(
        gs3d::util::percentile(samples, 50.0) == 2.0,
        "p50 of {1,2,3} is the median 2.0"
    );
}

void test_percentile_interpolates_between_values()
{
    // Sorted: {0, 10}. p25 -> 0 + 0.25 * (10 - 0) = 2.5
    const std::vector<double> samples = {10.0, 0.0};
    expect(
        gs3d::util::percentile(samples, 25.0) == 2.5,
        "p25 of {0,10} interpolates to 2.5"
    );
}

/*
 * Drives the real ViewerApp render loop in benchmark mode: synthetic
 * camera orbit instead of real input, fixed frame count instead of an
 * interactive window-close exit, frame-time/reload-latency percentiles
 * printed to stdout (see ViewerApp::run()'s "[BENCH] ..." lines).
 *
 * This is a Large/manual test (Vulkan device + GPU required) — it is not
 * part of `ctest`'s default suite, matching docs/spec's decision to keep
 * the fast ctest/dev loop unaffected by GPU-bound benchmarks.
 */
int run_benchmark(int argc, char** argv)
{
    // Reuses the same --config flag as the interactive app (see main.cpp),
    // so the benchmark exercises the real config/viewer.toml dataset/LOD/
    // tile settings rather than a separate hardcoded path.
    auto app_config =
        gs3d::app::AppConfigLoader::load_from_args(argc, argv);

    auto viewer_config = app_config.viewer;
    viewer_config.benchmark_mode = true;
    viewer_config.benchmark_frame_count = 600;

    // Override via env var (not argv — argv is already claimed by
    // AppConfigLoader's --config flag parsing). Useful when the default
    // "settle" window is too short to observe a full async tile reload.
    if (const char* frames_env = std::getenv("GS3D_BENCHMARK_FRAMES")) {
        const auto frames = std::strtoul(frames_env, nullptr, 10);
        if (frames > 0) {
            viewer_config.benchmark_frame_count =
                static_cast<std::uint32_t>(frames);
        }
    }

    gs3d::app::ViewerApp app(viewer_config);
    return app.run();
}

} // namespace

int main(int argc, char** argv)
{
    test_percentile_of_empty_input_is_zero();
    test_percentile_of_single_element_ignores_p();
    test_percentile_p0_and_p100_are_min_and_max();
    test_percentile_p50_is_median_for_odd_count();
    test_percentile_interpolates_between_values();

    if (failures > 0) {
        std::cerr << "[FAIL] " << failures
                  << " PercentileStats assertion(s) failed.\n";
        return 1;
    }

    return run_benchmark(argc, argv);
}
