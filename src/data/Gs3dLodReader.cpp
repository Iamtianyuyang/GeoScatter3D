#include "data/Gs3dLodReader.hpp"
#include "util/Log.hpp"

#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gs3d::data {

namespace {

std::vector<Gs3dPoint> read_points(
    std::ifstream& file,
    std::uint64_t point_count,
    std::uint64_t point_data_bytes
) {
    if (point_count == 0) {
        throw std::runtime_error(
            "Gs3dLodReader: point_count is zero"
        );
    }

    const std::uint64_t expected_bytes =
        point_count *
        static_cast<std::uint64_t>(sizeof(Gs3dPoint));

    if (point_data_bytes != expected_bytes) {
        throw std::runtime_error(
            "Gs3dLodReader: point_data_bytes mismatch"
        );
    }

    if (point_count >
        static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max()
        )) {
        throw std::runtime_error(
            "Gs3dLodReader: point_count exceeds size_t range"
        );
    }

    std::vector<Gs3dPoint> points(
        static_cast<std::size_t>(point_count)
    );

    Gs3dLodFormat::read_points(
        file,
        points,
        "Gs3dLodReader: failed to read point data"
    );

    return points;
}

[[nodiscard]]
std::uint64_t file_size_or_zero(
    const std::filesystem::path& path
) {
    std::error_code ec;

    const auto size =
        std::filesystem::file_size(path, ec);

    if (ec) {
        return 0;
    }

    return static_cast<std::uint64_t>(size);
}

Gs3dHeader make_source_header_from_lod_header(
    const Gs3dLodFileHeader& header
) {
    Gs3dHeader source_header{};

    source_header.point_count =
        header.source_point_count;

    source_header.bbox_min_x = header.bbox_min_x;
    source_header.bbox_min_y = header.bbox_min_y;
    source_header.bbox_min_z = header.bbox_min_z;

    source_header.bbox_max_x = header.bbox_max_x;
    source_header.bbox_max_y = header.bbox_max_y;
    source_header.bbox_max_z = header.bbox_max_z;

    source_header.value_min = header.value_min;
    source_header.value_max = header.value_max;

    return source_header;
}

} // namespace

Gs3dLodReadResult Gs3dLodReader::read(
    const std::filesystem::path& path,
    const Gs3dHeader& source_header,
    const Gs3dLodReadConfig& config
) {
    if (path.empty()) {
        throw std::runtime_error(
            "Gs3dLodReader: input path is empty"
        );
    }

    std::ifstream file(
        path,
        std::ios::binary
    );

    if (!file.is_open()) {
        throw std::runtime_error(
            "Gs3dLodReader: failed to open input file: " +
            path.string()
        );
    }

    Gs3dLodFileHeader file_header{};
    Gs3dLodFormat::read_file_header(
        file,
        file_header,
        "Gs3dLodReader: failed to read file header"
    );

    Gs3dLodFormat::validate_file_header(
        file_header
    );

    if (config.validate_against_source) {
        Gs3dLodFormat::validate_against_source(
            file_header,
            source_header
        );
    }

    std::vector<Gs3dLodLevel> levels;

    if (file_header.level_count >
        static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max()
        )) {
        throw std::runtime_error(
            "Gs3dLodReader: level_count exceeds size_t range"
        );
    }

    levels.reserve(
        static_cast<std::size_t>(
            file_header.level_count
        )
    );

    Gs3dLodReadStats stats;
    stats.path = path;
    stats.level_count = file_header.level_count;

    for (std::uint64_t i = 0;
         i < file_header.level_count;
         ++i) {
        Gs3dLodLevelHeader level_header{};
        Gs3dLodFormat::read_level_header(
            file,
            level_header,
            "Gs3dLodReader: failed to read level header"
        );

        Gs3dLodFormat::validate_level_header(
            level_header
        );

        Gs3dLodLevel level;
        level.name =
            "lod_" +
            std::to_string(level_header.level_index);

        level.level_index =
            level_header.level_index;

        level.source_point_count =
            level_header.source_point_count;

        level.target_point_count =
            level_header.target_point_count;

        level.voxel_size =
            level_header.voxel_size;

        level.voxel_mode =
            Gs3dLodFormat::from_format_voxel_mode(
                level_header.voxel_mode
            );

        level.points =
            read_points(
                file,
                level_header.point_count,
                level_header.point_data_bytes
            );

        stats.total_points += level.point_count();
        stats.total_point_bytes += level.point_bytes();

        levels.push_back(std::move(level));
    }

    file.close();

    stats.file_bytes =
        file_size_or_zero(path);

    stats.success = true;

    Gs3dLodReadResult result;
    result.file_header = file_header;
    result.stats = stats;

    result.dataset =
        Gs3dLodDataset::from_levels(
            source_header,
            path,
            std::move(levels)
        );

    if (config.verbose) {
        gs3d::util::log::info() << Gs3dLodFormat::file_header_summary(
            result.file_header
        );

        gs3d::util::log::info() << result.dataset.summary();
    }

    return result;
}

Gs3dLodReadResult Gs3dLodReader::read_without_source_validation(
    const std::filesystem::path& path,
    const Gs3dLodReadConfig& config
) {
    if (path.empty()) {
        throw std::runtime_error(
            "Gs3dLodReader: input path is empty"
        );
    }

    std::ifstream file(
        path,
        std::ios::binary
    );

    if (!file.is_open()) {
        throw std::runtime_error(
            "Gs3dLodReader: failed to open input file: " +
            path.string()
        );
    }

    Gs3dLodFileHeader file_header{};
    Gs3dLodFormat::read_file_header(
        file,
        file_header,
        "Gs3dLodReader: failed to read file header"
    );

    Gs3dLodFormat::validate_file_header(
        file_header
    );

    file.close();

    auto source_header =
        make_source_header_from_lod_header(
            file_header
        );

    Gs3dLodReadConfig local_config = config;
    local_config.validate_against_source = false;

    return read(
        path,
        source_header,
        local_config
    );
}

} // namespace gs3d::data
