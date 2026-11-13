#include "data/format/Manifest.hpp"
#include "foundation/io/Sha256.hpp"
#include "pipeline/stages/BundleBuild.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

namespace fs = std::filesystem;

fs::path make_source(const std::string& name, const std::string& body) {
    const fs::path dir = fs::temp_directory_path() / ("gs3d-bundle-" + name);
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

TEST_CASE("a second build of the same source skips every finished stage")
{
    const fs::path source =
        make_source("resume", "1,2,3,4\n2,3,4,5\n3,4,5,6\n4,5,6,7\n");
    const fs::path bundle = source.parent_path() / "bundle";
    const gs3d::pipeline::BundleBuildOptions options = options_for(source, bundle);

    const gs3d::pipeline::BundleBuildResult first = gs3d::pipeline::build_bundle(options);
    REQUIRE(first.points == 4);
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

TEST_CASE("a changed source rebuilds instead of resuming another file's stages")
{
    const fs::path source = make_source("changed", "1,2,3,4\n2,3,4,5\n");
    const fs::path bundle = source.parent_path() / "bundle";
    const gs3d::pipeline::BundleBuildOptions options = options_for(source, bundle);
    REQUIRE(gs3d::pipeline::build_bundle(options).points == 2);

    std::ofstream(source) << "X,Y,Z,elevation\n9,9,9,9\n8,8,8,8\n7,7,7,7\n";
    const gs3d::pipeline::BundleBuildResult rebuilt = gs3d::pipeline::build_bundle(options);
    REQUIRE(rebuilt.points == 3);
    REQUIRE(contains(rebuilt.stages_run, "stats"));
    REQUIRE(contains(rebuilt.stages_run, "points"));
    REQUIRE(rebuilt.stages_skipped.empty());

    const gs3d::data::Manifest manifest = gs3d::data::load_bundle_manifest(bundle);
    REQUIRE(manifest.bounds_max[2] == 9.0 - manifest.origin[2]);

    std::error_code ignored;
    fs::remove_all(source.parent_path(), ignored);
}
