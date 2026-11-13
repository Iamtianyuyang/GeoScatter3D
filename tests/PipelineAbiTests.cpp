#include "ffi/geoscatter3d_ffi.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#if INTPTR_MAX == INT64_MAX
static_assert(sizeof(gs3d_pipeline_config) == 4);
static_assert(sizeof(gs3d_pipeline_job) == 40);
static_assert(offsetof(gs3d_pipeline_job, source_path) == 8);
static_assert(offsetof(gs3d_pipeline_job, value_field) == 32);
static_assert(sizeof(gs3d_pipeline_status) == 328);
static_assert(offsetof(gs3d_pipeline_status, task_id) == 8);
static_assert(offsetof(gs3d_pipeline_status, stage) == 20);
static_assert(offsetof(gs3d_pipeline_status, error) == 52);
static_assert(offsetof(gs3d_pipeline_status, points) == 312);
#endif

TEST_CASE("pipeline ABI copies arguments and reports background task results") {
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() /
        ("gs3d-pipeline-abi-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    const auto source = dir / "points.csv";
    const auto bundle = dir / "bundle";
    std::ofstream(source) << "X,Y,Z,elevation\n1,2,3,4\n";

    gs3d_pipeline_config bad_config{};
    REQUIRE(gs3d_pipeline_create(&bad_config) == nullptr);
    gs3d_pipeline_config config{sizeof(config)};
    gs3d_pipeline* pipeline = gs3d_pipeline_create(&config);
    REQUIRE(pipeline != nullptr);
    std::string source_path = source.string();
    std::string bundle_path = bundle.string();
    gs3d_pipeline_job input{sizeof(input), source_path.c_str(), bundle_path.c_str(), 8, "elevation"};
    uint64_t task = 0;
    REQUIRE(gs3d_pipeline_start(pipeline, &input, &task) == 0);
    REQUIRE(task != 0);
    source_path.clear();
    bundle_path.clear();
    gs3d_pipeline_status status{};
    REQUIRE(gs3d_pipeline_poll(pipeline, &status) == -1);
    status.struct_size = sizeof(status);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    do {
        REQUIRE(gs3d_pipeline_poll(pipeline, &status) == 0);
        if (status.state != GS3D_PIPELINE_RUNNING) break;
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);
    REQUIRE(status.task_id == task);
    REQUIRE(status.state == GS3D_PIPELINE_DONE);
    REQUIRE(status.points == 1);
    REQUIRE(gs3d_pipeline_cancel(pipeline, task) == -2);
    gs3d_pipeline_destroy(pipeline);
    std::error_code ignored;
    fs::remove_all(dir, ignored);
}
