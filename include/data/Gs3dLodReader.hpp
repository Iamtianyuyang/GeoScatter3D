#pragma once

#include "data/Gs3dFormat.hpp"
#include "data/Gs3dLodDataset.hpp"
#include "data/Gs3dLodFormat.hpp"

#include <cstdint>
#include <filesystem>

namespace gs3d::data {

struct Gs3dLodReadConfig {
    /*
     * 如果 true，会检查 .gs3dlod 的 source header
     * 是否和 .gs3d 的 header 匹配。
     */
    bool validate_against_source = true;

    /*
     * 如果 true，读取后输出简要信息。
     */
    bool verbose = false;
};

struct Gs3dLodReadStats {
    std::filesystem::path path;

    std::uint64_t level_count = 0;
    std::uint64_t total_points = 0;
    std::uint64_t total_point_bytes = 0;
    std::uint64_t file_bytes = 0;

    bool success = false;
};

struct Gs3dLodReadResult {
    Gs3dLodDataset dataset;
    Gs3dLodReadStats stats;
    Gs3dLodFileHeader file_header{};
};

class Gs3dLodReader {
public:
    [[nodiscard]]
    static Gs3dLodReadResult read(
        const std::filesystem::path& path,
        const Gs3dHeader& source_header,
        const Gs3dLodReadConfig& config = {}
    );

    [[nodiscard]]
    static Gs3dLodReadResult read_without_source_validation(
        const std::filesystem::path& path,
        const Gs3dLodReadConfig& config = {}
    );
};

} // namespace gs3d::data