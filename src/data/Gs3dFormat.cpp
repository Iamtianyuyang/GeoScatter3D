#include "data/Gs3dFormat.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <istream>
#include <limits>
#include <ostream>

namespace gs3d::data {
namespace {

constexpr std::size_t kHeaderPrefixSize = 12;
constexpr std::size_t kPointSerializationBatchSize = 4096;

static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);

template <std::size_t N>
void write_u32_le(
    std::array<std::byte, N>& bytes,
    std::size_t& offset,
    std::uint32_t value
) {
    for (std::size_t i = 0; i < sizeof(value); ++i) {
        bytes[offset++] = static_cast<std::byte>(value & 0xFFu);
        value >>= 8;
    }
}

template <std::size_t N>
void write_u64_le(
    std::array<std::byte, N>& bytes,
    std::size_t& offset,
    std::uint64_t value
) {
    for (std::size_t i = 0; i < sizeof(value); ++i) {
        bytes[offset++] = static_cast<std::byte>(value & 0xFFu);
        value >>= 8;
    }
}

template <std::size_t N>
void write_float_le(
    std::array<std::byte, N>& bytes,
    std::size_t& offset,
    float value
) {
    write_u32_le(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

template <std::size_t N>
void write_double_le(
    std::array<std::byte, N>& bytes,
    std::size_t& offset,
    double value
) {
    write_u64_le(bytes, offset, std::bit_cast<std::uint64_t>(value));
}

template <std::size_t N>
std::uint32_t read_u32_le(
    const std::array<std::byte, N>& bytes,
    std::size_t& offset
) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < sizeof(value); ++i) {
        value |= static_cast<std::uint32_t>(bytes[offset++]) << (i * 8);
    }
    return value;
}

template <std::size_t N>
std::uint64_t read_u64_le(
    const std::array<std::byte, N>& bytes,
    std::size_t& offset
) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < sizeof(value); ++i) {
        value |= static_cast<std::uint64_t>(bytes[offset++]) << (i * 8);
    }
    return value;
}

template <std::size_t N>
float read_float_le(
    const std::array<std::byte, N>& bytes,
    std::size_t& offset
) {
    return std::bit_cast<float>(read_u32_le(bytes, offset));
}

template <std::size_t N>
double read_double_le(
    const std::array<std::byte, N>& bytes,
    std::size_t& offset
) {
    return std::bit_cast<double>(read_u64_le(bytes, offset));
}

bool write_bytes(
    std::ostream& out,
    const std::byte* bytes,
    std::size_t size
) {
    out.write(
        reinterpret_cast<const char*>(bytes),
        static_cast<std::streamsize>(size)
    );
    return static_cast<bool>(out);
}

bool read_bytes(
    std::istream& in,
    std::byte* bytes,
    std::size_t size
) {
    in.read(
        reinterpret_cast<char*>(bytes),
        static_cast<std::streamsize>(size)
    );
    return static_cast<bool>(in);
}

template <std::size_t N>
bool write_bytes(std::ostream& out, const std::array<std::byte, N>& bytes)
{
    return write_bytes(out, bytes.data(), bytes.size());
}

template <std::size_t N>
bool read_bytes(std::istream& in, std::array<std::byte, N>& bytes)
{
    return read_bytes(in, bytes.data(), bytes.size());
}

void decode_v2_header(
    const std::array<std::byte, GS3D_HEADER_V2_SIZE>& bytes,
    Gs3dHeader& header
) {
    std::size_t offset = 0;
    for (char& character : header.magic) {
        character = static_cast<char>(bytes[offset++]);
    }
    header.version = read_u32_le(bytes, offset);
    header.header_size = read_u32_le(bytes, offset);
    header.point_count = read_u64_le(bytes, offset);
    header.point_data_offset = read_u64_le(bytes, offset);
    header.origin_x = read_double_le(bytes, offset);
    header.origin_y = read_double_le(bytes, offset);
    header.origin_z = read_double_le(bytes, offset);
    header.bbox_min_x = read_float_le(bytes, offset);
    header.bbox_min_y = read_float_le(bytes, offset);
    header.bbox_min_z = read_float_le(bytes, offset);
    header.bbox_max_x = read_float_le(bytes, offset);
    header.bbox_max_y = read_float_le(bytes, offset);
    header.bbox_max_z = read_float_le(bytes, offset);
    header.value_min = read_float_le(bytes, offset);
    header.value_max = read_float_le(bytes, offset);
    header.flags = read_u32_le(bytes, offset);
    header.reserved0 = read_u32_le(bytes, offset);
    header.reserved1 = read_u64_le(bytes, offset);
    header.reserved2 = read_u64_le(bytes, offset);
}

void encode_v2_header(
    const Gs3dHeader& header,
    std::array<std::byte, GS3D_HEADER_V2_SIZE>& bytes
) {
    std::size_t offset = 0;
    for (const char character : header.magic) {
        bytes[offset++] = static_cast<std::byte>(character);
    }
    write_u32_le(bytes, offset, header.version);
    write_u32_le(bytes, offset, header.header_size);
    write_u64_le(bytes, offset, header.point_count);
    write_u64_le(bytes, offset, header.point_data_offset);
    write_double_le(bytes, offset, header.origin_x);
    write_double_le(bytes, offset, header.origin_y);
    write_double_le(bytes, offset, header.origin_z);
    write_float_le(bytes, offset, header.bbox_min_x);
    write_float_le(bytes, offset, header.bbox_min_y);
    write_float_le(bytes, offset, header.bbox_min_z);
    write_float_le(bytes, offset, header.bbox_max_x);
    write_float_le(bytes, offset, header.bbox_max_y);
    write_float_le(bytes, offset, header.bbox_max_z);
    write_float_le(bytes, offset, header.value_min);
    write_float_le(bytes, offset, header.value_max);
    write_u32_le(bytes, offset, header.flags);
    write_u32_le(bytes, offset, header.reserved0);
    write_u64_le(bytes, offset, header.reserved1);
    write_u64_le(bytes, offset, header.reserved2);
}

void encode_point(
    const Gs3dPoint& point,
    std::array<std::byte, GS3D_POINT_SIZE>& bytes
) {
    std::size_t offset = 0;
    write_float_le(bytes, offset, point.x);
    write_float_le(bytes, offset, point.y);
    write_float_le(bytes, offset, point.z);
    write_float_le(bytes, offset, point.value);
}

Gs3dPoint decode_point(
    const std::array<std::byte, GS3D_POINT_SIZE>& bytes
) {
    std::size_t offset = 0;
    return {
        read_float_le(bytes, offset),
        read_float_le(bytes, offset),
        read_float_le(bytes, offset),
        read_float_le(bytes, offset)
    };
}

} // namespace

Gs3dHeader Gs3dFormat::create_empty_header() {
    Gs3dHeader header{};

    std::memcpy(header.magic, GS3D_MAGIC, sizeof(header.magic));

    header.version = GS3D_VERSION;
    header.header_size = GS3D_HEADER_V2_SIZE;

    header.point_count = 0;
    header.point_data_offset = GS3D_HEADER_V2_SIZE;

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
    return header.version == GS3D_LEGACY_VERSION ||
        header.version == GS3D_VERSION;
}

bool Gs3dFormat::is_valid_header(const Gs3dHeader& header) {
    if (!has_valid_magic(header)) {
        return false;
    }

    if (!has_supported_version(header)) {
        return false;
    }

    const std::uint64_t minimum_header_size =
        header.version == GS3D_LEGACY_VERSION
            ? sizeof(Gs3dHeader)
            : GS3D_HEADER_V2_SIZE;
    if (header.version == GS3D_LEGACY_VERSION &&
        header.header_size != minimum_header_size) {
        return false;
    }
    if (header.version == GS3D_VERSION &&
        header.header_size < minimum_header_size) {
        return false;
    }

    if (header.point_data_offset < header.header_size) {
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
        std::numeric_limits<std::uint64_t>::max() / GS3D_POINT_SIZE;

    if (header.point_count > max_count) {
        return false;
    }

    const std::uint64_t point_bytes =
        header.point_count * static_cast<std::uint64_t>(GS3D_POINT_SIZE);
    if (header.point_data_offset >
        std::numeric_limits<std::uint64_t>::max() - point_bytes) {
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

    if (header.version == GS3D_LEGACY_VERSION &&
        header.header_size != sizeof(Gs3dHeader)) {
        return "invalid GS3D v1 header size";
    }
    if (header.version == GS3D_VERSION &&
        header.header_size < GS3D_HEADER_V2_SIZE) {
        return "invalid GS3D v2 header size";
    }

    if (header.point_data_offset < header.header_size) {
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
        std::numeric_limits<std::uint64_t>::max() / GS3D_POINT_SIZE;

    if (header.point_count > max_count) {
        return "GS3D point count is too large";
    }

    const std::uint64_t point_bytes =
        header.point_count * static_cast<std::uint64_t>(GS3D_POINT_SIZE);
    if (header.point_data_offset >
        std::numeric_limits<std::uint64_t>::max() - point_bytes) {
        return "GS3D file size overflows uint64";
    }

    return "valid GS3D header";
}

std::uint64_t Gs3dFormat::expected_file_size(const Gs3dHeader& header) {
    const std::uint64_t point_bytes =
        header.point_count * static_cast<std::uint64_t>(GS3D_POINT_SIZE);

    return header.point_data_offset + point_bytes;
}

bool Gs3dFormat::write_header(
    std::ostream& out,
    const Gs3dHeader& header
) {
    if (header.version != GS3D_VERSION || !is_valid_header(header)) {
        return false;
    }

    std::array<std::byte, GS3D_HEADER_V2_SIZE> bytes{};
    encode_v2_header(header, bytes);
    return write_bytes(out, bytes);
}

bool Gs3dFormat::read_header(std::istream& in, Gs3dHeader& header) {
    std::array<std::byte, GS3D_HEADER_V2_SIZE> bytes{};
    if (!read_bytes(in, bytes.data(), kHeaderPrefixSize)) {
        return false;
    }

    std::size_t prefix_offset = sizeof(GS3D_MAGIC);
    const auto version = read_u32_le(bytes, prefix_offset);

    if (version == GS3D_LEGACY_VERSION) {
        std::array<std::byte, sizeof(Gs3dHeader)> legacy_bytes{};
        std::copy_n(
            bytes.begin(),
            kHeaderPrefixSize,
            legacy_bytes.begin()
        );
        if (!read_bytes(
                in,
                legacy_bytes.data() + kHeaderPrefixSize,
                legacy_bytes.size() - kHeaderPrefixSize
            )) {
            return false;
        }
        std::memcpy(&header, legacy_bytes.data(), sizeof(header));
        return true;
    }

    if (version != GS3D_VERSION ||
        !read_bytes(
            in,
            bytes.data() + kHeaderPrefixSize,
            bytes.size() - kHeaderPrefixSize
        )) {
        return false;
    }

    decode_v2_header(bytes, header);
    return true;
}

bool Gs3dFormat::write_points(
    std::ostream& out,
    std::span<const Gs3dPoint> points
) {
    if constexpr (std::endian::native == std::endian::little) {
        static_assert(sizeof(Gs3dPoint) == GS3D_POINT_SIZE);
        constexpr std::size_t kBlockPoints = 1024 * 1024;
        for (std::size_t begin = 0; begin < points.size();) {
            const auto count = std::min(kBlockPoints, points.size() - begin);
            out.write(
                reinterpret_cast<const char*>(points.data() + begin),
                static_cast<std::streamsize>(count * sizeof(Gs3dPoint))
            );
            if (!out) {
                return false;
            }
            begin += count;
        }
        return true;
    } else {
        std::array<
            std::byte,
            GS3D_POINT_SIZE * kPointSerializationBatchSize
        > bytes{};

        for (std::size_t begin = 0; begin < points.size();) {
            const auto count = std::min(
                kPointSerializationBatchSize,
                points.size() - begin
            );
            for (std::size_t i = 0; i < count; ++i) {
                std::array<std::byte, GS3D_POINT_SIZE> point_bytes{};
                encode_point(points[begin + i], point_bytes);
                std::copy(
                    point_bytes.begin(),
                    point_bytes.end(),
                    bytes.begin() + i * GS3D_POINT_SIZE
                );
            }
            if (!write_bytes(out, bytes.data(), count * GS3D_POINT_SIZE)) {
                return false;
            }
            begin += count;
        }

        return true;
    }
}

bool Gs3dFormat::read_points(
    std::istream& in,
    const Gs3dHeader& header,
    std::vector<Gs3dPoint>& points
) {
    if (header.version == GS3D_LEGACY_VERSION) {
        for (std::size_t begin = 0; begin < points.size();) {
            const auto count = std::min(
                kPointSerializationBatchSize,
                points.size() - begin
            );
            in.read(
                reinterpret_cast<char*>(points.data() + begin),
                static_cast<std::streamsize>(count * sizeof(Gs3dPoint))
            );
            if (!in) {
                return false;
            }
            begin += count;
        }
        return true;
    }

    if (header.version != GS3D_VERSION) {
        return false;
    }

    if constexpr (std::endian::native == std::endian::little) {
        static_assert(sizeof(Gs3dPoint) == GS3D_POINT_SIZE);
        constexpr std::size_t kBlockPoints = 1024 * 1024;
        for (std::size_t begin = 0; begin < points.size();) {
            const auto count = std::min(kBlockPoints, points.size() - begin);
            in.read(
                reinterpret_cast<char*>(points.data() + begin),
                static_cast<std::streamsize>(count * sizeof(Gs3dPoint))
            );
            if (!in) {
                return false;
            }
            begin += count;
        }
        return true;
    } else {
        std::array<
            std::byte,
            GS3D_POINT_SIZE * kPointSerializationBatchSize
        > bytes{};
        for (std::size_t begin = 0; begin < points.size();) {
            const auto count = std::min(
                kPointSerializationBatchSize,
                points.size() - begin
            );
            if (!read_bytes(in, bytes.data(), count * GS3D_POINT_SIZE)) {
                return false;
            }
            for (std::size_t i = 0; i < count; ++i) {
                std::array<std::byte, GS3D_POINT_SIZE> point_bytes{};
                std::copy_n(
                    bytes.begin() + i * GS3D_POINT_SIZE,
                    GS3D_POINT_SIZE,
                    point_bytes.begin()
                );
                points[begin + i] = decode_point(point_bytes);
            }
            begin += count;
        }

        return true;
    }
}

} // namespace gs3d::data
