#pragma once

#include "data/format/Point.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace gs3d::data {

// Little-endian v3: 8-byte magic, then six u32 fields (version, header bytes,
// point count, payload bytes, CRC32C of payload, reserved zero). Payload is
// x/y/z/value IEEE-754 binary32 in that order; 1 to 65536 points per chunk.
inline constexpr std::size_t GS3D_POINT_CHUNK_MAX_POINTS = 65536;
inline constexpr std::size_t GS3D_POINT_CHUNK_HEADER_SIZE = 32;

std::vector<std::byte> encode_point_chunk(std::span<const Gs3dPoint> points);
std::vector<Gs3dPoint> decode_point_chunk(std::span<const std::byte> bytes);

} // namespace gs3d::data
