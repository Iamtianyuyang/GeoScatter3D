#include "data/Gs3dTileFormat.hpp"

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

constexpr std::size_t kTilePointBatchSize = 4096;

void encode_index_file_header(
    const Gs3dTileIndexFileHeader& header,
    std::array<std::byte, sizeof(Gs3dTileIndexFileHeader)>& bytes
) {
    std::size_t offset = 0;

    std::memcpy(bytes.data(), header.magic.data(), header.magic.size());
    offset = header.magic.size();

    write_u32_le(bytes, offset, header.version);
    write_u32_le(bytes, offset, header.header_size);
    write_u64_le(bytes, offset, header.source_point_count);
    write_u64_le(bytes, offset, header.tile_count);
    write_u64_le(bytes, offset, header.total_point_count);
    write_u32_le(bytes, offset, header.point_stride);
    write_u32_le(bytes, offset, header.tile_record_size);
    write_float_le(bytes, offset, header.tile_size_x);
    write_float_le(bytes, offset, header.tile_size_y);
    write_float_le(bytes, offset, header.grid_origin_x);
    write_float_le(bytes, offset, header.grid_origin_y);
    write_u32_le(bytes, offset, header.grid_count_x);
    write_u32_le(bytes, offset, header.grid_count_y);
    write_float_le(bytes, offset, header.bbox_min_x);
    write_float_le(bytes, offset, header.bbox_min_y);
    write_float_le(bytes, offset, header.bbox_min_z);
    write_float_le(bytes, offset, header.bbox_max_x);
    write_float_le(bytes, offset, header.bbox_max_y);
    write_float_le(bytes, offset, header.bbox_max_z);
    write_float_le(bytes, offset, header.value_min);
    write_float_le(bytes, offset, header.value_max);
    write_u32_le(bytes, offset, header.split_mode);
    write_u32_le(bytes, offset, header.reserved_u32);
    write_u64_le(bytes, offset, header.reserved0);
    write_u64_le(bytes, offset, header.reserved1);
    write_u64_le(bytes, offset, header.reserved2);
}

void decode_index_file_header(
    const std::array<std::byte, sizeof(Gs3dTileIndexFileHeader)>& bytes,
    Gs3dTileIndexFileHeader& header
) {
    std::size_t offset = 0;

    std::memcpy(header.magic.data(), bytes.data(), header.magic.size());
    offset = header.magic.size();

    header.version = read_u32_le(bytes, offset);
    header.header_size = read_u32_le(bytes, offset);
    header.source_point_count = read_u64_le(bytes, offset);
    header.tile_count = read_u64_le(bytes, offset);
    header.total_point_count = read_u64_le(bytes, offset);
    header.point_stride = read_u32_le(bytes, offset);
    header.tile_record_size = read_u32_le(bytes, offset);
    header.tile_size_x = read_float_le(bytes, offset);
    header.tile_size_y = read_float_le(bytes, offset);
    header.grid_origin_x = read_float_le(bytes, offset);
    header.grid_origin_y = read_float_le(bytes, offset);
    header.grid_count_x = read_u32_le(bytes, offset);
    header.grid_count_y = read_u32_le(bytes, offset);
    header.bbox_min_x = read_float_le(bytes, offset);
    header.bbox_min_y = read_float_le(bytes, offset);
    header.bbox_min_z = read_float_le(bytes, offset);
    header.bbox_max_x = read_float_le(bytes, offset);
    header.bbox_max_y = read_float_le(bytes, offset);
    header.bbox_max_z = read_float_le(bytes, offset);
    header.value_min = read_float_le(bytes, offset);
    header.value_max = read_float_le(bytes, offset);
    header.split_mode = read_u32_le(bytes, offset);
    header.reserved_u32 = read_u32_le(bytes, offset);
    header.reserved0 = read_u64_le(bytes, offset);
    header.reserved1 = read_u64_le(bytes, offset);
    header.reserved2 = read_u64_le(bytes, offset);
}

void encode_data_file_header(
    const Gs3dTileDataFileHeader& header,
    std::array<std::byte, sizeof(Gs3dTileDataFileHeader)>& bytes
) {
    std::size_t offset = 0;

    std::memcpy(bytes.data(), header.magic.data(), header.magic.size());
    offset = header.magic.size();

    write_u32_le(bytes, offset, header.version);
    write_u32_le(bytes, offset, header.header_size);
    write_u64_le(bytes, offset, header.source_point_count);
    write_u64_le(bytes, offset, header.tile_count);
    write_u64_le(bytes, offset, header.total_point_count);
    write_u64_le(bytes, offset, header.total_point_bytes);
    write_u32_le(bytes, offset, header.point_stride);
    write_u32_le(bytes, offset, header.reserved_u32);
    write_u64_le(bytes, offset, header.reserved0);
    write_u64_le(bytes, offset, header.reserved1);
    write_u64_le(bytes, offset, header.reserved2);
}

void decode_data_file_header(
    const std::array<std::byte, sizeof(Gs3dTileDataFileHeader)>& bytes,
    Gs3dTileDataFileHeader& header
) {
    std::size_t offset = 0;

    std::memcpy(header.magic.data(), bytes.data(), header.magic.size());
    offset = header.magic.size();

    header.version = read_u32_le(bytes, offset);
    header.header_size = read_u32_le(bytes, offset);
    header.source_point_count = read_u64_le(bytes, offset);
    header.tile_count = read_u64_le(bytes, offset);
    header.total_point_count = read_u64_le(bytes, offset);
    header.total_point_bytes = read_u64_le(bytes, offset);
    header.point_stride = read_u32_le(bytes, offset);
    header.reserved_u32 = read_u32_le(bytes, offset);
    header.reserved0 = read_u64_le(bytes, offset);
    header.reserved1 = read_u64_le(bytes, offset);
    header.reserved2 = read_u64_le(bytes, offset);
}

void encode_tile_record(
    const Gs3dTileRecord& record,
    std::array<std::byte, sizeof(Gs3dTileRecord)>& bytes
) {
    std::size_t offset = 0;

    write_u64_le(bytes, offset, record.tile_id);
    write_u32_le(bytes, offset, record.tile_x);
    write_u32_le(bytes, offset, record.tile_y);
    write_u64_le(bytes, offset, record.point_count);
    write_u64_le(bytes, offset, record.point_data_offset);
    write_u64_le(bytes, offset, record.point_data_bytes);
    write_float_le(bytes, offset, record.bbox_min_x);
    write_float_le(bytes, offset, record.bbox_min_y);
    write_float_le(bytes, offset, record.bbox_min_z);
    write_float_le(bytes, offset, record.bbox_max_x);
    write_float_le(bytes, offset, record.bbox_max_y);
    write_float_le(bytes, offset, record.bbox_max_z);
    write_float_le(bytes, offset, record.value_min);
    write_float_le(bytes, offset, record.value_max);
    write_u64_le(bytes, offset, record.reserved0);
}

void decode_tile_record(
    const std::array<std::byte, sizeof(Gs3dTileRecord)>& bytes,
    Gs3dTileRecord& record
) {
    std::size_t offset = 0;

    record.tile_id = read_u64_le(bytes, offset);
    record.tile_x = read_u32_le(bytes, offset);
    record.tile_y = read_u32_le(bytes, offset);
    record.point_count = read_u64_le(bytes, offset);
    record.point_data_offset = read_u64_le(bytes, offset);
    record.point_data_bytes = read_u64_le(bytes, offset);
    record.bbox_min_x = read_float_le(bytes, offset);
    record.bbox_min_y = read_float_le(bytes, offset);
    record.bbox_min_z = read_float_le(bytes, offset);
    record.bbox_max_x = read_float_le(bytes, offset);
    record.bbox_max_y = read_float_le(bytes, offset);
    record.bbox_max_z = read_float_le(bytes, offset);
    record.value_min = read_float_le(bytes, offset);
    record.value_max = read_float_le(bytes, offset);
    record.reserved0 = read_u64_le(bytes, offset);
}

void validate_tile_record_geometry(
    const Gs3dTileRecord& record
) {
    if (record.point_count == 0) {
        throw std::runtime_error(
            "Gs3dTileFormat: tile point_count is zero"
        );
    }

    if (record.bbox_max_x < record.bbox_min_x ||
        record.bbox_max_y < record.bbox_min_y ||
        record.bbox_max_z < record.bbox_min_z) {
        throw std::runtime_error(
            "Gs3dTileFormat: tile invalid bbox"
        );
    }

    if (record.value_max < record.value_min) {
        throw std::runtime_error(
            "Gs3dTileFormat: tile invalid value range"
        );
    }
}

void write_points_with_stride(
    std::ostream& out,
    const Gs3dPointWithId* points,
    std::size_t point_count
) {
    if constexpr (std::endian::native == std::endian::little) {
        static_assert(sizeof(Gs3dPointWithId) == 20);
        constexpr std::size_t kBlockPoints = 1024 * 1024;
        for (std::size_t begin = 0; begin < point_count;) {
            const auto count = std::min(
                kBlockPoints,
                point_count - begin
            );
            out.write(
                reinterpret_cast<const char*>(points + begin),
                static_cast<std::streamsize>(
                    count * sizeof(Gs3dPointWithId)
                )
            );
            if (!out) {
                throw std::runtime_error("Gs3dTileFormat: failed to write points with stride");
            }
            begin += count;
        }
    } else {
        std::array<std::byte, sizeof(Gs3dPointWithId) * kTilePointBatchSize> bytes{};

        for (std::size_t begin = 0; begin < point_count;) {
            const auto count = std::min(
                kTilePointBatchSize,
                point_count - begin
            );

            std::size_t offset = 0;
            for (std::size_t i = 0; i < count; ++i) {
                const auto& point = points[begin + i];
                write_float_le(bytes, offset, point.x);
                write_float_le(bytes, offset, point.y);
                write_float_le(bytes, offset, point.z);
                write_float_le(bytes, offset, point.value);
                write_u32_le(bytes, offset, point.point_id);
            }

            out.write(
                reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(
                    count * sizeof(Gs3dPointWithId)
                )
            );

            begin += count;
        }
    }
}

void read_points_with_stride(
    std::istream& in,
    Gs3dPointWithId* points,
    std::size_t point_count,
    const char* error_message
) {
    if constexpr (std::endian::native == std::endian::little) {
        static_assert(sizeof(Gs3dPointWithId) == 20);
        constexpr std::size_t kBlockPoints = 1024 * 1024;
        for (std::size_t begin = 0; begin < point_count;) {
            const auto count = std::min(
                kBlockPoints,
                point_count - begin
            );

            in.read(
                reinterpret_cast<char*>(points + begin),
                static_cast<std::streamsize>(
                    count * sizeof(Gs3dPointWithId)
                )
            );

            if (!in) {
                throw std::runtime_error(error_message);
            }

            begin += count;
        }
    } else {
        std::array<std::byte, sizeof(Gs3dPointWithId) * kTilePointBatchSize> bytes{};

        for (std::size_t begin = 0; begin < point_count;) {
            const auto count = std::min(
                kTilePointBatchSize,
                point_count - begin
            );

            in.read(
                reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(
                    count * sizeof(Gs3dPointWithId)
                )
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
                point.point_id = read_u32_le(bytes, offset);
            }

            begin += count;
        }
    }
}

} // namespace

bool Gs3dTileFormat::is_valid_index_magic(
    const std::array<char, 8>& magic
) noexcept {
    return magic == GS3D_TILE_INDEX_MAGIC;
}

bool Gs3dTileFormat::is_valid_data_magic(
    const std::array<char, 8>& magic
) noexcept {
    return magic == GS3D_TILE_DATA_MAGIC;
}

bool Gs3dTileFormat::is_supported_version(
    std::uint32_t version
) noexcept {
    return version == GS3D_TILE_VERSION_V1 ||
           version == GS3D_TILE_VERSION_V2;
}

bool Gs3dTileFormat::has_embedded_point_ids(
    std::uint32_t version
) noexcept {
    return version >= GS3D_TILE_VERSION_V2;
}

std::uint32_t Gs3dTileFormat::point_stride_for_version(
    std::uint32_t version
) {
    if (version == GS3D_TILE_VERSION_V1) {
        return GS3D_TILE_POINT_STRIDE_V1;
    }
    if (version == GS3D_TILE_VERSION_V2) {
        return GS3D_TILE_POINT_STRIDE_V2;
    }
    throw std::runtime_error(
        "Gs3dTileFormat: unsupported version for point_stride"
    );
}

bool Gs3dTileFormat::is_valid_point_stride(
    std::uint32_t stride
) noexcept {
    return stride == GS3D_TILE_POINT_STRIDE_V1 ||
           stride == GS3D_TILE_POINT_STRIDE_V2;
}

Gs3dTileIndexFileHeader Gs3dTileFormat::make_index_file_header(
    const Gs3dHeader& source_header,
    std::uint64_t tile_count,
    std::uint64_t total_point_count,
    float tile_size_x,
    float tile_size_y,
    float grid_origin_x,
    float grid_origin_y,
    std::uint32_t grid_count_x,
    std::uint32_t grid_count_y,
    Gs3dTileSplitMode split_mode
) {
    Gs3dTileIndexFileHeader header;

    header.magic = GS3D_TILE_INDEX_MAGIC;
    header.version = GS3D_TILE_VERSION;
    header.header_size =
        static_cast<std::uint32_t>(
            sizeof(Gs3dTileIndexFileHeader)
        );

    header.source_point_count = source_header.point_count;
    header.tile_count = tile_count;
    header.total_point_count = total_point_count;

    header.point_stride =
        point_stride_for_version(GS3D_TILE_VERSION);

    header.tile_record_size =
        static_cast<std::uint32_t>(sizeof(Gs3dTileRecord));

    header.tile_size_x = tile_size_x;
    header.tile_size_y = tile_size_y;

    header.grid_origin_x = grid_origin_x;
    header.grid_origin_y = grid_origin_y;

    header.grid_count_x = grid_count_x;
    header.grid_count_y = grid_count_y;

    header.bbox_min_x = source_header.bbox_min_x;
    header.bbox_min_y = source_header.bbox_min_y;
    header.bbox_min_z = source_header.bbox_min_z;

    header.bbox_max_x = source_header.bbox_max_x;
    header.bbox_max_y = source_header.bbox_max_y;
    header.bbox_max_z = source_header.bbox_max_z;

    header.value_min = source_header.value_min;
    header.value_max = source_header.value_max;

    header.split_mode =
        static_cast<std::uint32_t>(split_mode);

    validate_index_file_header(header);

    return header;
}

Gs3dTileDataFileHeader Gs3dTileFormat::make_data_file_header(
    const Gs3dHeader& source_header,
    std::uint64_t tile_count,
    std::uint64_t total_point_count,
    std::uint32_t version
) {
    Gs3dTileDataFileHeader header;

    header.magic = GS3D_TILE_DATA_MAGIC;
    header.version = version;
    header.header_size =
        static_cast<std::uint32_t>(
            sizeof(Gs3dTileDataFileHeader)
        );

    header.source_point_count = source_header.point_count;
    header.tile_count = tile_count;
    header.total_point_count = total_point_count;

    const auto stride = point_stride_for_version(version);

    header.total_point_bytes =
        total_point_count *
        static_cast<std::uint64_t>(stride);

    header.point_stride = stride;

    validate_data_file_header(header);

    return header;
}

Gs3dTileRecord Gs3dTileFormat::make_tile_record(
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
    std::uint32_t point_stride
) {
    Gs3dTileRecord record;

    record.tile_id = tile_id;
    record.tile_x = tile_x;
    record.tile_y = tile_y;

    record.point_count = point_count;
    record.point_data_offset = point_data_offset;

    record.point_data_bytes =
        point_count *
        static_cast<std::uint64_t>(point_stride);

    record.bbox_min_x = bbox_min_x;
    record.bbox_min_y = bbox_min_y;
    record.bbox_min_z = bbox_min_z;

    record.bbox_max_x = bbox_max_x;
    record.bbox_max_y = bbox_max_y;
    record.bbox_max_z = bbox_max_z;

    record.value_min = value_min;
    record.value_max = value_max;

    validate_tile_record(record, point_stride);

    return record;
}

void Gs3dTileFormat::write_index_file_header(
    std::ostream& out,
    const Gs3dTileIndexFileHeader& header
) {
    std::array<std::byte, sizeof(Gs3dTileIndexFileHeader)> bytes{};
    encode_index_file_header(header, bytes);

    out.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );
}

void Gs3dTileFormat::read_index_file_header(
    std::istream& in,
    Gs3dTileIndexFileHeader& header,
    const char* error_message
) {
    std::array<std::byte, sizeof(Gs3dTileIndexFileHeader)> bytes{};

    in.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );

    if (!in) {
        throw std::runtime_error(error_message);
    }

    decode_index_file_header(bytes, header);
}

void Gs3dTileFormat::write_data_file_header(
    std::ostream& out,
    const Gs3dTileDataFileHeader& header
) {
    std::array<std::byte, sizeof(Gs3dTileDataFileHeader)> bytes{};
    encode_data_file_header(header, bytes);

    out.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );
}

void Gs3dTileFormat::read_data_file_header(
    std::istream& in,
    Gs3dTileDataFileHeader& header,
    const char* error_message
) {
    std::array<std::byte, sizeof(Gs3dTileDataFileHeader)> bytes{};

    in.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );

    if (!in) {
        throw std::runtime_error(error_message);
    }

    decode_data_file_header(bytes, header);
}

void Gs3dTileFormat::write_tile_record(
    std::ostream& out,
    const Gs3dTileRecord& record
) {
    std::array<std::byte, sizeof(Gs3dTileRecord)> bytes{};
    encode_tile_record(record, bytes);

    out.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );
}

void Gs3dTileFormat::read_tile_record(
    std::istream& in,
    Gs3dTileRecord& record,
    const char* error_message
) {
    std::array<std::byte, sizeof(Gs3dTileRecord)> bytes{};

    in.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );

    if (!in) {
        throw std::runtime_error(error_message);
    }

    decode_tile_record(bytes, record);
}

void Gs3dTileFormat::write_points(
    std::ostream& out,
    std::span<const Gs3dPointWithId> points
) {
    write_points_with_stride(
        out,
        points.data(),
        points.size()
    );
}

void Gs3dTileFormat::read_points(
    std::istream& in,
    std::vector<Gs3dPoint>& points,
    const char* error_message
) {
    if constexpr (std::endian::native == std::endian::little) {
        static_assert(sizeof(Gs3dPoint) == 16);
        constexpr std::size_t kBlockPoints = 1024 * 1024;
        for (std::size_t begin = 0; begin < points.size();) {
            const auto count = std::min(
                kBlockPoints,
                points.size() - begin
            );

            in.read(
                reinterpret_cast<char*>(points.data() + begin),
                static_cast<std::streamsize>(count * sizeof(Gs3dPoint))
            );

            if (!in) {
                throw std::runtime_error(error_message);
            }

            begin += count;
        }
    } else {
        std::array<std::byte, sizeof(Gs3dPoint) * kTilePointBatchSize> bytes{};

        for (std::size_t begin = 0; begin < points.size();) {
            const auto count = std::min(
                kTilePointBatchSize,
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
}

void Gs3dTileFormat::read_points(
    std::istream& in,
    std::vector<Gs3dPointWithId>& points,
    const char* error_message
) {
    read_points_with_stride(
        in,
        points.data(),
        points.size(),
        error_message
    );
}

void Gs3dTileFormat::validate_index_file_header(
    const Gs3dTileIndexFileHeader& header
) {
    if (!is_valid_index_magic(header.magic)) {
        throw std::runtime_error(
            "Gs3dTileFormat: invalid tile index magic"
        );
    }

    if (!is_supported_version(header.version)) {
        throw std::runtime_error(
            "Gs3dTileFormat: unsupported tile index version"
        );
    }

    if (header.header_size != sizeof(Gs3dTileIndexFileHeader)) {
        throw std::runtime_error(
            "Gs3dTileFormat: invalid tile index header size"
        );
    }

    if (header.source_point_count == 0) {
        throw std::runtime_error(
            "Gs3dTileFormat: source_point_count is zero"
        );
    }

    if (header.tile_count == 0) {
        throw std::runtime_error(
            "Gs3dTileFormat: tile_count is zero"
        );
    }

    if (header.total_point_count == 0) {
        throw std::runtime_error(
            "Gs3dTileFormat: total_point_count is zero"
        );
    }

    if (!is_valid_point_stride(header.point_stride)) {
        throw std::runtime_error(
            "Gs3dTileFormat: invalid point stride"
        );
    }

    if (header.point_stride !=
        point_stride_for_version(header.version)) {
        throw std::runtime_error(
            "Gs3dTileFormat: point stride does not match version"
        );
    }

    if (header.tile_record_size != sizeof(Gs3dTileRecord)) {
        throw std::runtime_error(
            "Gs3dTileFormat: invalid tile record size"
        );
    }

    if (header.tile_size_x <= 0.0f ||
        header.tile_size_y <= 0.0f) {
        throw std::runtime_error(
            "Gs3dTileFormat: invalid tile size"
        );
    }

    if (header.grid_count_x == 0 ||
        header.grid_count_y == 0) {
        throw std::runtime_error(
            "Gs3dTileFormat: invalid grid count"
        );
    }

    if (header.bbox_max_x < header.bbox_min_x ||
        header.bbox_max_y < header.bbox_min_y ||
        header.bbox_max_z < header.bbox_min_z) {
        throw std::runtime_error(
            "Gs3dTileFormat: invalid source bbox"
        );
    }

    if (header.value_max < header.value_min) {
        throw std::runtime_error(
            "Gs3dTileFormat: invalid value range"
        );
    }

    (void) from_format_split_mode(header.split_mode);
}

void Gs3dTileFormat::validate_data_file_header(
    const Gs3dTileDataFileHeader& header
) {
    if (!is_valid_data_magic(header.magic)) {
        throw std::runtime_error(
            "Gs3dTileFormat: invalid tile data magic"
        );
    }

    if (!is_supported_version(header.version)) {
        throw std::runtime_error(
            "Gs3dTileFormat: unsupported tile data version"
        );
    }

    if (header.header_size != sizeof(Gs3dTileDataFileHeader)) {
        throw std::runtime_error(
            "Gs3dTileFormat: invalid tile data header size"
        );
    }

    if (header.source_point_count == 0) {
        throw std::runtime_error(
            "Gs3dTileFormat: data source_point_count is zero"
        );
    }

    if (header.tile_count == 0) {
        throw std::runtime_error(
            "Gs3dTileFormat: data tile_count is zero"
        );
    }

    if (header.total_point_count == 0) {
        throw std::runtime_error(
            "Gs3dTileFormat: data total_point_count is zero"
        );
    }

    if (!is_valid_point_stride(header.point_stride)) {
        throw std::runtime_error(
            "Gs3dTileFormat: data invalid point stride"
        );
    }

    if (header.point_stride !=
        point_stride_for_version(header.version)) {
        throw std::runtime_error(
            "Gs3dTileFormat: data point stride does not match version"
        );
    }

    const std::uint64_t expected_bytes =
        header.total_point_count *
        static_cast<std::uint64_t>(header.point_stride);

    if (header.total_point_bytes != expected_bytes) {
        throw std::runtime_error(
            "Gs3dTileFormat: data total_point_bytes mismatch"
        );
    }
}

void Gs3dTileFormat::validate_tile_record(
    const Gs3dTileRecord& record
) {
    validate_tile_record_geometry(record);

    /*
     * 无 stride 上下文的独立校验：接受 v1（16 字节）或 v2（20 字节）
     * 两种记录字节数。调用方已知 stride 时必须使用带 point_stride 的
     * 重载逐记录强校验，防止混合 stride 文件通过聚合校验。
     */
    const bool valid_v1 =
        record.point_data_bytes ==
        record.point_count *
            static_cast<std::uint64_t>(GS3D_TILE_POINT_STRIDE_V1);
    const bool valid_v2 =
        record.point_data_bytes ==
        record.point_count *
            static_cast<std::uint64_t>(GS3D_TILE_POINT_STRIDE_V2);

    if (!valid_v1 && !valid_v2) {
        throw std::runtime_error(
            "Gs3dTileFormat: tile point_data_bytes mismatch"
        );
    }
}

void Gs3dTileFormat::validate_tile_record(
    const Gs3dTileRecord& record,
    std::uint32_t point_stride
) {
    validate_tile_record_geometry(record);

    const std::uint64_t expected_bytes =
        record.point_count *
        static_cast<std::uint64_t>(point_stride);

    if (record.point_data_bytes != expected_bytes) {
        throw std::runtime_error(
            "Gs3dTileFormat: tile point_data_bytes does not match "
            "the declared point stride"
        );
    }
}

void Gs3dTileFormat::validate_index_against_data(
    const Gs3dTileIndexFileHeader& index_header,
    const Gs3dTileDataFileHeader& data_header
) {
    validate_index_file_header(index_header);
    validate_data_file_header(data_header);

    if (index_header.source_point_count !=
        data_header.source_point_count) {
        throw std::runtime_error(
            "Gs3dTileFormat: source_point_count mismatch"
        );
    }

    if (index_header.tile_count != data_header.tile_count) {
        throw std::runtime_error(
            "Gs3dTileFormat: tile_count mismatch"
        );
    }

    if (index_header.total_point_count !=
        data_header.total_point_count) {
        throw std::runtime_error(
            "Gs3dTileFormat: total_point_count mismatch"
        );
    }

    if (index_header.point_stride != data_header.point_stride) {
        throw std::runtime_error(
            "Gs3dTileFormat: point_stride mismatch"
        );
    }
}

void Gs3dTileFormat::validate_against_source(
    const Gs3dTileIndexFileHeader& index_header,
    const Gs3dHeader& source_header
) {
    validate_index_file_header(index_header);

    if (index_header.source_point_count !=
        source_header.point_count) {
        throw std::runtime_error(
            "Gs3dTileFormat: source point_count mismatch"
        );
    }

    if (!approximately_equal(index_header.bbox_min_x, source_header.bbox_min_x) ||
        !approximately_equal(index_header.bbox_min_y, source_header.bbox_min_y) ||
        !approximately_equal(index_header.bbox_min_z, source_header.bbox_min_z) ||
        !approximately_equal(index_header.bbox_max_x, source_header.bbox_max_x) ||
        !approximately_equal(index_header.bbox_max_y, source_header.bbox_max_y) ||
        !approximately_equal(index_header.bbox_max_z, source_header.bbox_max_z)) {
        throw std::runtime_error(
            "Gs3dTileFormat: source bbox mismatch"
        );
    }

    if (!approximately_equal(index_header.value_min, source_header.value_min) ||
        !approximately_equal(index_header.value_max, source_header.value_max)) {
        throw std::runtime_error(
            "Gs3dTileFormat: source value range mismatch"
        );
    }
}

Gs3dTileSplitMode Gs3dTileFormat::from_format_split_mode(
    std::uint32_t mode
) {
    const auto typed_mode =
        static_cast<Gs3dTileSplitMode>(mode);

    switch (typed_mode) {
    case Gs3dTileSplitMode::XY:
        return Gs3dTileSplitMode::XY;

    default:
        throw std::runtime_error(
            "Gs3dTileFormat: unsupported tile split mode"
        );
    }
}

const char* Gs3dTileFormat::split_mode_name(
    std::uint32_t mode
) noexcept {
    const auto typed_mode =
        static_cast<Gs3dTileSplitMode>(mode);

    switch (typed_mode) {
    case Gs3dTileSplitMode::XY:
        return "XY";

    default:
        return "Unknown";
    }
}

bool Gs3dTileFormat::approximately_equal(
    float a,
    float b,
    float epsilon
) noexcept {
    return std::abs(a - b) <= epsilon;
}

std::string Gs3dTileFormat::index_file_header_summary(
    const Gs3dTileIndexFileHeader& header
) {
    std::ostringstream oss;

    oss << "Gs3dTileIndexFileHeader\n";

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

    oss << "  tile_count: "
        << header.tile_count
        << '\n';

    oss << "  total_point_count: "
        << header.total_point_count
        << '\n';

    oss << "  point_stride: "
        << header.point_stride
        << '\n';

    oss << "  tile_record_size: "
        << header.tile_record_size
        << '\n';

    oss << "  tile_size: ["
        << header.tile_size_x
        << ", "
        << header.tile_size_y
        << "]\n";

    oss << "  grid_origin: ["
        << header.grid_origin_x
        << ", "
        << header.grid_origin_y
        << "]\n";

    oss << "  grid_count: ["
        << header.grid_count_x
        << ", "
        << header.grid_count_y
        << "]\n";

    oss << "  bbox_min: ["
        << header.bbox_min_x << ", "
        << header.bbox_min_y << ", "
        << header.bbox_min_z << "]\n";

    oss << "  bbox_max: ["
        << header.bbox_max_x << ", "
        << header.bbox_max_y << ", "
        << header.bbox_max_z << "]\n";

    oss << "  value_range: ["
        << header.value_min
        << ", "
        << header.value_max
        << "]\n";

    oss << "  split_mode: "
        << split_mode_name(header.split_mode)
        << '\n';

    return oss.str();
}

std::string Gs3dTileFormat::data_file_header_summary(
    const Gs3dTileDataFileHeader& header
) {
    std::ostringstream oss;

    oss << "Gs3dTileDataFileHeader\n";

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

    oss << "  tile_count: "
        << header.tile_count
        << '\n';

    oss << "  total_point_count: "
        << header.total_point_count
        << '\n';

    oss << "  total_point_bytes: "
        << header.total_point_bytes
        << '\n';

    oss << "  point_stride: "
        << header.point_stride
        << '\n';

    return oss.str();
}

std::string Gs3dTileFormat::tile_record_summary(
    const Gs3dTileRecord& record
) {
    std::ostringstream oss;

    oss << "Gs3dTileRecord\n";

    oss << "  tile_id: "
        << record.tile_id
        << '\n';

    oss << "  tile_xy: ["
        << record.tile_x
        << ", "
        << record.tile_y
        << "]\n";

    oss << "  point_count: "
        << record.point_count
        << '\n';

    oss << "  point_data_offset: "
        << record.point_data_offset
        << '\n';

    oss << "  point_data_bytes: "
        << record.point_data_bytes
        << '\n';

    oss << "  bbox_min: ["
        << record.bbox_min_x << ", "
        << record.bbox_min_y << ", "
        << record.bbox_min_z << "]\n";

    oss << "  bbox_max: ["
        << record.bbox_max_x << ", "
        << record.bbox_max_y << ", "
        << record.bbox_max_z << "]\n";

    oss << "  value_range: ["
        << record.value_min
        << ", "
        << record.value_max
        << "]\n";

    return oss.str();
}

} // namespace gs3d::data