#include "app/ViewerApp.hpp"
#include "app/ViewerDatasetSession.hpp"
#include "data/Gs3dFormat.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace {

class TemporaryDatasetFile {
public:
    TemporaryDatasetFile()
    {
        const auto stamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("geoscatter3d-viewer-session-" + std::to_string(stamp) +
             ".gs3d");
    }

    ~TemporaryDatasetFile()
    {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void write_dataset(
    const std::filesystem::path& path,
    const std::vector<gs3d::data::Gs3dPoint>& points
)
{
    auto header = gs3d::data::Gs3dFormat::create_empty_header();
    header.point_count = points.size();
    if (!points.empty()) {
        header.bbox_min_x = header.bbox_max_x = points.front().x;
        header.bbox_min_y = header.bbox_max_y = points.front().y;
        header.bbox_min_z = header.bbox_max_z = points.front().z;
        header.value_min = header.value_max = points.front().value;
        for (const auto& point : points) {
            header.bbox_min_x = std::min(header.bbox_min_x, point.x);
            header.bbox_max_x = std::max(header.bbox_max_x, point.x);
            header.bbox_min_y = std::min(header.bbox_min_y, point.y);
            header.bbox_max_y = std::max(header.bbox_max_y, point.y);
            header.bbox_min_z = std::min(header.bbox_min_z, point.z);
            header.bbox_max_z = std::max(header.bbox_max_z, point.z);
            header.value_min = std::min(header.value_min, point.value);
            header.value_max = std::max(header.value_max, point.value);
        }
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE(gs3d::data::Gs3dFormat::write_header(output, header));
    REQUIRE(gs3d::data::Gs3dFormat::write_points(
        output,
        std::span<const gs3d::data::Gs3dPoint>(points)
    ));
}

gs3d::app::ViewerAppConfig make_config(
    const std::filesystem::path& path
)
{
    gs3d::app::ViewerAppConfig config;
    config.input.gs3d_path = path;
    config.lod.enabled = false;
    config.tile.enabled = false;
    return config;
}

} // namespace

TEST_CASE("ViewerDatasetSession prepares the CPU-side startup state")
{
    TemporaryDatasetFile file;
    const std::vector<gs3d::data::Gs3dPoint> points{
        {1.0f, 2.0f, 3.0f, 4.0f},
        {5.0f, 6.0f, 7.0f, 8.0f}
    };
    write_dataset(file.path(), points);

    const auto session =
        gs3d::app::prepare_viewer_dataset(make_config(file.path()));

    REQUIRE(session.has_value());
    CHECK(session->dataset.point_count() == points.size());
    CHECK(session->full_point_ids == std::vector<std::uint32_t>{1, 2});
    CHECK(!session->tile_reader.has_value());
    CHECK(session->lod_dataset.empty());
    REQUIRE(session->runtime_points_by_id.size() == points.size() + 1);
    REQUIRE(session->runtime_points_valid_by_id.size() == points.size() + 1);
    CHECK(session->runtime_points_valid_by_id[1] == 1);
    CHECK(session->runtime_points_valid_by_id[2] == 1);
    CHECK(session->runtime_points_by_id[2].value == 8.0f);
}

TEST_CASE("ViewerDatasetSession rejects an empty dataset before graphics startup")
{
    TemporaryDatasetFile file;
    write_dataset(file.path(), {});

    const auto session =
        gs3d::app::prepare_viewer_dataset(make_config(file.path()));

    CHECK(!session.has_value());
}
