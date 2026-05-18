#pragma once

#include <cstdint>
#include <string>

namespace gs3d::data {

inline constexpr char GS3D_MAGIC[4] = {'G', 'S', '3', 'D'};
inline constexpr std::uint32_t GS3D_VERSION = 1;

struct Gs3dHeader {
    char magic[4];

    std::uint32_t version;
    std::uint32_t header_size;

    std::uint64_t point_count;
    std::uint64_t point_data_offset;

    double origin_x;
    double origin_y;
    double origin_z;

    float bbox_min_x;
    float bbox_min_y;
    float bbox_min_z;

    float bbox_max_x;
    float bbox_max_y;
    float bbox_max_z;

    float value_min;
    float value_max;

    std::uint32_t flags;
    std::uint32_t reserved0;

    std::uint64_t reserved1;
    std::uint64_t reserved2;
};

struct Gs3dPoint {
    float x;
    float y;
    float z;
    float value;
};

static_assert(sizeof(Gs3dPoint) == 16, "Gs3dPoint must be 16 bytes");

class Gs3dFormat {
public:
    static Gs3dHeader create_empty_header();

    static bool has_valid_magic(const Gs3dHeader& header);

    static bool has_supported_version(const Gs3dHeader& header);

    static bool is_valid_header(const Gs3dHeader& header);

    static std::string describe_header_error(const Gs3dHeader& header);

    static std::uint64_t expected_file_size(const Gs3dHeader& header);
};

} // namespace gs3d::data