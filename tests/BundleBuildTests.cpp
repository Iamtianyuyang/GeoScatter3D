#include "data/format/Manifest.hpp"
#include "data/format/V3BundleReader.hpp"
#include "foundation/io/Sha256.hpp"
#include "pipeline/stages/BundleBuild.hpp"
#include "pipeline/stages/BundleJob.hpp"
#include "pipeline/stages/PointStage.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace {

namespace fs = std::filesystem;

fs::path make_source(const std::string& name, const std::string& body) {
    const std::string dir_name = "gs3d-bundle-" + name;
    const fs::path dir = fs::temp_directory_path() /
                         fs::path(std::u8string(dir_name.begin(), dir_name.end()));
    std::error_code ignored;
    fs::remove_all(dir, ignored);
    fs::create_directories(dir);
    std::ofstream(dir / "input.csv") << "X,Y,Z,elevation\n" << body;
    return dir / "input.csv";
}

gs3d::pipeline::BundleBuildOptions options_for(const fs::path& source, const fs::path& bundle) {
    gs3d::pipeline::BundleBuildOptions options;
    options.source = source;
    options.bundle_dir = bundle;
    options.points_per_chunk = 3;
    return options;
}

bool contains(const std::vector<std::string>& values, const std::string& value) {
    for (const std::string& item : values) {
        if (item == value) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("large CSV chunks preserve point order and ignore invalid records")
{
    const fs::path source = make_source("parallel-points", "");
    std::string block;
    for (int i = 0; i < 500; ++i) {
        block += "1,2,3,4\n5,6,7,8\n";
    }
    {
        std::ofstream out(source, std::ios::app);
        for (int i = 0; i < 2200; ++i) {
            out << block;
            if (i == 1000) out << "invalid,row\n";
        }
    }
    auto options = options_for(source, source.parent_path() / "bundle");
    options.points_per_chunk = 65536;
    const auto result = gs3d::pipeline::build_bundle(options);
    REQUIRE(result.points == 2200000);
    const auto bundle = gs3d::data::read_v3_bundle(options.bundle_dir);
    REQUIRE(bundle.points.size() == 2200000);
    bool ordered = true;
    for (std::size_t i = 0; i < bundle.points.size(); ++i) {
        if (bundle.points[i].x != (i % 2 == 0 ? -2.0f : 2.0f)) {
            ordered = false;
            break;
        }
    }
    REQUIRE(ordered);
    std::error_code ignored;
    fs::remove_all(source.parent_path(), ignored);
}

TEST_CASE("background bundle jobs publish completion and errors")
{
    using gs3d::pipeline::BundleJobState;
    const fs::path source = make_source("background", "1,2,3,4\n");
    const fs::path bundle = source.parent_path() / "bundle";
    gs3d::pipeline::BundleJob job;
    const auto wait = [&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        auto status = job.poll();
        while (status.state == BundleJobState::running &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
            status = job.poll();
        }
        return status;
    };
    const auto first = job.start(options_for(source, bundle));
    const auto complete = wait();
    REQUIRE(complete.id == first);
    REQUIRE(complete.state == BundleJobState::done);
    REQUIRE(complete.points == 1);
    REQUIRE_FALSE(job.cancel(first));

    fs::remove(source);
    const auto second = job.resume(bundle);
    REQUIRE(second != first);
    const auto failed = wait();
    REQUIRE(failed.id == second);
    REQUIRE(failed.state == BundleJobState::failed);
    REQUIRE_FALSE(failed.error.empty());
    REQUIRE_FALSE(job.cancel(first));
    std::error_code ignored;
    fs::remove_all(source.parent_path(), ignored);
}

TEST_CASE("only one background bundle job runs across instances")
{
    using gs3d::pipeline::BundleJobState;
    const fs::path source = make_source("job-limit", "1,2,3,4\n");
    const fs::path large_source = source.parent_path() / "large.csv";
    std::ofstream(large_source).close();
    fs::resize_file(large_source, 64 * 1024 * 1024);
    gs3d::pipeline::BundleJob first;
    gs3d::pipeline::BundleJob second;
    const auto id = first.start(options_for(large_source, source.parent_path() / "first"));
    REQUIRE_THROWS(second.start(options_for(source, source.parent_path() / "second")));
    REQUIRE(first.cancel(id));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    auto status = first.poll();
    while (status.state == BundleJobState::running &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
        status = first.poll();
    }
    REQUIRE(status.state == BundleJobState::cancelled);
    first.start(options_for(source, source.parent_path() / "after"));
    do {
        std::this_thread::yield();
        status = first.poll();
    } while (status.state == BundleJobState::running &&
             std::chrono::steady_clock::now() < deadline);
    REQUIRE(status.state == BundleJobState::done);
    std::error_code ignored;
    fs::remove_all(source.parent_path(), ignored);
}

TEST_CASE("a second build of the same source skips every finished stage")
{
    const fs::path source =
        make_source("resume", "1,2,3,4\n2,3,4,5\n3,4,5,6\n4,5,6,7\n");
    const fs::path bundle = source.parent_path() / "bundle";
    const gs3d::pipeline::BundleBuildOptions options = options_for(source, bundle);

    const gs3d::pipeline::BundleBuildResult first = gs3d::pipeline::build_bundle(options);
    REQUIRE(first.points == 4);
    REQUIRE(gs3d::data::load_bundle_manifest(bundle).attributes.size() == 1);
    REQUIRE(contains(first.stages_run, "stats"));
    REQUIRE(contains(first.stages_run, "points"));

    const gs3d::pipeline::BundleBuildResult second = gs3d::pipeline::build_bundle(options);
    REQUIRE(second.stages_run.empty());
    REQUIRE(contains(second.stages_skipped, "stats"));
    REQUIRE(contains(second.stages_skipped, "points"));
    REQUIRE(second.points == 4);

    std::error_code ignored;
    fs::remove_all(source.parent_path(), ignored);
}

TEST_CASE("a build stopped at a stage boundary resumes from its checkpoint")
{
    const fs::path source = make_source("cancel-boundary-测试", "1,2,3,4\n2,3,4,5\n");
    const fs::path bundle = source.parent_path() / "bundle";
    auto options = options_for(source, bundle);
    options.value_field = "elevation";
    REQUIRE_THROWS(gs3d::pipeline::build_bundle(options, [](const char* stage) {
        if (std::string(stage) == "points") {
            throw std::runtime_error("cancelled");
        }
    }));
    const auto partial = gs3d::data::load_bundle_manifest(bundle);
    REQUIRE(partial.job.stages.at("stats") == "done");
    REQUIRE(partial.job.stages.count("points") == 0);
    REQUIRE(fs::path(std::u8string(partial.job.resume_source_path.begin(),
                                   partial.job.resume_source_path.end())) == fs::absolute(source));
    REQUIRE(partial.job.resume_points_per_chunk == 3);
    REQUIRE(partial.job.resume_value_field == "elevation");
    auto changed = partial;
    changed.job.resume_points_per_chunk = 4;
    gs3d::data::save_bundle_manifest(bundle, changed);
    REQUIRE_THROWS(gs3d::pipeline::resume_bundle(bundle));
    gs3d::data::save_bundle_manifest(bundle, partial);
    const auto resumed = gs3d::pipeline::resume_bundle(bundle);
    REQUIRE(contains(resumed.stages_skipped, "stats"));
    REQUIRE(contains(resumed.stages_run, "points"));
    std::ofstream(source, std::ios::app) << "3,4,5,6\n";
    REQUIRE_THROWS(gs3d::pipeline::resume_bundle(bundle));
    REQUIRE(gs3d::pipeline::verify_point_chunks(bundle, nullptr));
    std::error_code ignored;
    fs::remove_all(source.parent_path(), ignored);
}

TEST_CASE("a source that loses points after statistics is not committed as complete")
{
    const std::string original = "X,Y,Z,elevation\n1,2,3,4\n2,3,4,5\n";
    const fs::path source = make_source("change-during-build", "1,2,3,4\n2,3,4,5\n");
    const auto options = options_for(source, source.parent_path() / "bundle");
    REQUIRE_THROWS(gs3d::pipeline::build_bundle(options, [&](const char* stage) {
        if (std::string(stage) == "points") {
            std::ofstream(source) << "X,Y,Z,elevation\n1,2,3,4\n";
        }
    }));
    const auto partial = gs3d::data::load_bundle_manifest(options.bundle_dir);
    REQUIRE(partial.job.stages.count("points") == 0);
    std::ofstream(source) << original;
    REQUIRE(gs3d::pipeline::resume_bundle(options.bundle_dir).points == 2);
    std::error_code ignored;
    fs::remove_all(source.parent_path(), ignored);
}

TEST_CASE("a checkpoint before statistics can be resumed but not opened as a bundle")
{
    const fs::path source = make_source("before-stats", "1,2,3,4\n");
    const fs::path bundle = source.parent_path() / "bundle";
    REQUIRE_THROWS(gs3d::pipeline::build_bundle(options_for(source, bundle), [](const char* stage) {
        if (std::string(stage) == "stats") {
            throw std::runtime_error("cancelled");
        }
    }));
    REQUIRE_THROWS(gs3d::data::read_v3_bundle(bundle));
    REQUIRE(gs3d::pipeline::resume_bundle(bundle).points == 1);
    std::error_code ignored;
    fs::remove_all(source.parent_path(), ignored);
}

TEST_CASE("an interrupted build resumes at the point stage instead of redoing statistics")
{
    const fs::path source =
        make_source("stages", "1,2,3,4\n2,3,4,5\n3,4,5,6\n4,5,6,7\n5,6,7,8\n");
    const fs::path bundle = source.parent_path() / "bundle";
    const gs3d::pipeline::BundleBuildOptions options = options_for(source, bundle);

    // A run killed right after its statistics stage leaves manifest.json with
    // only that stage recorded and no point chunks at all.
    gs3d::data::Manifest partial;
    partial.job.id = "job-partial";
    partial.job.source_path = source.filename().string();
    partial.job.source_bytes = fs::file_size(source);
    partial.job.source_sha256 = gs3d::foundation::sha256_file_hex(source);
    partial.job.options_hash = "v3/points-per-chunk=3/value-field=";
    partial.origin = {0.0, 0.0, 0.0};
    partial.bounds_min = {1.0, 2.0, 3.0};
    partial.bounds_max = {5.0, 6.0, 7.0};
    partial.point_count = 5;
    partial.job.stages["stats"] = "done";
    gs3d::data::save_bundle_manifest(bundle, partial);

    const gs3d::pipeline::BundleBuildResult result = gs3d::pipeline::build_bundle(options);
    REQUIRE(contains(result.stages_skipped, "stats"));
    REQUIRE(contains(result.stages_run, "points"));
    REQUIRE(result.points == 5);

    std::error_code ignored;
    fs::remove_all(source.parent_path(), ignored);
}

TEST_CASE("resuming a completed bundle rejects corrupt point data")
{
    const fs::path source = make_source("corrupt-resume", "1,2,3,4\n2,3,4,5\n");
    const fs::path bundle = source.parent_path() / "bundle";
    const auto options = options_for(source, bundle);
    REQUIRE(gs3d::pipeline::build_bundle(options).points == 2);
    const auto manifest = gs3d::data::load_bundle_manifest(bundle);
    REQUIRE_FALSE(manifest.chunks.empty());
    std::fstream chunk(bundle / manifest.chunks.front().path,
                       std::ios::binary | std::ios::in | std::ios::out);
    REQUIRE(chunk.is_open());
    chunk.seekp(0);
    chunk.put('\x7f');
    chunk.close();
    REQUIRE_THROWS(gs3d::pipeline::build_bundle(options));
    std::error_code ignored;
    fs::remove_all(source.parent_path(), ignored);
}

TEST_CASE("building protects source and unrelated chunks in an existing points directory")
{
    const fs::path source = make_source("owned-chunks", "1,2,3,4\n");
    const fs::path bundle = source.parent_path() / "bundle";
    fs::create_directories(bundle / "points");
    const fs::path existing = bundle / "points" / "existing.chunk";
    std::ofstream(existing) << "keep me";
    REQUIRE_THROWS(gs3d::pipeline::build_bundle(options_for(source, bundle)));
    REQUIRE(fs::file_size(existing) == 7);
    REQUIRE_FALSE(fs::exists(bundle / "manifest.json"));

    std::ofstream(existing) << "X,Y,Z,elevation\n1,2,3,4\n";
    REQUIRE_THROWS(gs3d::pipeline::build_bundle(options_for(existing, bundle)));
    REQUIRE(fs::file_size(existing) > 7);
    std::error_code ignored;
    fs::remove_all(source.parent_path(), ignored);
}

TEST_CASE("a changed source is rejected without changing the existing bundle")
{
    const fs::path source = make_source("changed", "1,2,3,4\n2,3,4,5\n");
    const fs::path bundle = source.parent_path() / "bundle";
    const gs3d::pipeline::BundleBuildOptions options = options_for(source, bundle);
    REQUIRE(gs3d::pipeline::build_bundle(options).points == 2);

    auto changed_options = options;
    changed_options.points_per_chunk = 4;
    REQUIRE_THROWS(gs3d::pipeline::build_bundle(changed_options));

    std::ofstream(source) << "X,Y,Z,elevation\n9,9,9,9\n8,8,8,8\n7,7,7,7\n";
    REQUIRE_THROWS(gs3d::pipeline::build_bundle(options));
    const gs3d::data::Manifest manifest = gs3d::data::load_bundle_manifest(bundle);
    REQUIRE(manifest.point_count == 2);
    REQUIRE(gs3d::pipeline::verify_point_chunks(bundle, nullptr));

    std::error_code ignored;
    fs::remove_all(source.parent_path(), ignored);
}
