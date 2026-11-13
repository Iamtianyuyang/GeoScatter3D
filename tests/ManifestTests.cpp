#include "data/format/Manifest.hpp"
#include "foundation/io/Sha256.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>

using gs3d::data::Manifest;
using gs3d::data::ManifestAttribute;
using gs3d::data::ManifestChunk;
using gs3d::data::parse_manifest;
using gs3d::data::serialize_manifest;

namespace {

Manifest sample_manifest() {
    Manifest manifest;
    manifest.required_features = {"tiles"};
    manifest.crs.authority = "EPSG";
    manifest.crs.code = 32650;
    manifest.crs.axis_order = "east,north,up";
    manifest.crs.unit = "m";
    manifest.crs.vertical_unit = "m";
    manifest.crs.datum = "WGS84";
    manifest.origin = {28165200.0, 57031700.0, 3243.5};
    manifest.bounds_min = {-18688700.0, -20026100.0, -400.5};
    manifest.bounds_max = {18688700.0, 20026100.0, 400.5};
    manifest.point_count = 50000000;
    manifest.stride = 16;
    manifest.attributes = {ManifestAttribute{"elevation", "f32", "m", true, -9999.0}};
    manifest.chunks = {ManifestChunk{"points/000000.chunk", 65536, 1048608, 0x1a2b3c4du}};
    manifest.job.id = "job-1";
    manifest.job.source_path = "x.dat";
    manifest.job.source_bytes = 3350000067ull;
    manifest.job.source_sha256 = "abc";
    manifest.job.options_hash = "def";
    manifest.job.stages = {{"parse", "done"}, {"lod", "pending"}};
    return manifest;
}

std::filesystem::path temp_bundle_dir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("gs3d-manifest-" + std::to_string(stamp));
}

} // namespace

TEST_CASE("Manifest round-trips through JSON")
{
    const Manifest original = sample_manifest();
    const Manifest parsed = parse_manifest(serialize_manifest(original));

    REQUIRE(parsed.format_version == Manifest::kFormatVersion);
    REQUIRE(parsed.min_reader_version == Manifest::kFormatVersion);
    REQUIRE(parsed.required_features == original.required_features);
    REQUIRE(parsed.crs.code == 32650);
    REQUIRE(parsed.origin[2] == 3243.5);
    REQUIRE(parsed.bounds_min[0] == -18688700.0);
    REQUIRE(parsed.point_count == 50000000);
    REQUIRE(parsed.attributes.size() == 1);
    REQUIRE(parsed.attributes[0].has_null);
    REQUIRE(parsed.attributes[0].null_value == -9999.0);
    REQUIRE(parsed.chunks.size() == 1);
    REQUIRE(parsed.chunks[0].crc32c == 0x1a2b3c4du);
    REQUIRE(parsed.job.stages.at("lod") == "pending");
}

TEST_CASE("Manifest rejects bundles this build cannot read")
{
    Manifest manifest = sample_manifest();

    manifest.format_version = 4;
    REQUIRE_THROWS_AS(parse_manifest(serialize_manifest(manifest)), std::runtime_error);

    manifest = sample_manifest();
    manifest.min_reader_version = 4;
    REQUIRE_THROWS_AS(parse_manifest(serialize_manifest(manifest)), std::runtime_error);

    manifest = sample_manifest();
    manifest.required_features = {"quantized-grid"};
    REQUIRE_THROWS_AS(parse_manifest(serialize_manifest(manifest)), std::runtime_error);

    REQUIRE_THROWS_AS(parse_manifest("{\"format_version\": 3}"), std::runtime_error);
    REQUIRE_THROWS_AS(parse_manifest("not json"), std::runtime_error);
}

TEST_CASE("Manifest ignores unknown optional chunk kinds")
{
    const std::string text = R"({
  "format_version": 3, "min_reader_version": 3, "required_features": [],
  "origin": [1.0, 2.0, 3.0],
  "bounds": {"min": [0.0, 0.0, 0.0], "max": [10.0, 10.0, 10.0]},
  "points": {"count": 8, "stride": 16},
  "chunks": [
    {"path": "points/000000.chunk", "points": 8, "bytes": 128, "crc32c": "0x0000abcd"},
    {"path": "future/thing.bin", "points": 1, "bytes": 1, "crc32c": "0x1"}
  ]
})";
    const Manifest manifest = parse_manifest(text);
    REQUIRE(manifest.chunks.size() == 1);
    REQUIRE(manifest.chunks[0].path == "points/000000.chunk");
}

TEST_CASE("Manifest saves atomically and reloads from disk")
{
    const std::filesystem::path dir = temp_bundle_dir();
    const Manifest manifest = sample_manifest();

    gs3d::data::save_bundle_manifest(dir, manifest);
    REQUIRE(std::filesystem::exists(dir / "manifest.json"));
    REQUIRE_FALSE(std::filesystem::exists(dir / "manifest.json.tmp"));

    const Manifest reloaded = gs3d::data::load_bundle_manifest(dir);
    REQUIRE(reloaded.point_count == manifest.point_count);

    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);
}

TEST_CASE("Manifest load reports a missing bundle instead of empty defaults")
{
    const std::filesystem::path dir = temp_bundle_dir();
    REQUIRE_THROWS_AS(gs3d::data::load_bundle_manifest(dir), std::runtime_error);
}

TEST_CASE("source fingerprints match the published SHA-256 vectors")
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "gs3d-sha256-abc.txt";
    std::filesystem::remove(path);
    std::ofstream(path, std::ios::binary) << "abc";
    REQUIRE(gs3d::foundation::sha256_file_hex(path) ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    std::filesystem::remove(path);
    std::ofstream empty(path, std::ios::binary);
    empty.close();
    REQUIRE(gs3d::foundation::sha256_file_hex(path) ==
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    std::filesystem::remove(path);
}
