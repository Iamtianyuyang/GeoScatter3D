#include "preprocess/Gs3dLodWriter.hpp"

#include "data/Gs3dLodFormat.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace gs3d::preprocess {

namespace {

using gs3d::data::Gs3dLodDataset;
using gs3d::data::Gs3dLodFormat;

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

} // namespace

Gs3dLodWriteStats Gs3dLodWriter::write(
    const std::filesystem::path& path,
    const Gs3dLodDataset& lod_dataset
) {
    if (path.empty()) {
        throw std::runtime_error(
            "Gs3dLodWriter: output path is empty"
        );
    }

    if (lod_dataset.empty()) {
        throw std::runtime_error(
            "Gs3dLodWriter: LOD dataset is empty"
        );
    }

    const auto parent_path =
        path.parent_path();

    if (!parent_path.empty()) {
        std::error_code ec;

        std::filesystem::create_directories(
            parent_path,
            ec
        );

        if (ec) {
            throw std::runtime_error(
                "Gs3dLodWriter: failed to create output directory: " +
                parent_path.string()
            );
        }
    }

    std::ofstream file(
        path,
        std::ios::binary | std::ios::trunc
    );

    if (!file.is_open()) {
        throw std::runtime_error(
            "Gs3dLodWriter: failed to open output file: " +
            path.string()
        );
    }

    const auto file_header =
        Gs3dLodFormat::make_file_header(
            lod_dataset.source_header(),
            static_cast<std::uint64_t>(
                lod_dataset.level_count()
            ),
            lod_dataset.build_config()
        );

    Gs3dLodFormat::validate_file_header(
        file_header
    );

    Gs3dLodFormat::write_file_header(
        file,
        file_header
    );

    if (!file.good()) {
        throw std::runtime_error(
            "Gs3dLodWriter: failed to write file header"
        );
    }

    Gs3dLodWriteStats stats;
    stats.path = path;
    stats.level_count =
        static_cast<std::uint64_t>(
            lod_dataset.level_count()
        );

    for (const auto& level : lod_dataset.levels()) {
        if (level.empty()) {
            throw std::runtime_error(
                "Gs3dLodWriter: LOD level is empty: " +
                level.name
            );
        }

        const auto level_header =
            Gs3dLodFormat::make_level_header(
                level
            );

        Gs3dLodFormat::validate_level_header(
            level_header
        );

        Gs3dLodFormat::write_level_header(
            file,
            level_header
        );

        if (!file.good()) {
            throw std::runtime_error(
                "Gs3dLodWriter: failed to write level header"
            );
        }

        Gs3dLodFormat::write_points(
            file,
            level.points
        );

        if (!file.good()) {
            throw std::runtime_error(
                "Gs3dLodWriter: failed to write point data"
            );
        }

        stats.total_points += level.point_count();
        stats.total_point_bytes += level.point_bytes();
    }

    file.flush();

    if (!file.good()) {
        throw std::runtime_error(
            "Gs3dLodWriter: failed to flush output file"
        );
    }

    file.close();

    stats.total_file_bytes =
        file_size_or_zero(path);

    stats.success = true;

    return stats;
}

} // namespace gs3d::preprocess