#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gs3d::util {

inline std::uint32_t crc32c(std::span<const std::byte> bytes) noexcept {
    static constexpr auto table = [] {
        std::array<std::uint32_t, 256> values{};
        for (std::uint32_t i = 0; i < values.size(); ++i) {
            auto crc = i;
            for (int bit = 0; bit < 8; ++bit) {
                crc = (crc >> 1) ^ ((crc & 1u) ? 0x82f63b78u : 0u);
            }
            values[i] = crc;
        }
        return values;
    }();

    std::uint32_t crc = 0xffffffffu;
    for (const auto byte : bytes) {
        crc = table[(crc ^ std::to_integer<std::uint8_t>(byte)) & 0xffu] ^ (crc >> 8);
    }
    return crc ^ 0xffffffffu;
}

} // namespace gs3d::util
