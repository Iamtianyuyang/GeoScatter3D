#include "preprocess/Gs3dLodWriter.hpp"

#include "data/Gs3dLodFormat.hpp"

#include <fstream>
#include <stdexcept>

namespace gs3d::preprocess {

namespace {

using gs3d::data::Gs3dLodDataset;
using gs3d::data::Gs3dLodFormat;
using gs3d::data::Gs3dPoint;

template <typename T>
void write_binary(
    std::ofstream& file,
    const T& value,
    const char* error_message
) {
    file.write(
        reinterpret_cast<const char*>(&value),
        static_cast<std::streamsize>(sizeof(T))
    );

    if (!file.good()) {
        throw std::runtime_error(error_message);
    }
}

void write_points(
    std::ofstream& file,
    const std::vector<Gs3dPoint>& points
) {
    if (points.empty()) {
        throw std::runtime_error(
            "Gs3dLodWriter: cannot write empty point array"
        );
    }

    const auto byte_count =
        static_cast<std::streamsize>(
            points.size() * sizeof(Gs3dPoint)
        );

    file.write(
        reinterpret_cast<const char*>(points.data()),
        byte_count
    );

    if (!file.good()) {
        throw std::runtime_error(
            "Gs3dLodWriter: failed to write point data"
        );
    }
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
            )
        );

    Gs3dLodFormat::validate_file_header(
        file_header
    );

    write_binary(
        file,
        file_header,
        "Gs3dLodWriter: failed to write file header"
    );

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

        write_binary(
            file,
            level_header,
            "Gs3dLodWriter: failed to write level header"
        );

        write_points(
            file,
            level.points
        );

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