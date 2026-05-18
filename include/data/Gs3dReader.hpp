#pragma once

#include "data/Gs3dFormat.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace gs3d::data {

struct Gs3dReadResult {
    Gs3dHeader header{};
    std::vector<Gs3dPoint> points;
};

class Gs3dReader {
public:
    [[nodiscard]]
    static Gs3dHeader read_header(const std::filesystem::path& path);

    [[nodiscard]]
    static Gs3dReadResult read_all(const std::filesystem::path& path);

    [[nodiscard]]
    static bool validate_file_size(const std::filesystem::path& path);
};

} // namespace gs3d::data