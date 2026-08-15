#include "data/Gs3dLodFormat.hpp"

#include "data/Gs3dByteOrder.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <istream>
#include <ostream>
#include <sstream>
#include <stdexcept>

namespace gs3d::data {

namespace {

constexpr std::size_t kLodPointBatchSize = 4096;

void encode_file_header(
    const Gs3dLodFileHeader& header,
    std::array<std::byte, sizeof(Gs3dLodFileHeader)>& bytes
) {
    std::size_t offset = 0;

    std::memcpy(bytes.data(), header.magic.data(), header.magic.size());
    offset = header.magic.size();

    write_u32_le(bytes, offset, header.version);
    write_u32_le(bytes, offset, header.header_size);
    write_u64_le(bytes, offset, header.source_point_count);
    write_u64_le(bytes, offset, header.level_count);

    write_float_le(bytes, offset, header.bbox_min_x);
    write_float_le(bytes, offset, header.bbox_min_y);
    write_float_le(bytes, offset, header.bbox_min_z);

    write_float_le(bytes, offset, header.bbox_max_x);
    write_float_le(bytes, offset, header.bbox_max_y);
    write_float_le(bytes, offset, header.bbox_max_z);

    write_float_le(bytes, offset, header.value_min);
    write_float_le(bytes, offset, header.value_max);

    write_u64_le(bytes, offset, header.build_finest_target_points);
    write_u64_le(bytes, offset, header.build_growth_factor_x1000);
    write_u64_le(bytes, offset, header.build_min_points_per_level);
    write_u64_le(bytes, offset, header.reserved);
}

void decode_file_header(
    const std::array<std::byte, sizeof(Gs3dLodFileHeader)>& bytes,
    Gs3dLodFileHeader& header
) {
    std::size_t offset = 0;

    std::memcpy(header.magic.data(), bytes.data(), header.magic.size());
    offset = header.magic.size();

    header.version = read_u32_le(bytes, offset);
    header.header_size = read_u32_le(bytes, offset);
    header.source_point_count = read_u64_le(bytes, offset);
    header.level_count = read_u64_le(bytes, offset);

    header.bbox_min_x = read_float_le(bytes, offset);
    header.bbox_min_y = read_float_le(bytes, offset);
    header.bbox_min_z = read_float_le(bytes, offset);

    header.bbox_max_x = read_float_le(bytes, offset);
    header.bbox_max_y = read_float_le(bytes, offset);
    header.bbox_max_z = read_float_le(bytes, offset);

    header.value_min = read_float_le(bytes, offset);
    header.value_max = read_float_le(bytes, offset);

    header.build_finest_target_points = read_u64_le(bytes, offset);
    header.build_growth_factor_x1000 = read_u64_le(bytes, offset);
    header.build_min_points_per_level = read_u64_le(bytes, offset);
    header.reserved = read_u64_le(bytes, offset);
}

void encode_level_header(
    const Gs3dLodLevelHeader& header,
    std::array<std::byte, sizeof(Gs3dLodLevelHeader)>& bytes
) {
    std::size_t offset = 0;

    write_u32_le(bytes, offset, header.level_index);
    write_u32_le(bytes, offset, header.voxel_mode);
    write_u64_le(bytes, offset, header.source_point_count);
    write_u64_le(bytes, offset, header.target_point_count);
    write_u64_le(bytes, offset, header.point_count);
    write_u64_le(bytes, offset, header.point_data_bytes);
    write_float_le(bytes, offset, header.voxel_size);
    write_u32_le(bytes, offset, header.level_header_size);
    write_u64_le(bytes, offset, header.reserved0);
    write_u64_le(bytes, offset, header.reserved1);
    write_u64_le(bytes, offset, header.reserved2);
}

void decode_level_header(
    const std::array<std::byte, sizeof(Gs3dLodLevelHeader)>& bytes,
    Gs3dLodLevelHeader& header
) {
    std::size_t offset = 0;

    header.level_index = read_u32_le(bytes, offset);
    header.voxel_mode = read_u32_le(bytes, offset);
    header.source_point_count = read_u64_le(bytes, offset);
    header.target_point_count = read_u64_le(bytes, offset);
    header.point_count = read_u64_le(bytes, offset);
    header.point_data_bytes = read_u64_le(bytes, offset);
    header.voxel_size = read_float_le(bytes, offset);
    header.level_header_size = read_u32_le(bytes, offset);
    header.reserved0 = read_u64_le(bytes, offset);
    header.reserved1 = read_u64_le(bytes, offset);
    header.reserved2 = read_u64_le(bytes, offset);
}

} // namespace

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
    std::uint64_t level_count,
    const Gs3dLodBuildConfig& build_config
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

    header.build_finest_target_points =
        build_config.finest_target_points;

    const double growth_x1000 =
        build_config.growth_factor > 0.0f
            ? std::llround(
                  static_cast<double>(build_config.growth_factor) *
                  1000.0
              )
            : 0.0;
    header.build_growth_factor_x1000 =
        static_cast<std::uint64_t>(std::max(growth_x1000, 0.0));

    header.build_min_points_per_level =
        build_config.min_points_per_level;

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

void Gs3dLodFormat::write_file_header(
    std::ostream& out,
    const Gs3dLodFileHeader& header
) {
    std::array<std::byte, sizeof(Gs3dLodFileHeader)> bytes{};
    encode_file_header(header, bytes);

    out.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );
}

void Gs3dLodFormat::read_file_header(
    std::istream& in,
    Gs3dLodFileHeader& header,
    const char* error_message
) {
    std::array<std::byte, sizeof(Gs3dLodFileHeader)> bytes{};

    in.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );

    if (!in) {
        throw std::runtime_error(error_message);
    }

    decode_file_header(bytes, header);
}

void Gs3dLodFormat::write_level_header(
    std::ostream& out,
    const Gs3dLodLevelHeader& header
) {
    std::array<std::byte, sizeof(Gs3dLodLevelHeader)> bytes{};
    encode_level_header(header, bytes);

    out.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );
}

void Gs3dLodFormat::read_level_header(
    std::istream& in,
    Gs3dLodLevelHeader& header,
    const char* error_message
) {
    std::array<std::byte, sizeof(Gs3dLodLevelHeader)> bytes{};

    in.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );

    if (!in) {
        throw std::runtime_error(error_message);
    }

    decode_level_header(bytes, header);
}

void Gs3dLodFormat::write_points(
    std::ostream& out,
    std::span<const Gs3dPoint> points
) {
    std::array<std::byte, sizeof(Gs3dPoint) * kLodPointBatchSize> bytes{};

    for (std::size_t begin = 0; begin < points.size();) {
        const auto count = std::min(
            kLodPointBatchSize,
            points.size() - begin
        );

        std::size_t offset = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const auto& point = points[begin + i];
            write_float_le(bytes, offset, point.x);
            write_float_le(bytes, offset, point.y);
            write_float_le(bytes, offset, point.z);
            write_float_le(bytes, offset, point.value);
        }

        out.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(count * sizeof(Gs3dPoint))
        );

        begin += count;
    }
}

void Gs3dLodFormat::read_points(
    std::istream& in,
    std::vector<Gs3dPoint>& points,
    const char* error_message
) {
    std::array<std::byte, sizeof(Gs3dPoint) * kLodPointBatchSize> bytes{};

    for (std::size_t begin = 0; begin < points.size();) {
        const auto count = std::min(
            kLodPointBatchSize,
            points.size() - begin
        );

        in.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(count * sizeof(Gs3dPoint))
        );

        if (!in) {
            throw std::runtime_error(error_message);
        }

        std::size_t offset = 0;
        for (std::size_t i = 0; i < count; ++i) {
            auto& point = points[begin + i];
            point.x = read_float_le(bytes, offset);
            point.y = read_float_le(bytes, offset);
            point.z = read_float_le(bytes, offset);
            point.value = read_float_le(bytes, offset);
        }

        begin += count;
    }
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
        if (header.version == GS3D_LOD_VERSION_V1) {
            throw std::runtime_error(
                "Gs3dLodFormat: .gs3dlod v1 is deprecated and no longer "
                "supported; regenerate the file with v2"
            );
        }

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

    oss << "  build_finest_target_points: "
        << header.build_finest_target_points
        << '\n';
    oss << "  build_growth_factor_x1000: "
        << header.build_growth_factor_x1000
        << '\n';
    oss << "  build_min_points_per_level: "
        << header.build_min_points_per_level
        << '\n';

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