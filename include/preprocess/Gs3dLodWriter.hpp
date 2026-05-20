#pragma once

#include "data/Gs3dLodDataset.hpp"

#include <cstdint>
#include <filesystem>

namespace gs3d::preprocess {

struct Gs3dLodWriteStats {
    std::filesystem::path path;

    std::uint64_t level_count = 0;
    std::uint64_t total_points = 0;
    std::uint64_t total_point_bytes = 0;
    std::uint64_t total_file_bytes = 0;

    bool success = false;
};

class Gs3dLodWriter {
public:
    [[nodiscard]]
    static Gs3dLodWriteStats write(
        const std::filesystem::path& path,
        const gs3d::data::Gs3dLodDataset& lod_dataset
    );
};

} // namespace gs3d::preprocess