#pragma once

#include "data/Gs3dFormat.hpp"

#include <array>
#include <cstdint>
#include <iosfwd>
#include <span>
#include <string>
#include <vector>

namespace gs3d::data {

inline constexpr std::array<char, 8> GS3D_TILE_INDEX_MAGIC{
    'G', 'S', '3', 'D', 'T', 'I', 'X', '\0'
};

inline constexpr std::array<char, 8> GS3D_TILE_DATA_MAGIC{
    'G', 'S', '3', 'D', 'T', 'I', 'L', '\0'
};

inline constexpr std::uint32_t GS3D_TILE_VERSION = 2;

/*
 * Supported tile format versions.
 *
 * v1 — point_stride = 16 (Gs3dPoint: x, y, z, value).  No embedded
 *      point_id; runtime must rebuild tile→point_id mapping by scanning
 *      the full source dataset (slow path).
 *
 * v2 — point_stride = 20 (Gs3dPointWithId: x, y, z, value, point_id).
 *      Embedded global 1-based point_id eliminates the startup scan.
 *      New preprocessing writes v2; the reader auto-detects and uses
 *      the fast path.  Old v1 tiles still open with the v1 slow path.
 */
inline constexpr std::uint32_t GS3D_TILE_VERSION_V1 = 1;
inline constexpr std::uint32_t GS3D_TILE_VERSION_V2 = 2;

inline constexpr std::uint32_t GS3D_TILE_POINT_STRIDE_V1 =
    sizeof(Gs3dPoint);          // 16
inline constexpr std::uint32_t GS3D_TILE_POINT_STRIDE_V2 =
    sizeof(Gs3dPointWithId);    // 20

enum class Gs3dTileSplitMode : std::uint32_t {
    XY = 1
};

struct Gs3dTileRecord {
    std::uint64_t tile_id = 0;

    std::uint32_t tile_x = 0;
    std::uint32_t tile_y = 0;

    std::uint64_t point_count = 0;
    std::uint64_t point_data_offset = 0;
    std::uint64_t point_data_bytes = 0;

    float bbox_min_x = 0.0f;
    float bbox_min_y = 0.0f;
    float bbox_min_z = 0.0f;

    float bbox_max_x = 0.0f;
    float bbox_max_y = 0.0f;
    float bbox_max_z = 0.0f;

    float value_min = 0.0f;
    float value_max = 0.0f;

    std::uint64_t reserved0 = 0;
};

struct Gs3dTileIndexFileHeader {
    std::array<char, 8> magic = GS3D_TILE_INDEX_MAGIC;

    std::uint32_t version = GS3D_TILE_VERSION;
    std::uint32_t header_size = sizeof(Gs3dTileIndexFileHeader);

    std::uint64_t source_point_count = 0;
    std::uint64_t tile_count = 0;
    std::uint64_t total_point_count = 0;

    std::uint32_t point_stride = sizeof(Gs3dPoint);
    std::uint32_t tile_record_size = sizeof(Gs3dTileRecord);

    float tile_size_x = 0.0f;
    float tile_size_y = 0.0f;

    float grid_origin_x = 0.0f;
    float grid_origin_y = 0.0f;

    std::uint32_t grid_count_x = 0;
    std::uint32_t grid_count_y = 0;

    float bbox_min_x = 0.0f;
    float bbox_min_y = 0.0f;
    float bbox_min_z = 0.0f;

    float bbox_max_x = 0.0f;
    float bbox_max_y = 0.0f;
    float bbox_max_z = 0.0f;

    float value_min = 0.0f;
    float value_max = 0.0f;

    std::uint32_t split_mode = static_cast<std::uint32_t>(
        Gs3dTileSplitMode::XY
    );

    std::uint32_t reserved_u32 = 0;

    std::uint64_t reserved0 = 0;
    std::uint64_t reserved1 = 0;
    std::uint64_t reserved2 = 0;
};

struct Gs3dTileDataFileHeader {
    std::array<char, 8> magic = GS3D_TILE_DATA_MAGIC;

    std::uint32_t version = GS3D_TILE_VERSION;
    std::uint32_t header_size = sizeof(Gs3dTileDataFileHeader);

    std::uint64_t source_point_count = 0;
    std::uint64_t tile_count = 0;
    std::uint64_t total_point_count = 0;
    std::uint64_t total_point_bytes = 0;

    std::uint32_t point_stride = sizeof(Gs3dPoint);
    std::uint32_t reserved_u32 = 0;

    std::uint64_t reserved0 = 0;
    std::uint64_t reserved1 = 0;
    std::uint64_t reserved2 = 0;
};


class Gs3dTileFormat {
public:
    [[nodiscard]]
    static bool is_valid_index_magic(
        const std::array<char, 8>& magic
    ) noexcept;

    [[nodiscard]]
    static bool is_valid_data_magic(
        const std::array<char, 8>& magic
    ) noexcept;

    [[nodiscard]]
    static bool is_supported_version(
        std::uint32_t version
    ) noexcept;

    [[nodiscard]]
    static bool has_embedded_point_ids(
        std::uint32_t version
    ) noexcept;

    [[nodiscard]]
    static std::uint32_t point_stride_for_version(
        std::uint32_t version
    );

    [[nodiscard]]
    static bool is_valid_point_stride(
        std::uint32_t stride
    ) noexcept;

    [[nodiscard]]
    static Gs3dTileIndexFileHeader make_index_file_header(
        const Gs3dHeader& source_header,
        std::uint64_t tile_count,
        std::uint64_t total_point_count,
        float tile_size_x,
        float tile_size_y,
        float grid_origin_x,
        float grid_origin_y,
        std::uint32_t grid_count_x,
        std::uint32_t grid_count_y,
        Gs3dTileSplitMode split_mode = Gs3dTileSplitMode::XY
    );

    [[nodiscard]]
    static Gs3dTileDataFileHeader make_data_file_header(
        const Gs3dHeader& source_header,
        std::uint64_t tile_count,
        std::uint64_t total_point_count,
        std::uint32_t version = GS3D_TILE_VERSION
    );

    [[nodiscard]]
    static Gs3dTileRecord make_tile_record(
        std::uint64_t tile_id,
        std::uint32_t tile_x,
        std::uint32_t tile_y,
        std::uint64_t point_count,
        std::uint64_t point_data_offset,
        float bbox_min_x,
        float bbox_min_y,
        float bbox_min_z,
        float bbox_max_x,
        float bbox_max_y,
        float bbox_max_z,
        float value_min,
        float value_max,
        std::uint32_t point_stride = GS3D_TILE_POINT_STRIDE_V2
    );

    static void validate_index_file_header(
        const Gs3dTileIndexFileHeader& header
    );

    static void validate_data_file_header(
        const Gs3dTileDataFileHeader& header
    );

    static void validate_tile_record(
        const Gs3dTileRecord& record
    );

    /*
     * 严格按已知 stride 校验单条 tile 记录：point_data_bytes 必须恰好等于
     * point_count × point_stride。混合/损坏 stride 的文件必须被显式拒绝，
     * 不允许读取端按错误 stride 解码导致尾部点静默置零。
     */
    static void validate_tile_record(
        const Gs3dTileRecord& record,
        std::uint32_t point_stride
    );

    static void validate_index_against_data(
        const Gs3dTileIndexFileHeader& index_header,
        const Gs3dTileDataFileHeader& data_header
    );

    static void validate_against_source(
        const Gs3dTileIndexFileHeader& index_header,
        const Gs3dHeader& source_header
    );

    [[nodiscard]]
    static Gs3dTileSplitMode from_format_split_mode(
        std::uint32_t mode
    );

    [[nodiscard]]
    static const char* split_mode_name(
        std::uint32_t mode
    ) noexcept;

    [[nodiscard]]
    static bool approximately_equal(
        float a,
        float b,
        float epsilon = 1.0e-4f
    ) noexcept;

    /*
     * 显式小端序列化：所有整数与 IEEE-754 字段按小端逐字节写入/读出，
     * 与宿主字节序无关（参考 Gs3dFormat 的 GS3D v2 可移植编码）。
     * 在小端主机上与原生 struct 直写字节级一致。
     */
    static void write_index_file_header(
        std::ostream& out,
        const Gs3dTileIndexFileHeader& header
    );

    static void read_index_file_header(
        std::istream& in,
        Gs3dTileIndexFileHeader& header,
        const char* error_message
    );

    static void write_data_file_header(
        std::ostream& out,
        const Gs3dTileDataFileHeader& header
    );

    static void read_data_file_header(
        std::istream& in,
        Gs3dTileDataFileHeader& header,
        const char* error_message
    );

    static void write_tile_record(
        std::ostream& out,
        const Gs3dTileRecord& record
    );

    static void read_tile_record(
        std::istream& in,
        Gs3dTileRecord& record,
        const char* error_message
    );

    static void write_points(
        std::ostream& out,
        std::span<const Gs3dPointWithId> points
    );

    static void read_points(
        std::istream& in,
        std::vector<Gs3dPoint>& points,
        const char* error_message
    );

    static void read_points(
        std::istream& in,
        std::vector<Gs3dPointWithId>& points,
        const char* error_message
    );

    [[nodiscard]]
    static std::string index_file_header_summary(
        const Gs3dTileIndexFileHeader& header
    );

    [[nodiscard]]
    static std::string data_file_header_summary(
        const Gs3dTileDataFileHeader& header
    );

    [[nodiscard]]
    static std::string tile_record_summary(
        const Gs3dTileRecord& record
    );
};

} // namespace gs3d::data

static_assert(
    sizeof(gs3d::data::Gs3dTileIndexFileHeader) == 136,
    "Gs3dTileIndexFileHeader size must remain stable"
);

static_assert(
    sizeof(gs3d::data::Gs3dTileDataFileHeader) == 80,
    "Gs3dTileDataFileHeader size must remain stable"
);

static_assert(
    sizeof(gs3d::data::Gs3dTileRecord) == 80,
    "Gs3dTileRecord size must remain stable"
);