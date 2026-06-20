#include "data/Gs3dDataset.hpp"
#include "data/Gs3dFormat.hpp"
#include "data/Gs3dLodTargets.hpp"
#include "data/Gs3dReader.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, std::string_view name)
{
    if (!condition) {
        std::cerr << "[FAIL] " << name << '\n';
        ++failures;
    }
}

template <typename Function>
void expect_throws(Function&& function, std::string_view name)
{
    try {
        function();
        std::cerr << "[FAIL] " << name << " did not throw\n";
        ++failures;
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
    out.write(
        reinterpret_cast<const char*>(&header),
        static_cast<std::streamsize>(sizeof(header))
    );
    if (point != nullptr) {
        out.write(
            reinterpret_cast<const char*>(point),
            static_cast<std::streamsize>(sizeof(*point))
        );
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

} // namespace

int main()
{
    test_valid_dataset_round_trip();
    test_truncated_dataset_is_rejected();
    test_overflowing_header_is_rejected();
    test_metadata_only_dataset();
    test_lod_ratios_empty_falls_back_to_explicit_counts();
    test_lod_ratios_scale_with_source_point_count();
    test_lod_ratios_same_ratios_give_different_counts_for_smaller_source();
    test_lod_ratios_negative_ratio_clamped_to_zero();
    test_lod_ratios_tiny_ratio_floors_to_one_point();

    if (failures == 0) {
        std::cout << "[PASS] GS3D format tests\n";
    }
    return failures == 0 ? 0 : 1;
}
