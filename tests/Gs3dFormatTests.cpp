#include "data/CsvChunkPlanner.hpp"
#include "data/CsvSniffer.hpp"
#include "data/DataSchema.hpp"
#include "data/Gs3dDataset.hpp"
#include "data/Gs3dFormat.hpp"
#include "data/Gs3dLodDataset.hpp"
#include "data/Gs3dLodFormat.hpp"
#include "data/Gs3dLodReader.hpp"
#include "data/Gs3dLodTargets.hpp"
#include "data/Gs3dReader.hpp"
#include "data/Gs3dTileFormat.hpp"
#include "data/Gs3dTileReader.hpp"
#include "preprocess/CsvToGs3dConverter.hpp"
#include "preprocess/Gs3dLodWriter.hpp"
#include "preprocess/Gs3dTileWriter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

void expect(bool condition, std::string_view name)
{
    INFO(name);
    CHECK(condition);
}

template <typename Function>
void expect_throws(Function&& function, std::string_view name)
{
    try {
        function();
        INFO(name);
        FAIL_CHECK("expected std::runtime_error");
    } catch (const std::runtime_error&) {
    }
}

class TemporaryFile {
public:
    TemporaryFile()
    {
        const auto stamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
            ("geoscatter3d-format-test-" + std::to_string(stamp) + ".gs3d");
    }

    ~TemporaryFile()
    {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void write_dataset(
    const std::filesystem::path& path,
    const gs3d::data::Gs3dHeader& header,
    const gs3d::data::Gs3dPoint* point
)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!gs3d::data::Gs3dFormat::write_header(out, header) ||
        (point != nullptr && !gs3d::data::Gs3dFormat::write_points(
            out,
            std::span<const gs3d::data::Gs3dPoint>(point, 1)
        ))) {
        throw std::runtime_error("failed to write test GS3D dataset");
    }
}

void test_valid_dataset_round_trip()
{
    TemporaryFile file;
    auto header = gs3d::data::Gs3dFormat::create_empty_header();
    header.point_count = 1;
    header.bbox_max_x = 1.0f;
    header.bbox_max_y = 2.0f;
    header.bbox_max_z = 3.0f;
    header.value_max = 4.0f;

    const gs3d::data::Gs3dPoint point{1.0f, 2.0f, 3.0f, 4.0f};
    write_dataset(file.path(), header, &point);

    const auto result = gs3d::data::Gs3dReader::read_all(file.path());
    expect(result.points.size() == 1, "valid dataset point count");
    expect(result.points[0].value == 4.0f, "valid dataset point value");
}

void test_truncated_dataset_is_rejected()
{
    TemporaryFile file;
    auto header = gs3d::data::Gs3dFormat::create_empty_header();
    header.point_count = 1;
    write_dataset(file.path(), header, nullptr);

    expect_throws(
        [&] { static_cast<void>(gs3d::data::Gs3dReader::read_all(file.path())); },
        "truncated dataset"
    );
}

void test_overflowing_header_is_rejected()
{
    auto header = gs3d::data::Gs3dFormat::create_empty_header();
    header.point_count =
        std::numeric_limits<std::uint64_t>::max() /
        sizeof(gs3d::data::Gs3dPoint);
    header.point_data_offset = std::numeric_limits<std::uint64_t>::max();

    expect(
        !gs3d::data::Gs3dFormat::is_valid_header(header),
        "overflowing header"
    );
}

void test_metadata_only_dataset()
{
    TemporaryFile file;
    auto header = gs3d::data::Gs3dFormat::create_empty_header();
    header.point_count = 1;
    const gs3d::data::Gs3dPoint point{1.0f, 2.0f, 3.0f, 4.0f};
    header.bbox_max_x = 1.0f;
    header.bbox_max_y = 2.0f;
    header.bbox_max_z = 3.0f;
    header.value_max = 4.0f;
    write_dataset(file.path(), header, &point);

    const auto dataset =
        gs3d::data::Gs3dDatasetLoader::load_header_only(file.path());
    expect(dataset.is_consistent(), "metadata-only dataset is consistent");
    expect(dataset.metadata_only(), "metadata-only flag");
    expect(dataset.point_count() == 1, "metadata-only source point count");
    expect(!dataset.empty(), "metadata-only source is not empty");
    expect(!dataset.has_point_data(), "metadata-only has no point buffer");
}

void test_chunk_plan_has_no_overlap_at_record_boundary()
{
    TemporaryFile file;
    constexpr std::string_view contents =
        "h\n"
        "111\n"
        "222\n"
        "333\n";
    {
        std::ofstream out(file.path(), std::ios::binary | std::ios::trunc);
        out.write(
            contents.data(),
            static_cast<std::streamsize>(contents.size())
        );
    }

    gs3d::data::CsvSniffResult sniff;
    sniff.header_end_offset = 2;

    gs3d::data::CsvChunkPlanConfig config;
    config.num_threads = 3;
    config.target_chunk_bytes = 4;
    config.min_parallel_file_bytes = 0;

    const auto chunks =
        gs3d::data::CsvChunkPlanner::plan(file.path(), sniff, config);

    expect(chunks.size() == 3, "exact-boundary chunk count");
    expect(
        !chunks.empty() && chunks.front().aligned_begin == 2,
        "chunk plan begins after header"
    );
    expect(
        !chunks.empty() &&
            chunks.back().aligned_end == contents.size(),
        "chunk plan reaches end of file"
    );
    for (std::size_t i = 1; i < chunks.size(); ++i) {
        expect(
            chunks[i - 1].aligned_end == chunks[i].aligned_begin,
            "adjacent chunks neither overlap nor leave gaps"
        );
    }
}

void test_dat_schema_maps_field_statics()
{
    const std::vector<std::string> header{
        "X", "Y", "elevation", "field_statics"
    };
    const std::unordered_map<std::string, std::size_t> field_map{
        {"x", 0},
        {"y", 1},
        {"elevation", 2},
        {"field_statics", 3}
    };

    auto schema = gs3d::data::DataSchema::default_fold_elevation();
    schema.primary_value_field = "field_statics";
    const auto resolved = schema.resolve(field_map, header);

    expect(resolved.x_col == 0, "DAT X column");
    expect(resolved.y_col == 1, "DAT Y column");
    expect(resolved.z_col == 2, "DAT elevation column");
    expect(resolved.primary_value_col == 3, "DAT field_statics column");
}

void test_csv_and_dat_conversion_preserve_field_semantics_and_precision()
{
    {
        TemporaryFile input;
        TemporaryFile output;
        constexpr std::string_view text =
            "x,y,fold,elevation\n"
            "100,200,7.5,1000.25\n"
            "104,208,9.5,1002.25\n";
        {
            std::ofstream out(
                input.path(),
                std::ios::binary | std::ios::trunc
            );
            out.write(
                text.data(),
                static_cast<std::streamsize>(text.size())
            );
        }

        gs3d::data::CsvReadConfig read_config;
        read_config.schema.x_field = "x";
        read_config.schema.y_field = "y";
        read_config.schema.z_field = "elevation";
        read_config.schema.primary_value_field = "fold";

        gs3d::data::CsvChunkPlanConfig plan_config;
        plan_config.num_threads = 1;

        gs3d::preprocess::CsvToGs3dConverter converter(
            read_config,
            plan_config
        );
        const auto [result, dataset] =
            converter.convert(input.path(), output.path());

        expect(result.written_points == 2, "CSV writes every data row");
        expect(result.invalid_records == 0, "CSV has no invalid rows");
        expect(
            dataset.points()[0].value == 7.5f,
            "CSV maps fold to primary value"
        );
        expect(
            std::abs(dataset.points()[0].z + 1.0f) < 1.0e-6f,
            "CSV maps elevation to Z before origin rebasing"
        );
    }

    {
        TemporaryFile input;
        TemporaryFile parallel_output;
        TemporaryFile sequential_output;
        constexpr std::string_view text =
            "X Y elevation field_statics\n"
            "18966454 54945337 2994.0 109.8\n"
            "9476500 37005628 2843.0 -10.0\n"
            "46853979 77057736 3644.0 282.52\n";
        {
            std::ofstream out(
                input.path(),
                std::ios::binary | std::ios::trunc
            );
            out.write(
                text.data(),
                static_cast<std::streamsize>(text.size())
            );
        }

        gs3d::data::CsvReadConfig read_config;
        read_config.schema.x_field = "X";
        read_config.schema.y_field = "Y";
        read_config.schema.z_field = "elevation";
        read_config.schema.primary_value_field = "field_statics";

        gs3d::data::CsvChunkPlanConfig plan_config;
        plan_config.num_threads = 2;
        plan_config.target_chunk_bytes = 32;
        plan_config.min_parallel_file_bytes = 0;

        gs3d::preprocess::CsvToGs3dConverter converter(
            read_config,
            plan_config
        );
        const auto [result, dataset] =
            converter.convert(input.path(), parallel_output.path());

        gs3d::data::CsvChunkPlanConfig sequential_plan;
        sequential_plan.num_threads = 1;
        gs3d::preprocess::CsvToGs3dConverter sequential_converter(
            read_config,
            sequential_plan
        );
        const auto [sequential_result, sequential_dataset] =
            sequential_converter.convert(
                input.path(),
                sequential_output.path()
            );

        expect(result.written_points == 3, "DAT writes every data row");
        expect(result.invalid_records == 0, "DAT has no invalid rows");
        expect(
            std::abs(dataset.points()[0].y - (-2'086'345.0f)) < 0.01f,
            "DAT subtracts the double-precision origin before float storage"
        );
        expect(
            std::abs(dataset.points()[0].value - 109.8f) < 1.0e-4f,
            "DAT maps field_statics to primary value"
        );
        bool same_points =
            sequential_dataset.points().size() == dataset.points().size();
        for (std::size_t i = 0;
             same_points && i < dataset.points().size();
             ++i) {
            const auto& a = sequential_dataset.points()[i];
            const auto& b = dataset.points()[i];
            same_points =
                a.x == b.x && a.y == b.y && a.z == b.z &&
                a.value == b.value;
        }
        expect(
            sequential_result.written_points == result.written_points &&
                sequential_dataset.header().origin_x ==
                    dataset.header().origin_x &&
                sequential_dataset.header().origin_y ==
                    dataset.header().origin_y &&
                same_points,
            "DAT sequential and parallel conversion produce identical output"
        );
    }
}

void test_lod_ratios_empty_falls_back_to_explicit_counts()
{
    const auto result = gs3d::data::resolve_lod_target_point_counts(
        33'021'622ull,
        {},
        {3'000'000ull, 1'000'000ull, 300'000ull}
    );
    expect(
        result == std::vector<std::uint64_t>{
            3'000'000ull, 1'000'000ull, 300'000ull
        },
        "empty ratios fall back to explicit target_point_counts"
    );
}

void test_lod_ratios_scale_with_source_point_count()
{
    const auto result = gs3d::data::resolve_lod_target_point_counts(
        100'000'000ull,
        {0.10, 0.05, 0.02},
        {3'000'000ull, 1'000'000ull, 300'000ull}
    );
    expect(
        result == std::vector<std::uint64_t>{
            10'000'000ull, 5'000'000ull, 2'000'000ull
        },
        "ratios scale to 10%/5%/2% of a 1e8-point source"
    );
}

void test_lod_ratios_same_ratios_give_different_counts_for_smaller_source()
{
    const auto result = gs3d::data::resolve_lod_target_point_counts(
        33'021'622ull,
        {0.10, 0.05, 0.02},
        {}
    );
    expect(
        result == std::vector<std::uint64_t>{
            3'302'162ull, 1'651'081ull, 660'432ull
        },
        "same ratios scale down for a 33M-point source"
    );
}

void test_lod_ratios_negative_ratio_clamped_to_zero()
{
    const auto result = gs3d::data::resolve_lod_target_point_counts(
        1'000'000ull,
        {-0.5, 0.5},
        {}
    );
    expect(
        result[0] == 1ull,
        "negative ratio clamps to the 1-point floor, not zero or negative"
    );
    expect(
        result[1] == 500'000ull,
        "a valid ratio alongside a negative one still resolves correctly"
    );
}

void test_lod_ratios_tiny_ratio_floors_to_one_point()
{
    const auto result = gs3d::data::resolve_lod_target_point_counts(
        100ull,
        {0.001},
        {}
    );
    expect(
        result[0] == 1ull,
        "a ratio too small to reach 1 point floors to 1, not 0"
    );
}

void test_tile_format_v2_has_embedded_point_ids()
{
    using gs3d::data::Gs3dTileFormat;
    expect(
        Gs3dTileFormat::has_embedded_point_ids(
            gs3d::data::GS3D_TILE_VERSION_V2
        ),
        "v2 format reports embedded point IDs"
    );
}

void test_tile_format_v1_no_embedded_point_ids()
{
    using gs3d::data::Gs3dTileFormat;
    expect(
        !Gs3dTileFormat::has_embedded_point_ids(
            gs3d::data::GS3D_TILE_VERSION_V1
        ),
        "v1 format does NOT report embedded point IDs"
    );
}

void test_point_with_id_struct_size()
{
    expect(
        sizeof(gs3d::data::Gs3dPointWithId) == 20,
        "Gs3dPointWithId is 20 bytes (interleaved x,y,z,value,point_id)"
    );
}

void test_tile_format_v2_stride_is_20()
{
    using gs3d::data::Gs3dTileFormat;
    const auto stride =
        Gs3dTileFormat::point_stride_for_version(
            gs3d::data::GS3D_TILE_VERSION_V2
        );
    expect(
        stride == 20,
        "v2 tile point stride is 20 bytes"
    );
}

void test_tile_format_v1_stride_is_16()
{
    using gs3d::data::Gs3dTileFormat;
    const auto stride =
        Gs3dTileFormat::point_stride_for_version(
            gs3d::data::GS3D_TILE_VERSION_V1
        );
    expect(
        stride == 16,
        "v1 tile point stride is 16 bytes"
    );
}

void test_tile_format_accepts_both_strides()
{
    using gs3d::data::Gs3dTileFormat;
    expect(
        Gs3dTileFormat::is_valid_point_stride(16),
        "stride 16 (v1) is valid"
    );
    expect(
        Gs3dTileFormat::is_valid_point_stride(20),
        "stride 20 (v2) is valid"
    );
    expect(
        !Gs3dTileFormat::is_valid_point_stride(0),
        "stride 0 is invalid"
    );
    expect(
        !Gs3dTileFormat::is_valid_point_stride(24),
        "stride 24 is invalid"
    );
}

void test_tile_format_v1_and_v2_are_supported_versions()
{
    using gs3d::data::Gs3dTileFormat;
    expect(
        Gs3dTileFormat::is_supported_version(
            gs3d::data::GS3D_TILE_VERSION_V1
        ),
        "v1 is a supported version"
    );
    expect(
        Gs3dTileFormat::is_supported_version(
            gs3d::data::GS3D_TILE_VERSION_V2
        ),
        "v2 is a supported version"
    );
    expect(
        !Gs3dTileFormat::is_supported_version(0),
        "version 0 is NOT supported"
    );
    expect(
        !Gs3dTileFormat::is_supported_version(3),
        "version 3 is NOT supported"
    );
}

void test_tile_record_validates_both_strides()
{
    using gs3d::data::Gs3dTileFormat;
    using gs3d::data::Gs3dTileRecord;

    // v1 record:  point_count=100, stride=16 → point_data_bytes=1600
    {
        Gs3dTileRecord record{};
        record.point_count = 100;
        record.point_data_bytes = 1600; // 100 × 16
        record.bbox_min_x = 0.0f;
        record.bbox_min_y = 0.0f;
        record.bbox_min_z = 0.0f;
        record.bbox_max_x = 1.0f;
        record.bbox_max_y = 1.0f;
        record.bbox_max_z = 1.0f;
        record.value_min = 0.0f;
        record.value_max = 1.0f;
        Gs3dTileFormat::validate_tile_record(record);
        expect(true, "v1 tile record with stride 16 passes validation");
    }

    // v2 record:  point_count=100, stride=20 → point_data_bytes=2000
    {
        Gs3dTileRecord record{};
        record.point_count = 100;
        record.point_data_bytes = 2000; // 100 × 20
        record.bbox_min_x = 0.0f;
        record.bbox_min_y = 0.0f;
        record.bbox_min_z = 0.0f;
        record.bbox_max_x = 1.0f;
        record.bbox_max_y = 1.0f;
        record.bbox_max_z = 1.0f;
        record.value_min = 0.0f;
        record.value_max = 1.0f;
        Gs3dTileFormat::validate_tile_record(record);
        expect(true, "v2 tile record with stride 20 passes validation");
    }
}

void test_tile_format_stride_for_unknown_version_throws()
{
    using gs3d::data::Gs3dTileFormat;
    expect_throws(
        [] {
            static_cast<void>(Gs3dTileFormat::point_stride_for_version(99));
        },
        "point_stride_for_version(99) throws"
    );
}

void test_parallel_lod_matches_serial_output()
{
    constexpr std::uint32_t width = 400;
    constexpr std::uint32_t height = 300;
    std::vector<gs3d::data::Gs3dPoint> points;
    points.reserve(
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height)
    );

    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            points.push_back({
                static_cast<float>(x),
                static_cast<float>(y),
                static_cast<float>((x + y) % 13),
                static_cast<float>((x * 3 + y * 5) % 29)
            });
        }
    }

    auto header = gs3d::data::Gs3dFormat::create_empty_header();
    header.point_count = points.size();
    header.bbox_min_x = 0.0f;
    header.bbox_max_x = static_cast<float>(width - 1);
    header.bbox_min_y = 0.0f;
    header.bbox_max_y = static_cast<float>(height - 1);
    header.bbox_min_z = 0.0f;
    header.bbox_max_z = 12.0f;
    header.value_min = 0.0f;
    header.value_max = 28.0f;

    const gs3d::data::Gs3dDataset dataset(
        header,
        std::move(points),
        {},
        false
    );

    gs3d::data::Gs3dLodBuildConfig serial_config;
    serial_config.finest_target_points = 10'000;
    serial_config.growth_factor = 2.0f;
    serial_config.min_points_per_level = 500;
    serial_config.num_threads = 1;
    serial_config.verbose = false;

    auto parallel_config = serial_config;
    parallel_config.num_threads = 4;

    const auto serial =
        gs3d::data::Gs3dLodDataset::build(
            dataset,
            serial_config
        );
    const auto parallel =
        gs3d::data::Gs3dLodDataset::build(
            dataset,
            parallel_config
        );

    REQUIRE(serial.level_count() == parallel.level_count());
    for (std::size_t level_index = 0;
         level_index < serial.level_count();
         ++level_index) {
        const auto& expected = serial.level(level_index);
        const auto& actual = parallel.level(level_index);
        REQUIRE(expected.point_count() == actual.point_count());
        CHECK(expected.voxel_size == actual.voxel_size);

        bool same_points = true;
        for (std::size_t point_index = 0;
             same_points &&
             point_index < expected.points.size();
             ++point_index) {
            const auto& a = expected.points[point_index];
            const auto& b = actual.points[point_index];
            same_points =
                a.x == b.x &&
                a.y == b.y &&
                a.z == b.z &&
                a.value == b.value;
        }
        CHECK(same_points);
    }
}

// ---- 字节序 / LOD·tile 强校验测试辅助 -----------------------------------

void write_le_u32(std::ofstream& out, std::uint32_t value)
{
    for (std::size_t i = 0; i < sizeof(value); ++i) {
        out.put(static_cast<char>((value >> (8 * i)) & 0xFFu));
    }
}

void write_le_u64(std::ofstream& out, std::uint64_t value)
{
    for (std::size_t i = 0; i < sizeof(value); ++i) {
        out.put(static_cast<char>((value >> (8 * i)) & 0xFFu));
    }
}

void write_le_f32(std::ofstream& out, float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    write_le_u32(out, bits);
}

void write_be_u32(std::ofstream& out, std::uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.put(static_cast<char>((value >> shift) & 0xFFu));
    }
}

void write_be_u64(std::ofstream& out, std::uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.put(static_cast<char>((value >> shift) & 0xFFu));
    }
}

void write_be_f32(std::ofstream& out, float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    write_be_u32(out, bits);
}

/*
 * 按指定字节序手工拼装 .gs3dlod 文件（v2 布局），用于验证读取端
 * 与宿主字节序无关（大端视图模拟）。
 */
class EndianFieldWriter {
public:
    explicit EndianFieldWriter(bool big_endian)
        : big_endian_(big_endian)
    {
    }

    void u32(std::ofstream& out, std::uint32_t value) const
    {
        if (big_endian_) {
            write_be_u32(out, value);
        } else {
            write_le_u32(out, value);
        }
    }

    void u64(std::ofstream& out, std::uint64_t value) const
    {
        if (big_endian_) {
            write_be_u64(out, value);
        } else {
            write_le_u64(out, value);
        }
    }

    void f32(std::ofstream& out, float value) const
    {
        if (big_endian_) {
            write_be_f32(out, value);
        } else {
            write_le_f32(out, value);
        }
    }

private:
    bool big_endian_ = false;
};

struct LodFileSpec {
    std::uint32_t version = gs3d::data::GS3D_LOD_VERSION;
    std::uint64_t source_point_count = 0;
    std::uint64_t level_count = 1;

    float bbox_min_x = 0.0f;
    float bbox_min_y = 0.0f;
    float bbox_min_z = 0.0f;
    float bbox_max_x = 10.0f;
    float bbox_max_y = 10.0f;
    float bbox_max_z = 10.0f;

    float value_min = 0.0f;
    float value_max = 1.0f;

    std::uint64_t build_finest_target_points = 0;
    std::uint64_t build_growth_factor_x1000 = 0;
    std::uint64_t build_min_points_per_level = 0;
};

struct LodLevelSpec {
    std::uint32_t level_index = 0;
    std::uint32_t voxel_mode = 1; // XY
    std::uint64_t source_point_count = 0;
    std::uint64_t target_point_count = 0;
    std::uint64_t point_count = 0;
    float voxel_size = 1.0f;
};

void write_lod_file(
    const std::filesystem::path& path,
    const LodFileSpec& file_spec,
    const std::vector<LodLevelSpec>& levels,
    const std::vector<std::vector<gs3d::data::Gs3dPoint>>& level_points,
    bool big_endian
)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    const EndianFieldWriter w(big_endian);

    out.write("GS3DLOD\0", 8);
    w.u32(out, file_spec.version);
    w.u32(out, sizeof(gs3d::data::Gs3dLodFileHeader));
    w.u64(out, file_spec.source_point_count);
    w.u64(out, file_spec.level_count);
    w.f32(out, file_spec.bbox_min_x);
    w.f32(out, file_spec.bbox_min_y);
    w.f32(out, file_spec.bbox_min_z);
    w.f32(out, file_spec.bbox_max_x);
    w.f32(out, file_spec.bbox_max_y);
    w.f32(out, file_spec.bbox_max_z);
    w.f32(out, file_spec.value_min);
    w.f32(out, file_spec.value_max);
    w.u64(out, file_spec.build_finest_target_points);
    w.u64(out, file_spec.build_growth_factor_x1000);
    w.u64(out, file_spec.build_min_points_per_level);
    w.u64(out, 0); // reserved

    for (std::size_t i = 0; i < levels.size(); ++i) {
        const auto& level = levels[i];
        w.u32(out, level.level_index);
        w.u32(out, level.voxel_mode);
        w.u64(out, level.source_point_count);
        w.u64(out, level.target_point_count);
        w.u64(out, level.point_count);
        w.u64(out, level.point_count * sizeof(gs3d::data::Gs3dPoint));
        w.f32(out, level.voxel_size);
        w.u32(out, sizeof(gs3d::data::Gs3dLodLevelHeader));
        w.u64(out, 0); // reserved0
        w.u64(out, 0); // reserved1
        w.u64(out, 0); // reserved2

        for (const auto& point : level_points[i]) {
            w.f32(out, point.x);
            w.f32(out, point.y);
            w.f32(out, point.z);
            w.f32(out, point.value);
        }
    }
}

struct TileBundleSpec {
    std::uint32_t index_version = gs3d::data::GS3D_TILE_VERSION;
    std::uint32_t data_version = gs3d::data::GS3D_TILE_VERSION;
    std::uint32_t index_stride = gs3d::data::GS3D_TILE_POINT_STRIDE_V2;
    std::uint32_t data_stride = gs3d::data::GS3D_TILE_POINT_STRIDE_V2;

    std::uint64_t source_point_count = 0;
    std::uint64_t tile_count = 0;
    std::uint64_t total_point_count = 0;
    std::uint64_t total_point_bytes = 0;
};

struct TileRecordSpec {
    std::uint64_t tile_id = 0;
    std::uint32_t tile_x = 0;
    std::uint32_t tile_y = 0;
    std::uint64_t point_count = 0;
    std::uint64_t point_data_offset = 0;
    std::uint64_t point_data_bytes = 0;

    float bbox_min_x = 0.0f;
    float bbox_min_y = 0.0f;
    float bbox_min_z = 0.0f;
    float bbox_max_x = 5.0f;
    float bbox_max_y = 5.0f;
    float bbox_max_z = 5.0f;

    float value_min = 0.0f;
    float value_max = 1.0f;
};

void write_tile_bundle(
    const std::filesystem::path& index_path,
    const std::filesystem::path& data_path,
    const TileBundleSpec& spec,
    const std::vector<TileRecordSpec>& records,
    const std::vector<gs3d::data::Gs3dPointWithId>& points,
    bool big_endian
)
{
    const EndianFieldWriter w(big_endian);

    {
        std::ofstream out(index_path, std::ios::binary | std::ios::trunc);
        out.write("GS3DTIX\0", 8);
        w.u32(out, spec.index_version);
        w.u32(out, sizeof(gs3d::data::Gs3dTileIndexFileHeader));
        w.u64(out, spec.source_point_count);
        w.u64(out, spec.tile_count);
        w.u64(out, spec.total_point_count);
        w.u32(out, spec.index_stride);
        w.u32(out, sizeof(gs3d::data::Gs3dTileRecord));
        w.f32(out, 10.0f);  // tile_size_x
        w.f32(out, 10.0f);  // tile_size_y
        w.f32(out, 0.0f);   // grid_origin_x
        w.f32(out, 0.0f);   // grid_origin_y
        w.u32(out, 2);      // grid_count_x
        w.u32(out, 2);      // grid_count_y
        w.f32(out, 0.0f);
        w.f32(out, 0.0f);
        w.f32(out, 0.0f);
        w.f32(out, 10.0f);
        w.f32(out, 10.0f);
        w.f32(out, 10.0f);
        w.f32(out, 0.0f);   // value_min
        w.f32(out, 1.0f);   // value_max
        w.u32(out, 1);      // split_mode XY
        w.u32(out, 0);      // reserved_u32
        w.u64(out, 0);
        w.u64(out, 0);
        w.u64(out, 0);

        for (const auto& record : records) {
            w.u64(out, record.tile_id);
            w.u32(out, record.tile_x);
            w.u32(out, record.tile_y);
            w.u64(out, record.point_count);
            w.u64(out, record.point_data_offset);
            w.u64(out, record.point_data_bytes);
            w.f32(out, record.bbox_min_x);
            w.f32(out, record.bbox_min_y);
            w.f32(out, record.bbox_min_z);
            w.f32(out, record.bbox_max_x);
            w.f32(out, record.bbox_max_y);
            w.f32(out, record.bbox_max_z);
            w.f32(out, record.value_min);
            w.f32(out, record.value_max);
            w.u64(out, 0); // reserved0
        }
    }

    {
        std::ofstream out(data_path, std::ios::binary | std::ios::trunc);
        out.write("GS3DTIL\0", 8);
        w.u32(out, spec.data_version);
        w.u32(out, sizeof(gs3d::data::Gs3dTileDataFileHeader));
        w.u64(out, spec.source_point_count);
        w.u64(out, spec.tile_count);
        w.u64(out, spec.total_point_count);
        w.u64(out, spec.total_point_bytes);
        w.u32(out, spec.data_stride);
        w.u32(out, 0); // reserved_u32
        w.u64(out, 0);
        w.u64(out, 0);
        w.u64(out, 0);

        for (const auto& point : points) {
            w.f32(out, point.x);
            w.f32(out, point.y);
            w.f32(out, point.z);
            w.f32(out, point.value);
            w.u32(out, point.point_id);
        }
    }
}

[[nodiscard]]
gs3d::data::Gs3dHeader make_source_header(
    std::uint64_t point_count,
    float extent,
    float value_max
)
{
    auto header = gs3d::data::Gs3dFormat::create_empty_header();
    header.point_count = point_count;
    header.bbox_max_x = extent;
    header.bbox_max_y = extent;
    header.bbox_max_z = extent;
    header.value_max = value_max;
    return header;
}

[[nodiscard]]
std::vector<char> read_all_bytes(
    const std::filesystem::path& path
)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<char>(
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>()
    );
}

// ---- LOD v1 废弃 / LOD 字节序测试 -----------------------------------------

void test_lod_v1_is_rejected_with_deprecation_message()
{
    using gs3d::data::Gs3dLodFileHeader;
    using gs3d::data::Gs3dLodFormat;

    // 1) 头部级拒绝：报错信息必须明示 v1 已废弃。
    Gs3dLodFileHeader header;
    header.version = gs3d::data::GS3D_LOD_VERSION_V1;
    header.source_point_count = 100;
    header.level_count = 1;
    header.bbox_max_x = 1.0f;
    header.bbox_max_y = 1.0f;
    header.bbox_max_z = 1.0f;
    header.value_max = 1.0f;

    bool threw = false;
    std::string message;
    try {
        Gs3dLodFormat::validate_file_header(header);
    } catch (const std::runtime_error& error) {
        threw = true;
        message = error.what();
    }
    expect(threw, "v1 LOD header is rejected");
    expect(
        message.find("v1") != std::string::npos &&
            message.find("deprecated") != std::string::npos,
        "v1 rejection message mentions deprecation"
    );

    // 2) 读取级拒绝：真实 v1 文件必须被 reader 显式拒绝。
    LodFileSpec spec;
    spec.version = gs3d::data::GS3D_LOD_VERSION_V1;
    spec.source_point_count = 2;
    spec.level_count = 1;

    LodLevelSpec level;
    level.source_point_count = 2;
    level.target_point_count = 2;
    level.point_count = 2;

    const std::vector<gs3d::data::Gs3dPoint> points = {
        {1.0f, 2.0f, 3.0f, 4.0f},
        {5.0f, 6.0f, 7.0f, 8.0f}
    };

    TemporaryFile file;
    write_lod_file(
        file.path(),
        spec,
        {level},
        {points},
        false
    );

    expect_throws(
        [&] {
            static_cast<void>(
                gs3d::data::Gs3dLodReader::read_without_source_validation(
                    file.path()
                )
            );
        },
        "v1 LOD file is rejected by the reader"
    );
}

void test_lod_anchor_fields_and_round_trip()
{
    constexpr std::uint32_t width = 20;
    constexpr std::uint32_t height = 20;
    std::vector<gs3d::data::Gs3dPoint> points;
    points.reserve(
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height)
    );

    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            points.push_back({
                static_cast<float>(x),
                static_cast<float>(y),
                0.0f,
                static_cast<float>(x + y)
            });
        }
    }

    auto header = make_source_header(
        points.size(),
        static_cast<float>(width - 1),
        static_cast<float>(width + height - 2)
    );
    header.bbox_max_z = 0.0f;

    const gs3d::data::Gs3dDataset dataset(
        header,
        std::move(points),
        {},
        false
    );

    gs3d::data::Gs3dLodBuildConfig config;
    config.finest_target_points = 100;
    config.growth_factor = 1.5f;
    config.min_points_per_level = 5;
    config.voxel_mode = gs3d::data::Gs3dLodVoxelMode::XY;
    config.num_threads = 1;
    config.verbose = false;

    const auto lod =
        gs3d::data::Gs3dLodDataset::build(
            dataset,
            config
        );
    REQUIRE(lod.level_count() > 0);

    TemporaryFile file;
    const auto write_stats =
        gs3d::preprocess::Gs3dLodWriter::write(
            file.path(),
            lod
        );
    expect(write_stats.success, "LOD writer succeeds");

    const auto result =
        gs3d::data::Gs3dLodReader::read(
            file.path(),
            dataset.header()
        );
    expect(result.stats.success, "LOD reader succeeds");
    expect(
        result.file_header.version == gs3d::data::GS3D_LOD_VERSION,
        "round-trip keeps LOD version 2"
    );
    expect(
        result.file_header.build_finest_target_points == 100,
        "anchor build_finest_target_points round-trips"
    );
    expect(
        result.file_header.build_growth_factor_x1000 == 1500,
        "anchor growth_factor 1.5 encodes as 1500 (×1000)"
    );
    expect(
        result.file_header.build_min_points_per_level == 5,
        "anchor build_min_points_per_level round-trips"
    );
    expect(
        result.dataset.level_count() == lod.level_count(),
        "level count round-trips"
    );

    for (std::size_t i = 0; i < lod.level_count(); ++i) {
        const auto& expected = lod.level(i);
        const auto& actual = result.dataset.level(i);
        expect(
            actual.point_count() == expected.point_count(),
            "level point count round-trips"
        );
        expect(
            actual.points[0].x == expected.points[0].x &&
                actual.points[0].y == expected.points[0].y &&
                actual.points[0].z == expected.points[0].z &&
                actual.points[0].value == expected.points[0].value,
            "level first point round-trips"
        );
    }
}

void test_lod_explicit_le_bytes_match_native_on_le_host()
{
    if (std::endian::native != std::endian::little) {
        // 显式小端与原生布局的字节一致性只在小端主机上可验证。
        return;
    }

    constexpr std::uint32_t width = 20;
    constexpr std::uint32_t height = 20;
    std::vector<gs3d::data::Gs3dPoint> points;
    points.reserve(
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height)
    );

    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            points.push_back({
                static_cast<float>(x),
                static_cast<float>(y),
                0.0f,
                static_cast<float>(x + y)
            });
        }
    }

    auto header = make_source_header(
        points.size(),
        static_cast<float>(width - 1),
        static_cast<float>(width + height - 2)
    );
    header.bbox_max_z = 0.0f;

    const gs3d::data::Gs3dDataset dataset(
        header,
        std::move(points),
        {},
        false
    );

    gs3d::data::Gs3dLodBuildConfig config;
    config.finest_target_points = 100;
    config.growth_factor = 1.414f;
    config.min_points_per_level = 5;
    config.voxel_mode = gs3d::data::Gs3dLodVoxelMode::XY;
    config.num_threads = 1;
    config.verbose = false;

    const auto lod =
        gs3d::data::Gs3dLodDataset::build(
            dataset,
            config
        );
    REQUIRE(lod.level_count() > 0);

    TemporaryFile file;
    const auto write_stats =
        gs3d::preprocess::Gs3dLodWriter::write(
            file.path(),
            lod
        );
    REQUIRE(write_stats.success);

    const auto raw = read_all_bytes(file.path());
    REQUIRE(raw.size() >=
            sizeof(gs3d::data::Gs3dLodFileHeader) +
                sizeof(gs3d::data::Gs3dLodLevelHeader));

    // 文件头 96 字节必须与原生 struct 直写一致。
    const auto expected_file_header =
        gs3d::data::Gs3dLodFormat::make_file_header(
            dataset.header(),
            lod.level_count(),
            config
        );
    expect(
        std::memcmp(
            raw.data(),
            &expected_file_header,
            sizeof(expected_file_header)
        ) == 0,
        "LOD file header bytes match native little-endian layout"
    );

    // 首个 level header（紧跟文件头）72 字节必须一致。
    const auto expected_level_header =
        gs3d::data::Gs3dLodFormat::make_level_header(
            lod.level(0)
        );
    expect(
        std::memcmp(
            raw.data() + sizeof(gs3d::data::Gs3dLodFileHeader),
            &expected_level_header,
            sizeof(expected_level_header)
        ) == 0,
        "LOD level header bytes match native little-endian layout"
    );

    // 首个点（level header 之后）16 字节必须一致。
    const auto& first_point = lod.level(0).points[0];
    const std::size_t first_point_offset =
        sizeof(gs3d::data::Gs3dLodFileHeader) +
        sizeof(gs3d::data::Gs3dLodLevelHeader);
    expect(
        std::memcmp(
            raw.data() + first_point_offset,
            &first_point,
            sizeof(first_point)
        ) == 0,
        "LOD point bytes match native little-endian layout"
    );
}

void test_lod_big_endian_file_is_rejected()
{
    /*
     * 大端视图模拟：同一份内容按大端逐字段写出（相当于大端主机原生
     * 直写的产物）。显式小端读取端必须把它当作无效字节序拒绝，而
     * 不是按宿主原生布局误读 —— 证明读取端与宿主字节序无关。
     */
    LodFileSpec spec;
    spec.version = gs3d::data::GS3D_LOD_VERSION;
    spec.source_point_count = 2;
    spec.level_count = 1;
    spec.bbox_max_x = 10.0f;
    spec.bbox_max_y = 10.0f;
    spec.bbox_max_z = 10.0f;
    spec.value_max = 1.0f;
    spec.build_finest_target_points = 100;
    spec.build_growth_factor_x1000 = 1414;
    spec.build_min_points_per_level = 50;

    LodLevelSpec level;
    level.source_point_count = 2;
    level.target_point_count = 2;
    level.point_count = 2;
    level.voxel_size = 1.5f;

    const std::vector<gs3d::data::Gs3dPoint> points = {
        {1.5f, -2.25f, 0.125f, 42.0f},
        {3.5f, 4.5f, 5.5f, 6.5f}
    };

    // 1) 大端写出必须被显式拒绝（version 字段按小端解出为字节交换值）。
    TemporaryFile big_endian_file;
    write_lod_file(
        big_endian_file.path(),
        spec,
        {level},
        {points},
        true
    );

    bool threw = false;
    std::string message;
    try {
        static_cast<void>(
            gs3d::data::Gs3dLodReader::read_without_source_validation(
                big_endian_file.path()
            )
        );
    } catch (const std::runtime_error& error) {
        threw = true;
        message = error.what();
    }
    expect(threw, "big-endian LOD file is rejected");
    expect(
        message.find("unsupported") != std::string::npos,
        "big-endian LOD rejection is an explicit version error"
    );

    // 2) 完全相同的内容按小端写出必须被正确读取（对照组）。
    TemporaryFile little_endian_file;
    write_lod_file(
        little_endian_file.path(),
        spec,
        {level},
        {points},
        false
    );

    const auto result =
        gs3d::data::Gs3dLodReader::read_without_source_validation(
            little_endian_file.path()
        );
    expect(result.stats.success, "little-endian LOD twin reads successfully");
    expect(
        result.file_header.version == gs3d::data::GS3D_LOD_VERSION &&
            result.file_header.build_finest_target_points == 100 &&
            result.file_header.build_growth_factor_x1000 == 1414 &&
            result.file_header.build_min_points_per_level == 50,
        "little-endian LOD twin decodes headers"
    );
    const auto& decoded_points = result.dataset.level(0).points;
    expect(
        decoded_points.size() == 2 &&
            decoded_points[0].x == 1.5f &&
            decoded_points[0].y == -2.25f &&
            decoded_points[0].z == 0.125f &&
            decoded_points[0].value == 42.0f &&
            decoded_points[1].x == 3.5f &&
            decoded_points[1].y == 4.5f &&
            decoded_points[1].z == 5.5f &&
            decoded_points[1].value == 6.5f,
        "little-endian LOD twin decodes points"
    );
}

// ---- tile stride 强校验 / tile 字节序测试 ---------------------------------

void test_tile_record_strict_stride_validation()
{
    using gs3d::data::Gs3dTileFormat;
    using gs3d::data::Gs3dTileRecord;

    Gs3dTileRecord record{};
    record.point_count = 100;
    record.point_data_bytes = 1600; // 100 × 16
    record.bbox_min_x = 0.0f;
    record.bbox_min_y = 0.0f;
    record.bbox_min_z = 0.0f;
    record.bbox_max_x = 1.0f;
    record.bbox_max_y = 1.0f;
    record.bbox_max_z = 1.0f;
    record.value_min = 0.0f;
    record.value_max = 1.0f;

    Gs3dTileFormat::validate_tile_record(record, 16);
    expect(true, "16-byte record passes strict stride 16 validation");

    expect_throws(
        [&] { Gs3dTileFormat::validate_tile_record(record, 20); },
        "16-byte record is rejected against strict stride 20"
    );

    record.point_data_bytes = 2000; // 100 × 20
    Gs3dTileFormat::validate_tile_record(record, 20);
    expect(true, "20-byte record passes strict stride 20 validation");

    expect_throws(
        [&] { Gs3dTileFormat::validate_tile_record(record, 16); },
        "20-byte record is rejected against strict stride 16"
    );
}

void test_tile_stride_version_mismatch_is_rejected()
{
    /*
     * 手工拼装：index/data header 声明 v2（20 字节/点）但 point_stride=16。
     * 旧的校验只检查 stride ∈ {16,20} 与聚合字节数，全部通过后会按
     * 20 字节/点解码，每条记录尾部 4 字节（point_id）静默置零。
     * 新校验要求 stride 与版本严格一致，显式拒绝。
     */
    TileBundleSpec spec;
    spec.source_point_count = 4;
    spec.tile_count = 2;
    spec.total_point_count = 4;
    spec.total_point_bytes = 64; // 4 × 16
    spec.index_stride = gs3d::data::GS3D_TILE_POINT_STRIDE_V1;
    spec.data_stride = gs3d::data::GS3D_TILE_POINT_STRIDE_V1;

    std::vector<TileRecordSpec> records;
    records.push_back({
        0, 0, 0, 2, 80, 32,
        0.0f, 0.0f, 0.0f, 5.0f, 5.0f, 5.0f, 0.0f, 1.0f
    });
    records.push_back({
        1, 1, 0, 2, 112, 32,
        5.0f, 0.0f, 0.0f, 10.0f, 5.0f, 5.0f, 0.0f, 1.0f
    });

    std::vector<gs3d::data::Gs3dPointWithId> points(4);

    TemporaryFile index_file;
    TemporaryFile data_file;
    write_tile_bundle(
        index_file.path(),
        data_file.path(),
        spec,
        records,
        points,
        false
    );

    const auto source = make_source_header(4, 10.0f, 1.0f);
    expect_throws(
        [&] {
            static_cast<void>(gs3d::data::Gs3dTileReader::open(
                index_file.path(),
                data_file.path(),
                source
            ));
        },
        "v2 header with v1 stride is rejected"
    );
}

void test_tile_gap_payload_is_rejected()
{
    /*
     * 手工拼装：两条记录之间留有 40 字节空洞。聚合校验（累计字节数 ==
     * total_point_bytes）无法发现，旧的读取端会从空洞读到零字节。
     * 新校验要求每条记录的数据区间落在 payload 内且相邻记录连续铺满。
     */
    TileBundleSpec spec;
    spec.source_point_count = 4;
    spec.tile_count = 2;
    spec.total_point_count = 4;
    spec.total_point_bytes = 80; // 4 × 20

    std::vector<TileRecordSpec> records;
    records.push_back({
        0, 0, 0, 2, 80, 40,
        0.0f, 0.0f, 0.0f, 5.0f, 5.0f, 5.0f, 0.0f, 1.0f
    });
    // 第二条记录起点后移 40 字节（空洞），终点越过 payload 上界。
    records.push_back({
        1, 1, 0, 2, 160, 40,
        5.0f, 0.0f, 0.0f, 10.0f, 5.0f, 5.0f, 0.0f, 1.0f
    });

    std::vector<gs3d::data::Gs3dPointWithId> points(4);

    TemporaryFile index_file;
    TemporaryFile data_file;
    write_tile_bundle(
        index_file.path(),
        data_file.path(),
        spec,
        records,
        points,
        false
    );

    const auto source = make_source_header(4, 10.0f, 1.0f);
    expect_throws(
        [&] {
            static_cast<void>(gs3d::data::Gs3dTileReader::open(
                index_file.path(),
                data_file.path(),
                source
            ));
        },
        "tile payload with a gap is rejected"
    );
}

void test_tile_writer_reader_round_trip_and_le_bytes()
{
    constexpr std::uint32_t width = 20;
    constexpr std::uint32_t height = 20;
    std::vector<gs3d::data::Gs3dPoint> points;
    points.reserve(
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height)
    );

    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            points.push_back({
                static_cast<float>(x),
                static_cast<float>(y),
                0.0f,
                static_cast<float>(x + y)
            });
        }
    }

    auto header = make_source_header(
        points.size(),
        static_cast<float>(width - 1),
        static_cast<float>(width + height - 2)
    );
    header.bbox_max_z = 0.0f;

    const gs3d::data::Gs3dDataset dataset(
        header,
        std::move(points),
        {},
        false
    );

    gs3d::preprocess::Gs3dTileWriteConfig tile_config;
    tile_config.tile_size_x = 10.0f;
    tile_config.tile_size_y = 10.0f;
    tile_config.num_threads = 1;
    tile_config.verbose = false;

    TemporaryFile index_file;
    TemporaryFile data_file;
    const auto stats = gs3d::preprocess::Gs3dTileWriter::write(
        index_file.path(),
        data_file.path(),
        dataset,
        tile_config
    );
    REQUIRE(stats.success);
    REQUIRE(stats.tile_count > 0);

    const auto reader = gs3d::data::Gs3dTileReader::open(
        index_file.path(),
        data_file.path(),
        dataset.header()
    );
    REQUIRE(reader.valid());
    REQUIRE(reader.tile_count() == stats.tile_count);
    REQUIRE(reader.total_point_count() == dataset.point_count());
    REQUIRE(reader.has_embedded_point_ids());

    // 全量回读并与源数据比对。
    std::size_t decoded_point_count = 0;
    for (std::uint64_t tile_id = 0;
         tile_id < reader.tile_count();
         ++tile_id) {
        const auto block = reader.read_tile_points_with_ids(tile_id);
        expect(
            block.point_ids.size() == block.points.size(),
            "tile round-trip returns matching ids"
        );
        for (std::size_t i = 0; i < block.points.size(); ++i) {
            const auto& source_point =
                dataset.points()[block.point_ids[i] - 1];
            expect(
                block.points[i].x == source_point.x &&
                    block.points[i].y == source_point.y &&
                    block.points[i].z == source_point.z &&
                    block.points[i].value == source_point.value,
                "tile round-trip point matches source by embedded id"
            );
        }
        decoded_point_count += block.points.size();
    }
    expect(
        decoded_point_count == dataset.point_count(),
        "tile round-trip decodes every source point"
    );

    if (std::endian::native != std::endian::little) {
        return;
    }

    // 字节级一致性：显式小端输出必须与原生 struct 直写一致（小端主机）。
    const auto raw_index = read_all_bytes(index_file.path());
    const auto raw_data = read_all_bytes(data_file.path());

    const auto expected_index_header =
        gs3d::data::Gs3dTileFormat::make_index_file_header(
            dataset.header(),
            stats.tile_count,
            stats.total_points,
            stats.tile_size_x,
            stats.tile_size_y,
            dataset.bbox_min_x(),
            dataset.bbox_min_y(),
            stats.grid_count_x,
            stats.grid_count_y
        );
    expect(
        std::memcmp(
            raw_index.data(),
            &expected_index_header,
            sizeof(expected_index_header)
        ) == 0,
        "tile index header bytes match native little-endian layout"
    );

    const auto expected_data_header =
        gs3d::data::Gs3dTileFormat::make_data_file_header(
            dataset.header(),
            stats.tile_count,
            stats.total_points
        );
    expect(
        std::memcmp(
            raw_data.data(),
            &expected_data_header,
            sizeof(expected_data_header)
        ) == 0,
        "tile data header bytes match native little-endian layout"
    );

    for (std::size_t i = 0; i < reader.records().size(); ++i) {
        const auto& record = reader.records()[i];
        const std::size_t record_offset =
            sizeof(gs3d::data::Gs3dTileIndexFileHeader) +
            i * sizeof(gs3d::data::Gs3dTileRecord);
        expect(
            std::memcmp(
                raw_index.data() + record_offset,
                &record,
                sizeof(record)
            ) == 0,
            "tile record bytes match native little-endian layout"
        );
    }

    const auto first_block = reader.read_tile_points_with_ids(0);
    REQUIRE(first_block.points.size() > 0);
    const gs3d::data::Gs3dPointWithId first_embedded{
        first_block.points[0].x,
        first_block.points[0].y,
        first_block.points[0].z,
        first_block.points[0].value,
        first_block.point_ids[0]
    };
    expect(
        std::memcmp(
            raw_data.data() + sizeof(gs3d::data::Gs3dTileDataFileHeader),
            &first_embedded,
            sizeof(first_embedded)
        ) == 0,
        "tile point bytes match native little-endian layout"
    );
}

void test_tile_big_endian_bundle_is_rejected()
{
    /*
     * 大端视图模拟：整包按大端逐字段写出（相当于大端主机原生直写的
     * 产物）。显式小端读取端必须拒绝，同一内容按小端写出则必须正确
     * 解码 —— 证明 tile 读写与宿主字节序无关。
     */
    TileBundleSpec spec;
    spec.source_point_count = 4;
    spec.tile_count = 2;
    spec.total_point_count = 4;
    spec.total_point_bytes = 80; // 4 × 20

    std::vector<TileRecordSpec> records;
    records.push_back({
        0, 0, 0, 2, 80, 40,
        0.0f, 0.0f, 0.0f, 5.0f, 5.0f, 5.0f, 0.0f, 1.0f
    });
    records.push_back({
        1, 1, 0, 2, 120, 40,
        5.0f, 0.0f, 0.0f, 10.0f, 5.0f, 5.0f, 0.0f, 1.0f
    });

    const std::vector<gs3d::data::Gs3dPointWithId> points = {
        {1.0f, 2.0f, 3.0f, 4.0f, 11},
        {5.0f, 6.0f, 7.0f, 8.0f, 12},
        {9.0f, 10.0f, 11.0f, 12.0f, 13},
        {13.0f, 14.0f, 15.0f, 16.0f, 14}
    };

    const auto source = make_source_header(4, 10.0f, 1.0f);

    // 1) 大端写出必须被显式拒绝。
    TemporaryFile big_endian_index;
    TemporaryFile big_endian_data;
    write_tile_bundle(
        big_endian_index.path(),
        big_endian_data.path(),
        spec,
        records,
        points,
        true
    );

    expect_throws(
        [&] {
            static_cast<void>(gs3d::data::Gs3dTileReader::open(
                big_endian_index.path(),
                big_endian_data.path(),
                source
            ));
        },
        "big-endian tile bundle is rejected"
    );

    // 2) 完全相同的内容按小端写出必须被正确读取（对照组）。
    TemporaryFile little_endian_index;
    TemporaryFile little_endian_data;
    write_tile_bundle(
        little_endian_index.path(),
        little_endian_data.path(),
        spec,
        records,
        points,
        false
    );

    const auto reader = gs3d::data::Gs3dTileReader::open(
        little_endian_index.path(),
        little_endian_data.path(),
        source
    );
    expect(reader.valid(), "little-endian tile twin opens");
    expect(
        reader.total_point_count() == 4 &&
            reader.tile_count() == 2 &&
            reader.has_embedded_point_ids(),
        "little-endian tile twin decodes headers"
    );

    const auto first_block = reader.read_tile_points_with_ids(0);
    expect(
        first_block.points.size() == 2 &&
            first_block.point_ids.size() == 2,
        "little-endian tile twin decodes block"
    );
    expect(
        first_block.points[0].x == 1.0f &&
            first_block.points[0].y == 2.0f &&
            first_block.points[0].z == 3.0f &&
            first_block.points[0].value == 4.0f &&
            first_block.point_ids[0] == 11,
        "little-endian tile twin first point decodes"
    );
    expect(
        first_block.points[1].value == 8.0f &&
            first_block.point_ids[1] == 12,
        "little-endian tile twin second point decodes"
    );

    const auto second_block = reader.read_tile_points_with_ids(1);
    expect(
        second_block.points[0].value == 12.0f &&
            second_block.point_ids[0] == 13,
        "little-endian tile twin tile-1 point decodes"
    );
}

} // namespace

#define LEGACY_TEST_CASE(test_function) \
    TEST_CASE(#test_function, "[data_format]") { test_function(); }

    LEGACY_TEST_CASE(test_valid_dataset_round_trip)
    LEGACY_TEST_CASE(test_truncated_dataset_is_rejected)
    LEGACY_TEST_CASE(test_overflowing_header_is_rejected)
    LEGACY_TEST_CASE(test_metadata_only_dataset)
    LEGACY_TEST_CASE(test_chunk_plan_has_no_overlap_at_record_boundary)
    LEGACY_TEST_CASE(test_dat_schema_maps_field_statics)
    LEGACY_TEST_CASE(test_csv_and_dat_conversion_preserve_field_semantics_and_precision)
    LEGACY_TEST_CASE(test_lod_ratios_empty_falls_back_to_explicit_counts)
    LEGACY_TEST_CASE(test_lod_ratios_scale_with_source_point_count)
    LEGACY_TEST_CASE(test_lod_ratios_same_ratios_give_different_counts_for_smaller_source)
    LEGACY_TEST_CASE(test_lod_ratios_negative_ratio_clamped_to_zero)
    LEGACY_TEST_CASE(test_lod_ratios_tiny_ratio_floors_to_one_point)
    LEGACY_TEST_CASE(test_tile_format_v2_has_embedded_point_ids)
    LEGACY_TEST_CASE(test_tile_format_v1_no_embedded_point_ids)
    LEGACY_TEST_CASE(test_point_with_id_struct_size)
    LEGACY_TEST_CASE(test_tile_format_v2_stride_is_20)
    LEGACY_TEST_CASE(test_tile_format_v1_stride_is_16)
    LEGACY_TEST_CASE(test_tile_format_accepts_both_strides)
    LEGACY_TEST_CASE(test_tile_format_v1_and_v2_are_supported_versions)
    LEGACY_TEST_CASE(test_tile_record_validates_both_strides)
    LEGACY_TEST_CASE(test_tile_format_stride_for_unknown_version_throws)
    LEGACY_TEST_CASE(test_parallel_lod_matches_serial_output)
    LEGACY_TEST_CASE(test_lod_v1_is_rejected_with_deprecation_message)
    LEGACY_TEST_CASE(test_lod_anchor_fields_and_round_trip)
    LEGACY_TEST_CASE(test_lod_explicit_le_bytes_match_native_on_le_host)
    LEGACY_TEST_CASE(test_lod_big_endian_file_is_rejected)
    LEGACY_TEST_CASE(test_tile_record_strict_stride_validation)
    LEGACY_TEST_CASE(test_tile_stride_version_mismatch_is_rejected)
    LEGACY_TEST_CASE(test_tile_gap_payload_is_rejected)
    LEGACY_TEST_CASE(test_tile_writer_reader_round_trip_and_le_bytes)
    LEGACY_TEST_CASE(test_tile_big_endian_bundle_is_rejected)

#undef LEGACY_TEST_CASE
