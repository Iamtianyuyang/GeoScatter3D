#pragma once

#include "data/Gs3dFormat.hpp"
#include "data/Gs3dLodDataset.hpp"

#include <array>
#include <cstdint>
#include <iosfwd>
#include <span>
#include <string>
#include <vector>

namespace gs3d::data {

inline constexpr std::array<char, 8> GS3D_LOD_MAGIC{
    'G', 'S', '3', 'D', 'L', 'O', 'D', '\0'
};

/*
 * .gs3dlod 版本。
 *
 * v1 已正式废弃：只支持 v2。v1 文件（旧原生布局）会被 validate_file_header
 * 显式拒绝并提示重新生成，不再提供兼容读取路径。GS3D_LOD_VERSION_V1 仅用于
 * 报错信息区分「v1 已废弃」与「未知版本」。
 */
inline constexpr std::uint32_t GS3D_LOD_VERSION = 2;
inline constexpr std::uint32_t GS3D_LOD_VERSION_V1 = 1;

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

    /*
     * 构建时锚参数：预处理写入 build() 所用的配置，读取端/工具链可用于检测
     * 配置变更（growth_factor 存 ×1000 的整数值，如 1414 表示 1.414）。
     */
    std::uint64_t build_finest_target_points = 0;
    std::uint64_t build_growth_factor_x1000 = 0;
    std::uint64_t build_min_points_per_level = 0;

    std::uint64_t reserved = 0;
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
        std::uint64_t level_count,
        const Gs3dLodBuildConfig& build_config = {}
    ) noexcept;

    /*
     * 显式小端序列化：所有整数与 IEEE-754 字段按小端逐字节写入/读出，
     * 与宿主字节序无关（参考 Gs3dFormat 的 GS3D v2 可移植编码）。
     * 在小端主机上与原生 struct 直写字节级一致。
     */
    static void write_file_header(
        std::ostream& out,
        const Gs3dLodFileHeader& header
    );

    static void read_file_header(
        std::istream& in,
        Gs3dLodFileHeader& header,
        const char* error_message
    );

    static void write_level_header(
        std::ostream& out,
        const Gs3dLodLevelHeader& header
    );

    static void read_level_header(
        std::istream& in,
        Gs3dLodLevelHeader& header,
        const char* error_message
    );

    static void write_points(
        std::ostream& out,
        std::span<const Gs3dPoint> points
    );

    static void read_points(
        std::istream& in,
        std::vector<Gs3dPoint>& points,
        const char* error_message
    );

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