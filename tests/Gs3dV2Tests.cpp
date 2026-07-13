#include "data/Gs3dFormat.hpp"
#include "data/Gs3dReader.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>

namespace {

class TemporaryGs3dFile {
public:
    TemporaryGs3dFile()
    {
        const auto stamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("geoscatter3d-v2-test-" + std::to_string(stamp) + ".gs3d");
    }

    ~TemporaryGs3dFile()
    {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    [[nodiscard]]
    const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void set_single_point_bounds(gs3d::data::Gs3dHeader& header)
{
    header.point_count = 1;
    header.bbox_max_x = 1.0f;
    header.bbox_max_y = 2.0f;
    header.bbox_max_z = 3.0f;
    header.value_max = 4.0f;
}

void write_v2_dataset(
    const std::filesystem::path& path,
    const gs3d::data::Gs3dHeader& header,
    const gs3d::data::Gs3dPoint& point
) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!gs3d::data::Gs3dFormat::write_header(out, header) ||
        !gs3d::data::Gs3dFormat::write_points(
            out,
            std::span<const gs3d::data::Gs3dPoint>(&point, 1)
        )) {
        throw std::runtime_error("failed to write GS3D v2 fixture");
    }
}

} // namespace

TEST_CASE(
    "GS3D v2 writes an explicit little-endian wire format",
    "[gs3d][v2][portable]"
) {
    TemporaryGs3dFile file;
    auto header = gs3d::data::Gs3dFormat::create_empty_header();
    set_single_point_bounds(header);
    const gs3d::data::Gs3dPoint point{1.0f, 2.0f, 3.0f, 4.0f};
    write_v2_dataset(file.path(), header, point);

    std::array<unsigned char, gs3d::data::GS3D_HEADER_V2_SIZE +
                                   gs3d::data::GS3D_POINT_SIZE> bytes{};
    std::ifstream in(file.path(), std::ios::binary);
    in.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );

    REQUIRE(in);
    CHECK(std::filesystem::file_size(file.path()) == bytes.size());
    CHECK(bytes[4] == 2);
    CHECK(bytes[5] == 0);
    CHECK(bytes[6] == 0);
    CHECK(bytes[7] == 0);
    CHECK(bytes[8] == 108);
    CHECK(bytes[9] == 0);
    CHECK(bytes[10] == 0);
    CHECK(bytes[11] == 0);
    CHECK(bytes[gs3d::data::GS3D_HEADER_V2_SIZE] == 0);
    CHECK(bytes[gs3d::data::GS3D_HEADER_V2_SIZE + 1] == 0);
    CHECK(bytes[gs3d::data::GS3D_HEADER_V2_SIZE + 2] == 128);
    CHECK(bytes[gs3d::data::GS3D_HEADER_V2_SIZE + 3] == 63);
}

TEST_CASE(
    "GS3D v2 accepts complete header extensions and rejects truncated ones",
    "[gs3d][v2][header]"
) {
    SECTION("complete extension") {
        TemporaryGs3dFile file;
        auto header = gs3d::data::Gs3dFormat::create_empty_header();
        header.header_size = gs3d::data::GS3D_HEADER_V2_SIZE + 8;
        header.point_data_offset = header.header_size;
        set_single_point_bounds(header);
        const gs3d::data::Gs3dPoint point{1.0f, 2.0f, 3.0f, 4.0f};

        std::ofstream out(file.path(), std::ios::binary | std::ios::trunc);
        const std::array<char, 8> extension_bytes{};
        REQUIRE(gs3d::data::Gs3dFormat::write_header(out, header));
        out.write(
            extension_bytes.data(),
            static_cast<std::streamsize>(extension_bytes.size())
        );
        REQUIRE(out);
        REQUIRE(gs3d::data::Gs3dFormat::write_points(
            out,
            std::span<const gs3d::data::Gs3dPoint>(&point, 1)
        ));
        out.close();

        const auto result = gs3d::data::Gs3dReader::read_all(file.path());
        CHECK(result.header.header_size ==
              gs3d::data::GS3D_HEADER_V2_SIZE + 8);
        REQUIRE(result.points.size() == 1);
        CHECK(result.points[0].value == 4.0f);
    }

    SECTION("truncated extension") {
        TemporaryGs3dFile file;
        auto header = gs3d::data::Gs3dFormat::create_empty_header();
        header.header_size = gs3d::data::GS3D_HEADER_V2_SIZE + 8;
        header.point_data_offset = header.header_size;

        std::ofstream out(file.path(), std::ios::binary | std::ios::trunc);
        REQUIRE(gs3d::data::Gs3dFormat::write_header(out, header));
        out.close();

        CHECK_THROWS_AS(
            gs3d::data::Gs3dReader::read_header(file.path()),
            std::runtime_error
        );
    }
}

TEST_CASE("GS3D v1 remains readable", "[gs3d][v1][compatibility]")
{
    TemporaryGs3dFile file;
    auto header = gs3d::data::Gs3dFormat::create_empty_header();
    header.version = gs3d::data::GS3D_LEGACY_VERSION;
    header.header_size = sizeof(gs3d::data::Gs3dHeader);
    header.point_data_offset = sizeof(gs3d::data::Gs3dHeader);
    set_single_point_bounds(header);
    const gs3d::data::Gs3dPoint point{1.0f, 2.0f, 3.0f, 4.0f};

    // This is intentionally the native-layout representation emitted by
    // historic v1 writers. New code only writes the v2 wire format.
    std::ofstream out(file.path(), std::ios::binary | std::ios::trunc);
    out.write(
        reinterpret_cast<const char*>(&header),
        static_cast<std::streamsize>(sizeof(header))
    );
    out.write(
        reinterpret_cast<const char*>(&point),
        static_cast<std::streamsize>(sizeof(point))
    );
    out.close();

    const auto result = gs3d::data::Gs3dReader::read_all(file.path());
    CHECK(result.header.version == gs3d::data::GS3D_LEGACY_VERSION);
    REQUIRE(result.points.size() == 1);
    CHECK(result.points[0].z == 3.0f);
}
