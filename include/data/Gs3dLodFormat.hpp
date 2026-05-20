#pragma once

#include "data/Gs3dFormat.hpp"
#include "data/Gs3dLodDataset.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace gs3d::data {

inline constexpr std::array<char, 8> GS3D_LOD_MAGIC{
    'G', 'S', '3', 'D', 'L', 'O', 'D', '\0'
};

inline constexpr std::uint32_t GS3D_LOD_VERSION = 1;

enum class Gs3dLodFormatVoxelMode : std::uint32_t {
    XY = 1,
    XYZ = 2
};

struct Gs3dLodFileHeader {
    std::array<char, 8> magic = GS3D_LOD_MAGIC;

    std::uint32_t version = GS3D_LOD_VERSION;
    std::uint32_t header_size = sizeof(Gs3dLodFileHeader);

    std::uint64_t source_point_count = 0;
    std::uint64_t level_count = 0;

    float bbox_min_x = 0.0f;
    float bbox_min_y = 0.0f;
    float bbox_min_z = 0.0f;

    float bbox_max_x = 0.0f;
    float bbox_max_y = 0.0f;
    float bbox_max_z = 0.0f;

    float value_min = 0.0f;
    float value_max = 0.0f;

    std::uint64_t reserved0 = 0;
    std::uint64_t reserved1 = 0;
    std::uint64_t reserved2 = 0;
    std::uint64_t reserved3 = 0;
};

struct Gs3dLodLevelHeader {
    std::uint32_t level_index = 0;
    std::uint32_t voxel_mode = 0;

    std::uint64_t source_point_count = 0;
    std::uint64_t target_point_count = 0;
    std::uint64_t point_count = 0;
    std::uint64_t point_data_bytes = 0;

    float voxel_size = 0.0f;

    std::uint32_t level_header_size = sizeof(Gs3dLodLevelHeader);

    std::uint64_t reserved0 = 0;
    std::uint64_t reserved1 = 0;
    std::uint64_t reserved2 = 0;
};

class Gs3dLodFormat {
public:
    [[nodiscard]]
    static bool is_valid_magic(
        const std::array<char, 8>& magic
    ) noexcept;

    [[nodiscard]]
    static bool is_supported_version(
        std::uint32_t version
    ) noexcept;

    [[nodiscard]]
    static Gs3dLodFileHeader make_file_header(
        const Gs3dHeader& source_header,
        std::uint64_t level_count
    ) noexcept;

    [[nodiscard]]
    static Gs3dLodLevelHeader make_level_header(
        const Gs3dLodLevel& level
    );

    [[nodiscard]]
    static Gs3dLodFormatVoxelMode to_format_voxel_mode(
        Gs3dLodVoxelMode mode
    );

    [[nodiscard]]
    static Gs3dLodVoxelMode from_format_voxel_mode(
        std::uint32_t mode
    );

    [[nodiscard]]
    static const char* format_voxel_mode_name(
        std::uint32_t mode
    ) noexcept;

    static void validate_file_header(
        const Gs3dLodFileHeader& header
    );

    static void validate_level_header(
        const Gs3dLodLevelHeader& header
    );

    static void validate_against_source(
        const Gs3dLodFileHeader& lod_header,
        const Gs3dHeader& source_header
    );

    [[nodiscard]]
    static bool approximately_equal(
        float a,
        float b,
        float epsilon = 1.0e-4f
    ) noexcept;

    [[nodiscard]]
    static std::string file_header_summary(
        const Gs3dLodFileHeader& header
    );

    [[nodiscard]]
    static std::string level_header_summary(
        const Gs3dLodLevelHeader& header
    );
};

} // namespace gs3d::data

static_assert(
    sizeof(gs3d::data::Gs3dLodFileHeader) == 96,
    "Gs3dLodFileHeader size must remain stable"
);

static_assert(
    sizeof(gs3d::data::Gs3dLodLevelHeader) == 72,
    "Gs3dLodLevelHeader size must remain stable"
);