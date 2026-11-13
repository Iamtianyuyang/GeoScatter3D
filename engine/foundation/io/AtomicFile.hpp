#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string_view>

namespace gs3d::foundation {

// Replaces `path` with `bytes` so that a crash leaves either the old or the new
// file, never a half-written one: write to a sibling temp file, flush it, then
// rename over the target and flush the directory entry.
void atomic_write_file(const std::filesystem::path& path, std::span<const std::byte> bytes);
void atomic_write_file(const std::filesystem::path& path, std::string_view bytes);

} // namespace gs3d::foundation
