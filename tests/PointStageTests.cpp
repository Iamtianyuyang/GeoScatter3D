#include "data/format/V3BundleReader.hpp"
#include "pipeline/stages/PointStage.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

using gs3d::data::Gs3dPoint;
using gs3d::pipeline::PointStageOptions;
using gs3d::pipeline::point_stage_complete;
using gs3d::pipeline::read_point_chunk_file;
using gs3d::pipeline::verify_point_chunks;
using gs3d::pipeline::write_point_stage;

namespace {

std::filesystem::path temp_dir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("gs3d-stage-" + std::to_string(stamp));
}

std::vector<Gs3dPoint> make_points(std::size_t count) {
    std::vector<Gs3dPoint> points(count);
    for (std::size_t i = 0; i < count; ++i) {
        const float value = static_cast<float>(i);
        points[i] = Gs3dPoint{value, value * 2.0f, value * 3.0f, value};
    }
    return points;
}

PointStageOptions options(std::uint32_t per_chunk) {
    PointStageOptions out;
    out.points_per_chunk = per_chunk;
    out.job_id = "job-1";
    out.source_path = "source.dat";
    out.source_bytes = 123;
    out.source_sha256 = "sha";
    out.options_hash = "opts";
    return out;
}

} // namespace

TEST_CASE("Point stage splits points into chunks and records the manifest")
{
    const std::filesystem::path dir = temp_dir();
    const std::vector<Gs3dPoint> points = make_points(1000);

    const auto result = write_point_stage(dir, points, options(400));
    REQUIRE(result.points == 1000);
    REQUIRE(result.chunks == 3);
    REQUIRE(std::filesystem::exists(dir / "points/000000.chunk"));
    REQUIRE(std::filesystem::exists(dir / "points/000002.chunk"));
    REQUIRE_FALSE(std::filesystem::exists(dir / "points/000003.chunk"));

    const auto manifest = gs3d::data::load_bundle_manifest(dir);
    REQUIRE(manifest.point_count == 1000);
    REQUIRE(manifest.stride == 16);
    REQUIRE(manifest.chunks.size() == 3);
    REQUIRE(manifest.chunks[2].points == 200);
    REQUIRE(manifest.job.stages.at("points") == "done");
    REQUIRE(manifest.job.options_hash == "opts");
    REQUIRE(point_stage_complete(dir));
    REQUIRE(verify_point_chunks(dir));

    const auto back = read_point_chunk_file(dir / "points/000001.chunk");
    REQUIRE(back.size() == 400);
    REQUIRE(back[0].x == 400.0f);
    REQUIRE(back[399].z == 2397.0f);

    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);
}

TEST_CASE("Point stage rejects unusable chunk sizes")
{
    const std::filesystem::path dir = temp_dir();
    const std::vector<Gs3dPoint> points = make_points(4);
    REQUIRE_THROWS_AS(write_point_stage(dir, points, options(0)), std::runtime_error);
    REQUIRE_THROWS_AS(write_point_stage(dir, points, options(65537)), std::runtime_error);
    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);
}

TEST_CASE("Point stage restarts cleanly after an interrupted run")
{
    const std::filesystem::path dir = temp_dir();
    const std::vector<Gs3dPoint> points = make_points(900);
    write_point_stage(dir, points, options(300));

    // Simulate a kill between chunk writes and the manifest update.
    std::filesystem::remove(dir / "manifest.json");
    std::filesystem::remove(dir / "points/000002.chunk");
    REQUIRE_FALSE(point_stage_complete(dir));

    const auto result = write_point_stage(dir, points, options(300));
    REQUIRE(result.chunks == 3);
    REQUIRE(point_stage_complete(dir));
    // The abandoned 4th chunk of a wider earlier run must not linger.
    write_point_stage(dir, points, options(100));
    REQUIRE_FALSE(std::filesystem::exists(dir / "points/000009.chunk"));
    REQUIRE(gs3d::data::load_bundle_manifest(dir).chunks.size() == 9);

    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);
}

TEST_CASE("Point stage verification detects corrupted chunk bytes")
{
    const std::filesystem::path dir = temp_dir();
    const std::vector<Gs3dPoint> points = make_points(50);
    write_point_stage(dir, points, options(50));
    REQUIRE(verify_point_chunks(dir));

    const std::filesystem::path chunk = dir / "points/000000.chunk";
    {
        std::ofstream stream(chunk, std::ios::binary | std::ios::in);
        stream.seekp(40);
        const char byte = '\x7f';
        stream.write(&byte, 1);
    }
    std::string error;
    REQUIRE_FALSE(verify_point_chunks(dir, &error));
    REQUIRE_FALSE(error.empty());

    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);
}

TEST_CASE("v3 bundle reads back every point it was written with")
{
    const std::filesystem::path dir = temp_dir();
    const std::vector<Gs3dPoint> points = make_points(700);
    auto stage_options = options(256);
    stage_options.origin = {1000.0, 2000.0, 30.0};
    stage_options.bounds_min = {-10.0, -20.0, -1.0};
    stage_options.bounds_max = {10.0, 20.0, 1.0};
    write_point_stage(dir, points, stage_options);

    REQUIRE(gs3d::data::is_v3_bundle(dir));
    const gs3d::data::V3BundleData bundle = gs3d::data::read_v3_bundle(dir);
    REQUIRE(bundle.points.size() == points.size());
    REQUIRE(bundle.points[255].x == points[255].x);
    REQUIRE(bundle.points[699].value == points[699].value);
    REQUIRE(bundle.manifest.origin[0] == 1000.0);

    const gs3d::data::Gs3dHeader header = gs3d::data::v3_bundle_header(bundle);
    REQUIRE(header.point_count == points.size());
    REQUIRE(header.value_max == points.back().value);

    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);
}

TEST_CASE("v3 bundle reader rejects a corrupted chunk before handing out points")
{
    const std::filesystem::path dir = temp_dir();
    write_point_stage(dir, make_points(300), options(128));

    std::fstream chunk(dir / "points/000001.chunk",
                       std::ios::binary | std::ios::in | std::ios::out);
    REQUIRE(chunk.is_open());
    chunk.seekp(60);
    const char byte = '\x2a';
    chunk.write(&byte, 1);
    chunk.close();

    REQUIRE_THROWS_AS(gs3d::data::read_v3_bundle(dir), std::runtime_error);

    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);
}
