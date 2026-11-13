#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

namespace gs3d::foundation {

using Sha256Digest = std::array<std::uint8_t, 32>;

std::string to_hex(const Sha256Digest& digest);

// Lower-case hex digest of the whole file; used as the resume fingerprint in
// manifest.json so two preprocessing runs are never mixed.
std::string sha256_file_hex(const std::filesystem::path& path);

} // namespace gs3d::foundation
