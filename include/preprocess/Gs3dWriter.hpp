#pragma once

#include "data/CsvStreamReader.hpp"
#include "data/Gs3dFormat.hpp"
#include "preprocess/StatisticsPass.hpp"

#include <cstdint>
#include <filesystem>

namespace gs3d::preprocess {

struct Gs3dWriteResult {
    std::uint64_t expected_points = 0;
    std::uint64_t written_points = 0;
    std::uint64_t invalid_records = 0;
    std::uint64_t output_file_size = 0;
};

class Gs3dWriter {
public:
    explicit Gs3dWriter(gs3d::data::CsvReadConfig config = {});

    [[nodiscard]]
    Gs3dWriteResult write(
        const std::filesystem::path& csv_path,
        const std::filesystem::path& output_path,
        const StatisticsResult& statistics
    ) const;

private:
    gs3d::data::CsvReadConfig config_;

private:
    [[nodiscard]]
    Gs3dWriteResult write_sequential(
        const std::filesystem::path& csv_path,
        const std::filesystem::path& output_path,
        const StatisticsResult& statistics
    ) const;

    [[nodiscard]]
    Gs3dWriteResult write_parallel(
        const std::filesystem::path& csv_path,
        const std::filesystem::path& output_path,
        const StatisticsResult& statistics
    ) const;

    [[nodiscard]]
    static gs3d::data::Gs3dHeader build_header(
        const StatisticsResult& statistics
    );

    [[nodiscard]]
    static gs3d::data::Gs3dPoint make_point(
        const gs3d::data::CsvPointRecord& record,
        const StatisticsResult& statistics
    );
};

} // namespace gs3d::preprocess
