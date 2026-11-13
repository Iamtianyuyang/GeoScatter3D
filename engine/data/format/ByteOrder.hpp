#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace gs3d::data {

/*
 * 显式小端字段编解码工具（可移植磁盘布局）。
 *
 * 所有整数与 IEEE-754 浮点字段都按字节序无关的方式逐字节写入/读出，
 * 与宿主架构无关。在小端主机上产出与原生 struct 直写完全一致的字节。
 *
 * 用法（与 src/data/Gs3dFormat.cpp 中 GS3D v2 头部编码一致）：
 *
 *   std::array<std::byte, 16> bytes{};
 *   std::size_t offset = 0;
 *   write_u32_le(bytes, offset, value);
 *   ...
 *   std::uint32_t value = read_u32_le(bytes, offset);
 */

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

} // namespace gs3d::data
