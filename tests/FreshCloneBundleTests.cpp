#include "data/Gs3dReader.hpp"
#include "data/Gs3dTileReader.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

namespace {

const std::filesystem::path kSampleBundleDir =
    GS3D_SAMPLE_BUNDLE_DIR;

TEST_CASE(
    "Fresh-clone preprocessing writes a readable sample bundle",
    "[fresh_clone][integration]"
) {
    const auto source_path = kSampleBundleDir / "source.gs3d";
    const auto index_path = kSampleBundleDir / "tiles.gs3dtiles.index";
    const auto tile_path = kSampleBundleDir / "tiles.gs3dtiles";

    REQUIRE(std::filesystem::is_regular_file(source_path));
    REQUIRE(std::filesystem::is_regular_file(index_path));
    REQUIRE(std::filesystem::is_regular_file(tile_path));

    const auto dataset = gs3d::data::Gs3dReader::read_all(source_path);
    REQUIRE(dataset.header.version == gs3d::data::GS3D_VERSION);
    REQUIRE(dataset.points.size() == 25);

    const auto& first = dataset.points.front();
    CHECK(first.x == -50.0f);
    CHECK(first.y == -50.0f);
    CHECK(first.z == -10.0f);
    CHECK(first.value == 12.0f);

    const auto& last = dataset.points.back();
    CHECK(last.x == 50.0f);
    CHECK(last.y == 50.0f);
    CHECK(last.z == 8.0f);
    CHECK(last.value == 10.0f);

    const auto tiles = gs3d::data::Gs3dTileReader::open(
        index_path,
        tile_path,
        dataset.header
    );
    REQUIRE(tiles.valid());
    REQUIRE(tiles.tile_count() == 1);
    REQUIRE(tiles.total_point_count() == dataset.points.size());
    REQUIRE(tiles.has_embedded_point_ids());

    const auto tile_points = tiles.read_tile_points_with_ids(0);
    CHECK(tile_points.points.size() == dataset.points.size());
    CHECK(tile_points.point_ids.size() == dataset.points.size());
}

} // namespace
