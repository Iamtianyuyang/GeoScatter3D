#include "data/CsvChunkUtils.hpp"

#include <filesystem>

namespace gs3d::data {

std::uint64_t file_size(std::ifstream& file) {
    const auto current = file.tellg();
    file.clear();
    file.seekg(0, std::ios::end);
    const auto end = file.tellg();
    file.clear();
    file.seekg(current);
    return end >= 0
        ? static_cast<std::uint64_t>(end)
        : 0;
}

std::uint64_t align_to_next_record_begin(
    std::ifstream& file,
    std::uint64_t offset
) {
    const auto total_size = file_size(file);
    if (offset == 0 || offset >= total_size) {
        return offset;
    }

    file.clear();
    file.seekg(static_cast<std::streamoff>(offset - 1));

    char prev = '\0';
    file.get(prev);
    if (prev == '\n') {
        file.clear();
        return offset;
    }

    file.clear();
    file.seekg(static_cast<std::streamoff>(offset));

    char ch = '\0';
    while (file.get(ch)) {
        if (ch == '\n') {
            const auto pos = file.tellg();
            return pos >= 0
                ? static_cast<std::uint64_t>(pos)
                : total_size;
        }
    }

    file.clear();
    return total_size;
}

std::uint64_t extend_to_record_end(
    std::ifstream& file,
    std::uint64_t offset
) {
    const auto total_size = file_size(file);
    if (offset >= total_size) {
        return total_size;
    }

    file.clear();
    file.seekg(static_cast<std::streamoff>(offset));

    char ch = '\0';
    while (file.get(ch)) {
        if (ch == '\n') {
            const auto pos = file.tellg();
            return pos >= 0
                ? static_cast<std::uint64_t>(pos)
                : total_size;
        }
    }

    file.clear();
    return total_size;
}

bool read_line_at_current_position(
    std::ifstream& file,
    std::string& line,
    std::uint64_t& line_begin_offset,
    std::uint64_t& line_end_offset
) {
    const auto begin = file.tellg();
    if (begin < 0) {
        return false;
    }

    line_begin_offset = static_cast<std::uint64_t>(begin);

    if (!std::getline(file, line)) {
        return false;
    }

    const auto end = file.tellg();
    if (end >= 0) {
        line_end_offset = static_cast<std::uint64_t>(end);
    } else {
        line_end_offset = file_size(file);
    }

    return true;
}

} // namespace gs3d::data
