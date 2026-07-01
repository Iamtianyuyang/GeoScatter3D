#include "data/Gs3dLodFormat.hpp"

#include <cmath>
#include <sstream>
#include <stdexcept>

namespace gs3d::data {

bool Gs3dLodFormat::is_valid_magic(
    const std::array<char, 8>& magic
) noexcept {
    return magic == GS3D_LOD_MAGIC;
}

bool Gs3dLodFormat::is_supported_version(
    std::uint32_t version
) noexcept {
    return version == GS3D_LOD_VERSION;
}

Gs3dLodFileHeader Gs3dLodFormat::make_file_header(
    const Gs3dHeader& source_header,
    std::uint64_t level_count
) noexcept {
    Gs3dLodFileHeader header;

    header.magic = GS3D_LOD_MAGIC;
    header.version = GS3D_LOD_VERSION;
    header.header_size =
        static_cast<std::uint32_t>(sizeof(Gs3dLodFileHeader));

    header.source_point_count = source_header.point_count;
    header.level_count = level_count;

    header.bbox_min_x = source_header.bbox_min_x;
    header.bbox_min_y = source_header.bbox_min_y;
    header.bbox_min_z = source_header.bbox_min_z;

    header.bbox_max_x = source_header.bbox_max_x;
    header.bbox_max_y = source_header.bbox_max_y;
    header.bbox_max_z = source_header.bbox_max_z;

    header.value_min = source_header.value_min;
    header.value_max = source_header.value_max;

    return header;
}

Gs3dLodLevelHeader Gs3dLodFormat::make_level_header(
    const Gs3dLodLevel& level
) {
    Gs3dLodLevelHeader header;

    header.level_index = level.level_index;
    header.voxel_mode =
        static_cast<std::uint32_t>(
            to_format_voxel_mode(level.voxel_mode)
        );

    header.source_point_count = level.source_point_count;
    header.target_point_count = level.target_point_count;
    header.point_count = level.point_count();
    header.point_data_bytes = level.point_bytes();

    header.voxel_size = level.voxel_size;
    header.level_header_size =
        static_cast<std::uint32_t>(sizeof(Gs3dLodLevelHeader));

    validate_level_header(header);

    return header;
}

Gs3dLodFormatVoxelMode Gs3dLodFormat::to_format_voxel_mode(
    Gs3dLodVoxelMode mode
) {
    switch (mode) {
    case Gs3dLodVoxelMode::XY:
        return Gs3dLodFormatVoxelMode::XY;

    case Gs3dLodVoxelMode::XYZ:
        return Gs3dLodFormatVoxelMode::XYZ;

    default:
        throw std::runtime_error(
            "Gs3dLodFormat: unsupported Gs3dLodVoxelMode"
        );
    }
}

Gs3dLodVoxelMode Gs3dLodFormat::from_format_voxel_mode(
    std::uint32_t mode
) {
    const auto typed_mode =
        static_cast<Gs3dLodFormatVoxelMode>(mode);

    switch (typed_mode) {
    case Gs3dLodFormatVoxelMode::XY:
        return Gs3dLodVoxelMode::XY;

    case Gs3dLodFormatVoxelMode::XYZ:
        return Gs3dLodVoxelMode::XYZ;

    default:
        throw std::runtime_error(
            "Gs3dLodFormat: unsupported file voxel mode"
        );
    }
}

const char* Gs3dLodFormat::format_voxel_mode_name(
    std::uint32_t mode
) noexcept {
    const auto typed_mode =
        static_cast<Gs3dLodFormatVoxelMode>(mode);

    switch (typed_mode) {
    case Gs3dLodFormatVoxelMode::XY:
        return "XY";

    case Gs3dLodFormatVoxelMode::XYZ:
        return "XYZ";

    default:
        return "Unknown";
    }
}

void Gs3dLodFormat::validate_file_header(
    const Gs3dLodFileHeader& header
) {
    if (!is_valid_magic(header.magic)) {
        throw std::runtime_error(
            "Gs3dLodFormat: invalid .gs3dlod magic"
        );
    }

    if (!is_supported_version(header.version)) {
        throw std::runtime_error(
            "Gs3dLodFormat: unsupported .gs3dlod version"
        );
    }

    if (header.header_size != sizeof(Gs3dLodFileHeader)) {
        throw std::runtime_error(
            "Gs3dLodFormat: invalid file header size"
        );
    }

    if (header.source_point_count == 0) {
        throw std::runtime_error(
            "Gs3dLodFormat: source_point_count is zero"
        );
    }

    if (header.level_count == 0) {
        throw std::runtime_error(
            "Gs3dLodFormat: level_count is zero"
        );
    }

    if (header.bbox_max_x < header.bbox_min_x ||
        header.bbox_max_y < header.bbox_min_y ||
        header.bbox_max_z < header.bbox_min_z) {
        throw std::runtime_error(
            "Gs3dLodFormat: invalid source bounding box"
        );
    }

    if (header.value_max < header.value_min) {
        throw std::runtime_error(
            "Gs3dLodFormat: invalid source value range"
        );
    }
}

void Gs3dLodFormat::validate_level_header(
    const Gs3dLodLevelHeader& header
) {
    if (header.level_header_size != sizeof(Gs3dLodLevelHeader)) {
        throw std::runtime_error(
            "Gs3dLodFormat: invalid level header size"
        );
    }

    if (header.source_point_count == 0) {
        throw std::runtime_error(
            "Gs3dLodFormat: level source_point_count is zero"
        );
    }

    if (header.target_point_count == 0) {
        throw std::runtime_error(
            "Gs3dLodFormat: level target_point_count is zero"
        );
    }

    if (header.point_count == 0) {
        throw std::runtime_error(
            "Gs3dLodFormat: level point_count is zero"
        );
    }

    if (header.point_data_bytes !=
        header.point_count * sizeof(Gs3dPoint)) {
        throw std::runtime_error(
            "Gs3dLodFormat: level point_data_bytes mismatch"
        );
    }

    if (header.voxel_size < 0.0f) {
        throw std::runtime_error(
            "Gs3dLodFormat: level voxel_size is negative"
        );
    }

    (void) from_format_voxel_mode(header.voxel_mode);
}

void Gs3dLodFormat::validate_against_source(
    const Gs3dLodFileHeader& lod_header,
    const Gs3dHeader& source_header
) {
    validate_file_header(lod_header);

    if (lod_header.source_point_count != source_header.point_count) {
        throw std::runtime_error(
            "Gs3dLodFormat: source point count mismatch"
        );
    }

    if (!approximately_equal(lod_header.bbox_min_x, source_header.bbox_min_x) ||
        !approximately_equal(lod_header.bbox_min_y, source_header.bbox_min_y) ||
        !approximately_equal(lod_header.bbox_min_z, source_header.bbox_min_z) ||
        !approximately_equal(lod_header.bbox_max_x, source_header.bbox_max_x) ||
        !approximately_equal(lod_header.bbox_max_y, source_header.bbox_max_y) ||
        !approximately_equal(lod_header.bbox_max_z, source_header.bbox_max_z)) {
        throw std::runtime_error(
            "Gs3dLodFormat: source bounding box mismatch"
        );
    }

    if (!approximately_equal(lod_header.value_min, source_header.value_min) ||
        !approximately_equal(lod_header.value_max, source_header.value_max)) {
        throw std::runtime_error(
            "Gs3dLodFormat: source value range mismatch"
        );
    }
}

bool Gs3dLodFormat::approximately_equal(
    float a,
    float b,
    float epsilon
) noexcept {
    return std::abs(a - b) <= epsilon;
}

std::string Gs3dLodFormat::file_header_summary(
    const Gs3dLodFileHeader& header
) {
    std::ostringstream oss;

    oss << "Gs3dLodFileHeader\n";
    oss << "  magic: ";

    for (const char c : header.magic) {
        if (c == '\0') {
            break;
        }

        oss << c;
    }

    oss << '\n';

    oss << "  version: "
        << header.version
        << '\n';

    oss << "  header_size: "
        << header.header_size
        << '\n';

    oss << "  source_point_count: "
        << header.source_point_count
        << '\n';

    oss << "  level_count: "
        << header.level_count
        << '\n';

    oss << "  bbox_min: ["
        << header.bbox_min_x << ", "
        << header.bbox_min_y << ", "
        << header.bbox_min_z << "]\n";

    oss << "  bbox_max: ["
        << header.bbox_max_x << ", "
        << header.bbox_max_y << ", "
        << header.bbox_max_z << "]\n";

    oss << "  value_range: ["
        << header.value_min << ", "
        << header.value_max << "]\n";

    if (header.version >= 2) {
        oss << "  build_finest_target_points: "
            << header.build_finest_target_points
            << '\n';
        oss << "  build_growth_factor_x1000: "
            << header.build_growth_factor_x1000
            << '\n';
        oss << "  build_min_points_per_level: "
            << header.build_min_points_per_level
            << '\n';
    }

    return oss.str();
}

std::string Gs3dLodFormat::level_header_summary(
    const Gs3dLodLevelHeader& header
) {
    std::ostringstream oss;

    oss << "Gs3dLodLevelHeader\n";
    oss << "  level_index: "
        << header.level_index
        << '\n';

    oss << "  voxel_mode: "
        << format_voxel_mode_name(header.voxel_mode)
        << '\n';

    oss << "  source_point_count: "
        << header.source_point_count
        << '\n';

    oss << "  target_point_count: "
        << header.target_point_count
        << '\n';

    oss << "  point_count: "
        << header.point_count
        << '\n';

    oss << "  point_data_bytes: "
        << header.point_data_bytes
        << '\n';

    oss << "  voxel_size: "
        << header.voxel_size
        << '\n';

    oss << "  level_header_size: "
        << header.level_header_size
        << '\n';

    return oss.str();
}

} // namespace gs3d::data