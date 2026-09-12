/*
 * 合成基准 bundle 生成器：产出 source.gs3d + lod.gs3dlod + tiles.gs3dtiles(.index)。
 *
 * 用途：在没有真实大工区数据的环境里，按需生成「千万级」点云 bundle，
 * 用于验证 Vulkan 核心的两层渲染策略——粗尺度走 LOD、细尺度由 tile
 * 叠加全精度原始点。
 *
 * 用法：
 *   gs3d_bench_bundle --out <bundle_dir> [--points 4000000]
 *                     [--extent 40] [--tile-size 2] [--no-lod] [--no-tiles]
 */
#include "data/Gs3dFormat.hpp"
#include "data/Gs3dLodDataset.hpp"
#include "preprocess/Gs3dLodWriter.hpp"
#include "preprocess/Gs3dTileWriter.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct Options {
    std::filesystem::path out_dir = "tmp/bench-bundle";
    std::uint64_t points = 1'000'000;
    float extent = 40.0f;
    float tile_size = 2.0f;
    bool write_lod = true;
    bool write_tiles = true;
};

Options parse(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto next = [&]() -> std::string {
            return i + 1 < argc ? argv[++i] : std::string{};
        };
        if (arg == "--out") {
            options.out_dir = next();
        } else if (arg == "--points") {
            options.points = std::strtoull(next().c_str(), nullptr, 10);
        } else if (arg == "--extent") {
            options.extent = std::strtof(next().c_str(), nullptr);
        } else if (arg == "--tile-size") {
            options.tile_size = std::strtof(next().c_str(), nullptr);
        } else if (arg == "--no-lod") {
            options.write_lod = false;
        } else if (arg == "--no-tiles") {
            options.write_tiles = false;
        }
    }
    return options;
}

/// 与内核内置样例一致的确定性地形（sin 同心环 + 网格）。
std::vector<gs3d::data::Gs3dPoint> generate(
    std::uint64_t requested,
    float extent,
    gs3d::data::Gs3dHeader& header
) {
    const int grid = std::max(
        2,
        static_cast<int>(std::llround(std::sqrt(static_cast<double>(requested))))
    );

    std::vector<gs3d::data::Gs3dPoint> points;
    points.reserve(static_cast<std::size_t>(grid) * grid);

    float min_x = 1e30f, max_x = -1e30f;
    float min_y = 1e30f, max_y = -1e30f;
    float min_z = 1e30f, max_z = -1e30f;

    for (int i = 0; i < grid; ++i) {
        const float x = -extent + (2.0f * extent * i) / (grid - 1);
        for (int j = 0; j < grid; ++j) {
            const float y = -extent + (2.0f * extent * j) / (grid - 1);
            const float r = std::sqrt(x * x + y * y);
            const float z = 6.0f * std::sin(r) / (r * 0.4f + 1.0f);

            min_x = std::min(min_x, x);
            max_x = std::max(max_x, x);
            min_y = std::min(min_y, y);
            max_y = std::max(max_y, y);
            min_z = std::min(min_z, z);
            max_z = std::max(max_z, z);

            gs3d::data::Gs3dPoint point{};
            point.x = x;
            point.y = y;
            point.z = z;
            point.value = z;
            points.push_back(point);
        }
    }

    header = gs3d::data::Gs3dFormat::create_empty_header();
    header.point_count = points.size();
    header.header_size = gs3d::data::GS3D_HEADER_V2_SIZE;
    header.point_data_offset = header.header_size;
    header.origin_x = 0.0;
    header.origin_y = 0.0;
    header.origin_z = 0.0;
    header.bbox_min_x = min_x;
    header.bbox_min_y = min_y;
    header.bbox_min_z = min_z;
    header.bbox_max_x = max_x;
    header.bbox_max_y = max_y;
    header.bbox_max_z = max_z;
    header.value_min = min_z;
    header.value_max = max_z;
    return points;
}

std::uint64_t file_bytes(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    return error ? 0 : static_cast<std::uint64_t>(size);
}

}  // namespace

int main(int argc, char** argv) {
    const Options options = parse(argc, argv);

    std::error_code error;
    std::filesystem::create_directories(options.out_dir, error);
    if (error) {
        std::cerr << "cannot create " << options.out_dir << ": "
                  << error.message() << '\n';
        return 1;
    }

    gs3d::data::Gs3dHeader header{};
    const auto points = generate(options.points, options.extent, header);
    std::cout << "points=" << points.size() << " grid="
              << static_cast<int>(std::llround(
                     std::sqrt(static_cast<double>(points.size()))
                 ))
              << '\n';

    // 1. source.gs3d
    const auto source_path = options.out_dir / "source.gs3d";
    {
        std::ofstream out(source_path, std::ios::binary | std::ios::trunc);
        if (!out || !gs3d::data::Gs3dFormat::write_header(out, header) ||
            !gs3d::data::Gs3dFormat::write_points(
                out,
                std::span<const gs3d::data::Gs3dPoint>(points.data(), points.size())
            )) {
            std::cerr << "failed to write " << source_path << '\n';
            return 1;
        }
    }
    std::cout << "source.gs3d = " << file_bytes(source_path) << " bytes\n";

    const gs3d::data::Gs3dDataset dataset(header, points, source_path.string());

    // 2. lod.gs3dlod
    if (options.write_lod) {
        gs3d::data::Gs3dLodBuildConfig lod_config;
        lod_config.include_full_resolution_level = false;
        lod_config.voxel_mode = gs3d::data::Gs3dLodVoxelMode::XY;
        const auto lod_dataset = gs3d::data::Gs3dLodDataset::build(dataset, lod_config);
        if (lod_dataset.empty()) {
            std::cout << "lod: skipped (dataset below minimum)\n";
        } else {
            const auto lod_path = options.out_dir / "lod.gs3dlod";
            const auto stats =
                gs3d::preprocess::Gs3dLodWriter::write(lod_path, lod_dataset);
            std::cout << "lod.gs3dlod = " << file_bytes(lod_path)
                      << " bytes, levels=" << stats.level_count << '\n';
        }
    }

    // 3. tiles.gs3dtiles(.index)
    if (options.write_tiles) {
        gs3d::preprocess::Gs3dTileWriteConfig tile_config;
        tile_config.tile_size_x = options.tile_size;
        tile_config.tile_size_y = options.tile_size;
        tile_config.verbose = false;
        const auto index_path = options.out_dir / "tiles.gs3dtiles.index";
        const auto data_path = options.out_dir / "tiles.gs3dtiles";
        const auto stats = gs3d::preprocess::Gs3dTileWriter::write(
            index_path,
            data_path,
            dataset,
            tile_config
        );
        if (!stats.success) {
            std::cerr << "tile write failed\n";
            return 1;
        }
        std::cout << "tiles = " << stats.tile_count << " tiles, "
                  << stats.total_points << " points, "
                  << file_bytes(data_path) << " + " << file_bytes(index_path)
                  << " bytes\n";
    }

    return 0;
}
