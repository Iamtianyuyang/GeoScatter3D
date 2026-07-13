#include "data/CsvChunkPlanner.hpp"
#include "data/CsvSniffer.hpp"
#include "data/DataSchema.hpp"
#include "data/Gs3dDataset.hpp"
#include "data/Gs3dFormat.hpp"
#include "data/Gs3dLodTargets.hpp"
#include "data/Gs3dReader.hpp"
#include "data/Gs3dTileFormat.hpp"
#include "preprocess/CsvToGs3dConverter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
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

#undef LEGACY_TEST_CASE
