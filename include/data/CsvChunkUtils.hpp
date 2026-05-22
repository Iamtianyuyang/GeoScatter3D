#pragma once

#include <cstdint>
#include <fstream>
#include <string>

namespace gs3d::data {

[[nodiscard]]
std::uint64_t file_size(std::ifstream& file);

[[nodiscard]]
std::uint64_t align_to_next_record_begin(
    std::ifstream& file,
    std::uint64_t offset
);

[[nodiscard]]
std::uint64_t extend_to_record_end(
    std::ifstream& file,
    std::uint64_t offset
);

[[nodiscard]]
bool read_line_at_current_position(
    std::ifstream& file,
    std::string& line,
    std::uint64_t& line_begin_offset,
    std::uint64_t& line_end_offset
);

} // namespace gs3d::data
