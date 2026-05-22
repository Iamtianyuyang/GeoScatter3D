#pragma once

#include "data/Gs3dDataset.hpp"

#include <cstdint>
#include <filesystem>

namespace gs3d::preprocess {

struct Gs3dTileWriteConfig {
    /*
     * XY 分块尺寸。
     *
     * 单位和原始 x/y 坐标一致。
     * 对当前数据范围 x≈7294, y≈8964：
     *   512 -> 约 15 x 18 个 tile
     *   256 -> 约 29 x 36 个 tile
     */
    float tile_size_x = 512.0f;
    float tile_size_y = 512.0f;

    /*
     * 并行分桶线程数。
     *
     * 0 表示自动选择，按硬件并发数取值，并做适度上限限制。
     * 实际写盘仍保持单线程顺序写出，以保证输出稳定。
     */
    std::uint32_t num_threads = 0;

    bool verbose = true;
};

struct Gs3dTileWriteStats {
    std::filesystem::path index_path;
    std::filesystem::path data_path;

    std::uint32_t grid_count_x = 0;
    std::uint32_t grid_count_y = 0;

    std::uint64_t tile_count = 0;
    std::uint64_t total_points = 0;

    std::uint64_t min_tile_points = 0;
    std::uint64_t max_tile_points = 0;

    std::uint64_t index_file_bytes = 0;
    std::uint64_t data_file_bytes = 0;

    float tile_size_x = 0.0f;
    float tile_size_y = 0.0f;

    double bucket_build_seconds = 0.0;
    double tile_stats_seconds = 0.0;
    double data_write_seconds = 0.0;
    double index_write_seconds = 0.0;
    double total_write_seconds = 0.0;

    bool success = false;
};

class Gs3dTileWriter {
public:
    [[nodiscard]]
    static Gs3dTileWriteStats write(
        const std::filesystem::path& index_path,
        const std::filesystem::path& data_path,
        const gs3d::data::Gs3dDataset& dataset,
        const Gs3dTileWriteConfig& config = {}
    );
};

} // namespace gs3d::preprocess
