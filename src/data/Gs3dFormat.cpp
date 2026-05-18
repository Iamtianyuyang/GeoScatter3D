#include "data/Gs3dFormat.hpp"

#include <cstring>
#include <limits>

namespace gs3d::data {

Gs3dHeader Gs3dFormat::create_empty_header() {
    Gs3dHeader header{};

    std::memcpy(header.magic, GS3D_MAGIC, sizeof(header.magic));

    header.version = GS3D_VERSION;
    header.header_size = static_cast<std::uint32_t>(sizeof(Gs3dHeader));

    header.point_count = 0;
    header.point_data_offset = sizeof(Gs3dHeader);

    header.origin_x = 0.0;
    header.origin_y = 0.0;
    header.origin_z = 0.0;

    header.bbox_min_x = 0.0f;
    header.bbox_min_y = 0.0f;
    header.bbox_min_z = 0.0f;

    header.bbox_max_x = 0.0f;
    header.bbox_max_y = 0.0f;
    header.bbox_max_z = 0.0f;

    header.value_min = 0.0f;
    header.value_max = 0.0f;

    header.flags = 0;
    header.reserved0 = 0;
    header.reserved1 = 0;
    header.reserved2 = 0;

    return header;
}

bool Gs3dFormat::has_valid_magic(const Gs3dHeader& header) {
    return std::memcmp(header.magic, GS3D_MAGIC, sizeof(header.magic)) == 0;
}

bool Gs3dFormat::has_supported_version(const Gs3dHeader& header) {
    return header.version == GS3D_VERSION;
}

bool Gs3dFormat::is_valid_header(const Gs3dHeader& header) {
    if (!has_valid_magic(header)) {
        return false;
    }

    if (!has_supported_version(header)) {
        return false;
    }

    if (header.header_size != sizeof(Gs3dHeader)) {
        return false;
    }

    if (header.point_data_offset < sizeof(Gs3dHeader)) {
        return false;
    }

    if (header.bbox_min_x > header.bbox_max_x ||
        header.bbox_min_y > header.bbox_max_y ||
        header.bbox_min_z > header.bbox_max_z) {
        return false;
    }

    if (header.value_min > header.value_max) {
        return false;
    }

    const std::uint64_t max_count =
        std::numeric_limits<std::uint64_t>::max() / sizeof(Gs3dPoint);

    if (header.point_count > max_count) {
        return false;
    }

    return true;
}

std::string Gs3dFormat::describe_header_error(const Gs3dHeader& header) {
    if (!has_valid_magic(header)) {
        return "invalid GS3D magic";
    }

    if (!has_supported_version(header)) {
        return "unsupported GS3D version";
    }

    if (header.header_size != sizeof(Gs3dHeader)) {
        return "invalid GS3D header size";
    }

    if (header.point_data_offset < sizeof(Gs3dHeader)) {
        return "invalid GS3D point data offset";
    }

    if (header.bbox_min_x > header.bbox_max_x ||
        header.bbox_min_y > header.bbox_max_y ||
        header.bbox_min_z > header.bbox_max_z) {
        return "invalid GS3D bounding box";
    }

    if (header.value_min > header.value_max) {
        return "invalid GS3D value range";
    }

    const std::uint64_t max_count =
        std::numeric_limits<std::uint64_t>::max() / sizeof(Gs3dPoint);

    if (header.point_count > max_count) {
        return "GS3D point count is too large";
    }

    return "valid GS3D header";
}

std::uint64_t Gs3dFormat::expected_file_size(const Gs3dHeader& header) {
    const std::uint64_t point_bytes =
        header.point_count * static_cast<std::uint64_t>(sizeof(Gs3dPoint));

    return header.point_data_offset + point_bytes;
}

} // namespace gs3d::data