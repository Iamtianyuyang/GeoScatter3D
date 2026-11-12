#include "data/Gs3dPointChunk.hpp"

#include "data/Gs3dByteOrder.hpp"
#include "util/Crc32c.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace gs3d::data {
namespace {

constexpr std::array<std::byte, 8> magic{
    std::byte{'G'}, std::byte{'S'}, std::byte{'3'}, std::byte{'D'},
    std::byte{'P'}, std::byte{'T'}, std::byte{'3'}, std::byte{0}
};
constexpr std::uint32_t version = 3;
constexpr std::size_t point_bytes = 16;
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);

} // namespace

std::vector<std::byte> encode_point_chunk(std::span<const Gs3dPoint> points) {
    if (points.empty() || points.size() > GS3D_POINT_CHUNK_MAX_POINTS) {
        throw std::invalid_argument("point chunk: invalid point count");
    }

    std::vector<std::byte> bytes(GS3D_POINT_CHUNK_HEADER_SIZE + points.size() * point_bytes);
    auto output = bytes.begin() + GS3D_POINT_CHUNK_HEADER_SIZE;
    for (const auto& point : points) {
        std::array<std::byte, point_bytes> encoded{};
        std::size_t offset = 0;
        write_float_le(encoded, offset, point.x);
        write_float_le(encoded, offset, point.y);
        write_float_le(encoded, offset, point.z);
        write_float_le(encoded, offset, point.value);
        output = std::copy(encoded.begin(), encoded.end(), output);
    }

    std::array<std::byte, GS3D_POINT_CHUNK_HEADER_SIZE> header{};
    std::copy(magic.begin(), magic.end(), header.begin());
    std::size_t offset = magic.size();
    write_u32_le(header, offset, version);
    write_u32_le(header, offset, GS3D_POINT_CHUNK_HEADER_SIZE);
    write_u32_le(header, offset, static_cast<std::uint32_t>(points.size()));
    write_u32_le(header, offset, static_cast<std::uint32_t>(points.size() * point_bytes));
    write_u32_le(header, offset, gs3d::util::crc32c(std::span(bytes).subspan(header.size())));
    write_u32_le(header, offset, 0);
    std::copy(header.begin(), header.end(), bytes.begin());
    return bytes;
}

std::vector<Gs3dPoint> decode_point_chunk(std::span<const std::byte> bytes) {
    if (bytes.size() < GS3D_POINT_CHUNK_HEADER_SIZE ||
        !std::equal(magic.begin(), magic.end(), bytes.begin())) {
        throw std::runtime_error("point chunk: invalid header");
    }
    std::array<std::byte, GS3D_POINT_CHUNK_HEADER_SIZE> header{};
    std::copy_n(bytes.begin(), header.size(), header.begin());
    std::size_t offset = magic.size();
    const auto file_version = read_u32_le(header, offset);
    const auto header_size = read_u32_le(header, offset);
    const auto count = read_u32_le(header, offset);
    const auto payload_size = read_u32_le(header, offset);
    const auto checksum = read_u32_le(header, offset);
    const auto reserved = read_u32_le(header, offset);
    if (file_version != version || header_size != header.size() || reserved != 0 ||
        count == 0 || count > GS3D_POINT_CHUNK_MAX_POINTS ||
        payload_size != count * point_bytes || bytes.size() != header.size() + payload_size) {
        throw std::runtime_error("point chunk: invalid layout");
    }
    const auto payload = bytes.subspan(header.size());
    if (gs3d::util::crc32c(payload) != checksum) {
        throw std::runtime_error("point chunk: checksum mismatch");
    }

    std::vector<Gs3dPoint> points(count);
    auto input = payload.begin();
    for (auto& point : points) {
        std::array<std::byte, point_bytes> encoded{};
        std::copy_n(input, encoded.size(), encoded.begin());
        input += encoded.size();
        std::size_t point_offset = 0;
        point.x = read_float_le(encoded, point_offset);
        point.y = read_float_le(encoded, point_offset);
        point.z = read_float_le(encoded, point_offset);
        point.value = read_float_le(encoded, point_offset);
    }
    return points;
}

} // namespace gs3d::data
