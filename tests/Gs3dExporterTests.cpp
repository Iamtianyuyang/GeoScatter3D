#include "data/Gs3dExporter.hpp"
#include "data/Gs3dFormat.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

class TemporaryExportDirectory {
public:
    TemporaryExportDirectory() {
        const auto stamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("geoscatter3d-export-test-" + std::to_string(stamp));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryExportDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void write_source(
    const std::filesystem::path& path,
    const std::vector<gs3d::data::Gs3dPoint>& points
) {
    auto header = gs3d::data::Gs3dFormat::create_empty_header();
    header.point_count = points.size();
    header.bbox_max_x = 2.0f;
    header.bbox_max_y = 4.0f;
    header.bbox_max_z = 6.0f;
    header.value_max = 8.0f;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!gs3d::data::Gs3dFormat::write_header(output, header) ||
        !gs3d::data::Gs3dFormat::write_points(output, points)) {
        throw std::runtime_error("failed to write exporter fixture");
    }
}

} // namespace

TEST_CASE("GS3D exporter writes every source point", "[gs3d][export]") {
    TemporaryExportDirectory directory;
    const auto source = directory.path() / "source.gs3d";
    const std::vector<gs3d::data::Gs3dPoint> points{
        {1.0f, 2.0f, 3.0f, 4.0f},
        {2.0f, 4.0f, 6.0f, 8.0f}
    };
    write_source(source, points);

    SECTION("PLY") {
        const auto target = directory.path() / "nested" / "points.ply";
        const auto result = gs3d::data::export_gs3d_points(
            source, target, "ply"
        );
        REQUIRE(result.success);
        CHECK(result.point_count == points.size());

        std::ifstream input(target);
        const std::string content(
            (std::istreambuf_iterator<char>(input)),
            std::istreambuf_iterator<char>()
        );
        CHECK(content.find("element vertex 2") != std::string::npos);
        CHECK(content.find("1 2 3 4") != std::string::npos);
        CHECK(content.find("2 4 6 8") != std::string::npos);
    }

    SECTION("CSV") {
        const auto target = directory.path() / "points.csv";
        const auto result = gs3d::data::export_gs3d_points(
            source, target, "csv"
        );
        REQUIRE(result.success);
        CHECK(result.point_count == points.size());

        std::ifstream input(target);
        const std::string content(
            (std::istreambuf_iterator<char>(input)),
            std::istreambuf_iterator<char>()
        );
        CHECK(content.find("x,y,z,value") != std::string::npos);
        CHECK(content.find("1,2,3,4") != std::string::npos);
        CHECK(content.find("2,4,6,8") != std::string::npos);
    }
}

TEST_CASE("GS3D exporter rejects unsupported formats", "[gs3d][export]") {
    const auto result = gs3d::data::export_gs3d_points(
        "source.gs3d", "points.xyz", "xyz"
    );
    CHECK_FALSE(result.success);
    CHECK(result.error.find("unsupported") != std::string::npos);
}
