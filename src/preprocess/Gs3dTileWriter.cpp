#include "preprocess/Gs3dTileWriter.hpp"

#include "data/Gs3dTileFormat.hpp"
#include "util/Stopwatch.hpp"
#include "util/ThreadPool.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace gs3d::preprocess {

namespace {

using gs3d::data::Gs3dDataset;
using gs3d::data::Gs3dPoint;
using gs3d::data::Gs3dTileDataFileHeader;
using gs3d::data::Gs3dTileFormat;
using gs3d::data::Gs3dTileIndexFileHeader;
using gs3d::data::Gs3dTileRecord;
using gs3d::data::Gs3dTileSplitMode;

struct TileBuildBucket {
    std::uint32_t tile_x = 0;
    std::uint32_t tile_y = 0;

    std::vector<std::uint32_t> point_indices;

    float bbox_min_x = std::numeric_limits<float>::max();
    float bbox_min_y = std::numeric_limits<float>::max();
    float bbox_min_z = std::numeric_limits<float>::max();

    float bbox_max_x = std::numeric_limits<float>::lowest();
    float bbox_max_y = std::numeric_limits<float>::lowest();
    float bbox_max_z = std::numeric_limits<float>::lowest();

    float value_min = std::numeric_limits<float>::max();
    float value_max = std::numeric_limits<float>::lowest();

    [[nodiscard]]
    bool empty() const noexcept {
        return point_indices.empty();
    }

    [[nodiscard]]
    std::uint64_t point_count() const noexcept {
        return static_cast<std::uint64_t>(point_indices.size());
    }
};

template <typename T>
void write_binary(
    std::ofstream& file,
    const T& value,
    const char* error_message
) {
    file.write(
        reinterpret_cast<const char*>(&value),
        static_cast<std::streamsize>(sizeof(T))
    );

    if (!file.good()) {
        throw std::runtime_error(error_message);
    }
}

void write_points(
    std::ofstream& file,
    const std::vector<Gs3dPoint>& points
) {
    if (points.empty()) {
        throw std::runtime_error(
            "Gs3dTileWriter: cannot write empty point array"
        );
    }

    const auto byte_count =
        static_cast<std::streamsize>(
            points.size() * sizeof(Gs3dPoint)
        );

    file.write(
        reinterpret_cast<const char*>(points.data()),
        byte_count
    );

    if (!file.good()) {
        throw std::runtime_error(
            "Gs3dTileWriter: failed to write tile point data"
        );
    }
}

[[nodiscard]]
std::uint64_t file_size_or_zero(
    const std::filesystem::path& path
) {
    std::error_code ec;

    const auto size =
        std::filesystem::file_size(path, ec);

    if (ec) {
        return 0;
    }

    return static_cast<std::uint64_t>(size);
}

void create_parent_directories(
    const std::filesystem::path& path
) {
    const auto parent_path =
        path.parent_path();

    if (parent_path.empty()) {
        return;
    }

    std::error_code ec;

    std::filesystem::create_directories(
        parent_path,
        ec
    );

    if (ec) {
        throw std::runtime_error(
            "Gs3dTileWriter: failed to create directory: " +
            parent_path.string()
        );
    }
}

[[nodiscard]]
float safe_extent(
    float min_value,
    float max_value
) noexcept {
    return std::max(
        max_value - min_value,
        1.0e-6f
    );
}

[[nodiscard]]
std::uint32_t resolve_thread_count(
    std::uint32_t requested_threads,
    std::uint64_t point_count
) noexcept {
    if (point_count <= 1) {
        return 1;
    }

    if (requested_threads > 0) {
        return requested_threads;
    }

    const auto hw = std::thread::hardware_concurrency();
    if (hw <= 1) {
        return 1;
    }

    return std::min<std::uint32_t>(hw, 8);
}

[[nodiscard]]
std::uint32_t compute_grid_count(
    float min_value,
    float max_value,
    float tile_size
) {
    if (tile_size <= 0.0f) {
        throw std::runtime_error(
            "Gs3dTileWriter: tile size must be greater than zero"
        );
    }

    const float extent =
        safe_extent(
            min_value,
            max_value
        );

    const auto count =
        static_cast<std::uint64_t>(
            std::ceil(extent / tile_size)
        );

    if (count == 0) {
        return 1;
    }

    if (count > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error(
            "Gs3dTileWriter: grid count exceeds uint32 range"
        );
    }

    return static_cast<std::uint32_t>(count);
}

[[nodiscard]]
std::uint32_t point_tile_coord(
    float value,
    float origin,
    float tile_size,
    std::uint32_t grid_count
) {
    if (grid_count == 0) {
        throw std::runtime_error(
            "Gs3dTileWriter: grid_count is zero"
        );
    }

    const auto raw =
        static_cast<std::int64_t>(
            std::floor((value - origin) / tile_size)
        );

    if (raw < 0) {
        return 0;
    }

    const auto upper =
        static_cast<std::int64_t>(grid_count - 1);

    if (raw > upper) {
        return grid_count - 1;
    }

    return static_cast<std::uint32_t>(raw);
}

[[nodiscard]]
std::uint64_t linear_tile_id(
    std::uint32_t tile_x,
    std::uint32_t tile_y,
    std::uint32_t grid_count_x
) noexcept {
    return static_cast<std::uint64_t>(tile_y) *
           static_cast<std::uint64_t>(grid_count_x) +
           static_cast<std::uint64_t>(tile_x);
}

void update_tile_bounds(
    TileBuildBucket& tile,
    const Gs3dPoint& point
) noexcept {
    tile.bbox_min_x = std::min(tile.bbox_min_x, point.x);
    tile.bbox_min_y = std::min(tile.bbox_min_y, point.y);
    tile.bbox_min_z = std::min(tile.bbox_min_z, point.z);

    tile.bbox_max_x = std::max(tile.bbox_max_x, point.x);
    tile.bbox_max_y = std::max(tile.bbox_max_y, point.y);
    tile.bbox_max_z = std::max(tile.bbox_max_z, point.z);

    tile.value_min = std::min(tile.value_min, point.value);
    tile.value_max = std::max(tile.value_max, point.value);
}

[[nodiscard]]
std::vector<TileBuildBucket> build_tile_buckets(
    const Gs3dDataset& dataset,
    float tile_size_x,
    float tile_size_y,
    std::uint32_t grid_count_x,
    std::uint32_t grid_count_y,
    std::uint32_t num_threads
) {
    const std::uint64_t total_tile_slots =
        static_cast<std::uint64_t>(grid_count_x) *
        static_cast<std::uint64_t>(grid_count_y);

    if (total_tile_slots >
        static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max()
        )) {
        throw std::runtime_error(
            "Gs3dTileWriter: tile slot count exceeds size_t range"
        );
    }

    if (dataset.point_count() >
        static_cast<std::uint64_t>(
            std::numeric_limits<std::uint32_t>::max()
        )) {
        throw std::runtime_error(
            "Gs3dTileWriter: point_count exceeds uint32 index range"
        );
    }

    std::vector<TileBuildBucket> tiles(
        static_cast<std::size_t>(total_tile_slots)
    );

    for (std::uint32_t ty = 0; ty < grid_count_y; ++ty) {
        for (std::uint32_t tx = 0; tx < grid_count_x; ++tx) {
            const auto id =
                linear_tile_id(
                    tx,
                    ty,
                    grid_count_x
                );

            auto& tile =
                tiles[static_cast<std::size_t>(id)];

            tile.tile_x = tx;
            tile.tile_y = ty;
        }
    }

    const auto& points =
        dataset.points();

    const std::uint32_t worker_count =
        resolve_thread_count(
            num_threads,
            dataset.point_count()
        );

    if (worker_count > 1) {
        std::cout << "[TILE] parallel bucketing: threads="
                  << worker_count
                  << '\n';
    }

    if (worker_count <= 1) {
        for (std::uint64_t i = 0; i < dataset.point_count(); ++i) {
            const auto& point =
                points[static_cast<std::size_t>(i)];

            const std::uint32_t tx =
                point_tile_coord(
                    point.x,
                    dataset.bbox_min_x(),
                    tile_size_x,
                    grid_count_x
                );

            const std::uint32_t ty =
                point_tile_coord(
                    point.y,
                    dataset.bbox_min_y(),
                    tile_size_y,
                    grid_count_y
                );

            const auto tile_id =
                linear_tile_id(
                    tx,
                    ty,
                    grid_count_x
                );

            auto& tile =
                tiles[static_cast<std::size_t>(tile_id)];

            tile.point_indices.push_back(
                static_cast<std::uint32_t>(i)
            );

            update_tile_bounds(
                tile,
                point
            );
        }

        return tiles;
    }

    std::vector<std::vector<TileBuildBucket>> local_tiles(
        static_cast<std::size_t>(worker_count)
    );
    for (auto& worker_tiles : local_tiles) {
        worker_tiles.resize(
            static_cast<std::size_t>(total_tile_slots)
        );

        for (std::uint32_t ty = 0; ty < grid_count_y; ++ty) {
            for (std::uint32_t tx = 0; tx < grid_count_x; ++tx) {
                const auto id =
                    linear_tile_id(
                        tx,
                        ty,
                        grid_count_x
                    );

                auto& tile =
                    worker_tiles[static_cast<std::size_t>(id)];

                tile.tile_x = tx;
                tile.tile_y = ty;
            }
        }
    }

    gs3d::util::ThreadPool pool(worker_count);
    std::vector<std::future<void>> futures;
    futures.reserve(worker_count);

    const std::uint64_t point_count =
        dataset.point_count();
    const std::uint64_t chunk_size =
        (point_count + worker_count - 1) /
        worker_count;

    std::atomic<bool> failed = false;

    for (std::uint32_t worker_index = 0;
         worker_index < worker_count;
         ++worker_index) {
        const std::uint64_t begin =
            static_cast<std::uint64_t>(worker_index) *
            chunk_size;
        const std::uint64_t end =
            std::min(
                point_count,
                begin + chunk_size
            );

        if (begin >= end) {
            continue;
        }

        futures.push_back(pool.submit(
            [&, worker_index, begin, end] {
                auto& worker_tiles =
                    local_tiles[static_cast<std::size_t>(worker_index)];

                for (std::uint64_t i = begin; i < end; ++i) {
                    if (failed.load(std::memory_order_relaxed)) {
                        return;
                    }

                    const auto& point =
                        points[static_cast<std::size_t>(i)];

                    const std::uint32_t tx =
                        point_tile_coord(
                            point.x,
                            dataset.bbox_min_x(),
                            tile_size_x,
                            grid_count_x
                        );

                    const std::uint32_t ty =
                        point_tile_coord(
                            point.y,
                            dataset.bbox_min_y(),
                            tile_size_y,
                            grid_count_y
                        );

                    const auto tile_id =
                        linear_tile_id(
                            tx,
                            ty,
                            grid_count_x
                        );

                    auto& tile =
                        worker_tiles[static_cast<std::size_t>(tile_id)];

                    tile.point_indices.push_back(
                        static_cast<std::uint32_t>(i)
                    );

                    update_tile_bounds(
                        tile,
                        point
                    );
                }
            }
        ));
    }

    try {
        for (auto& future : futures) {
            future.get();
        }
    } catch (...) {
        failed.store(true, std::memory_order_relaxed);
        throw;
    }

    for (std::size_t tile_index = 0;
         tile_index < tiles.size();
         ++tile_index) {
        auto& merged_tile = tiles[tile_index];

        std::size_t total_indices = 0;
        bool has_points = false;

        for (const auto& worker_tiles : local_tiles) {
            const auto& worker_tile =
                worker_tiles[tile_index];

            total_indices +=
                worker_tile.point_indices.size();

            if (worker_tile.empty()) {
                continue;
            }

            if (!has_points) {
                merged_tile.bbox_min_x = worker_tile.bbox_min_x;
                merged_tile.bbox_min_y = worker_tile.bbox_min_y;
                merged_tile.bbox_min_z = worker_tile.bbox_min_z;

                merged_tile.bbox_max_x = worker_tile.bbox_max_x;
                merged_tile.bbox_max_y = worker_tile.bbox_max_y;
                merged_tile.bbox_max_z = worker_tile.bbox_max_z;

                merged_tile.value_min = worker_tile.value_min;
                merged_tile.value_max = worker_tile.value_max;

                has_points = true;
            } else {
                merged_tile.bbox_min_x =
                    std::min(merged_tile.bbox_min_x, worker_tile.bbox_min_x);
                merged_tile.bbox_min_y =
                    std::min(merged_tile.bbox_min_y, worker_tile.bbox_min_y);
                merged_tile.bbox_min_z =
                    std::min(merged_tile.bbox_min_z, worker_tile.bbox_min_z);

                merged_tile.bbox_max_x =
                    std::max(merged_tile.bbox_max_x, worker_tile.bbox_max_x);
                merged_tile.bbox_max_y =
                    std::max(merged_tile.bbox_max_y, worker_tile.bbox_max_y);
                merged_tile.bbox_max_z =
                    std::max(merged_tile.bbox_max_z, worker_tile.bbox_max_z);

                merged_tile.value_min =
                    std::min(merged_tile.value_min, worker_tile.value_min);
                merged_tile.value_max =
                    std::max(merged_tile.value_max, worker_tile.value_max);
            }
        }

        if (total_indices == 0) {
            continue;
        }

        merged_tile.point_indices.reserve(total_indices);

        for (auto& worker_tiles : local_tiles) {
            auto& worker_tile =
                worker_tiles[tile_index];

            merged_tile.point_indices.insert(
                merged_tile.point_indices.end(),
                std::make_move_iterator(worker_tile.point_indices.begin()),
                std::make_move_iterator(worker_tile.point_indices.end())
            );
        }
    }

    return tiles;
}

[[nodiscard]]
std::vector<Gs3dTileRecord> write_tile_data_file(
    const std::filesystem::path& data_path,
    const Gs3dDataset& dataset,
    const std::vector<TileBuildBucket>& tiles,
    std::uint64_t non_empty_tile_count
) {
    create_parent_directories(data_path);

    std::ofstream file(
        data_path,
        std::ios::binary | std::ios::trunc
    );

    if (!file.is_open()) {
        throw std::runtime_error(
            "Gs3dTileWriter: failed to open tile data file: " +
            data_path.string()
        );
    }

    const auto data_header =
        Gs3dTileFormat::make_data_file_header(
            dataset.header(),
            non_empty_tile_count,
            dataset.point_count()
        );

    Gs3dTileFormat::validate_data_file_header(
        data_header
    );

    write_binary(
        file,
        data_header,
        "Gs3dTileWriter: failed to write tile data file header"
    );

    std::vector<Gs3dTileRecord> records;
    records.reserve(
        static_cast<std::size_t>(non_empty_tile_count)
    );

    std::uint64_t point_data_offset =
        sizeof(Gs3dTileDataFileHeader);

    const auto& points =
        dataset.points();

    std::vector<Gs3dPoint> tile_points;

    for (const auto& tile : tiles) {
        if (tile.empty()) {
            continue;
        }

        tile_points.clear();
        tile_points.reserve(
            static_cast<std::size_t>(tile.point_count())
        );

        for (const auto point_index : tile.point_indices) {
            tile_points.push_back(
                points[static_cast<std::size_t>(point_index)]
            );
        }

        const auto tile_id =
            linear_tile_id(
                tile.tile_x,
                tile.tile_y,
                static_cast<std::uint32_t>(
                    std::max<std::uint32_t>(tile.tile_x + 1, 1)
                )
            );

        /*
         * 注意：上面的 tile_id 不用于定位，只作为调试标识。
         * 下面会在调用处重新覆盖为稳定的全局 tile_id。
         */
        (void) tile_id;

        const std::uint64_t stable_tile_id =
            records.size();

        const auto record =
            Gs3dTileFormat::make_tile_record(
                stable_tile_id,
                tile.tile_x,
                tile.tile_y,
                tile.point_count(),
                point_data_offset,
                tile.bbox_min_x,
                tile.bbox_min_y,
                tile.bbox_min_z,
                tile.bbox_max_x,
                tile.bbox_max_y,
                tile.bbox_max_z,
                tile.value_min,
                tile.value_max
            );

        write_points(
            file,
            tile_points
        );

        point_data_offset +=
            record.point_data_bytes;

        records.push_back(record);
    }

    file.flush();

    if (!file.good()) {
        throw std::runtime_error(
            "Gs3dTileWriter: failed to flush tile data file"
        );
    }

    file.close();

    return records;
}

void write_tile_index_file(
    const std::filesystem::path& index_path,
    const Gs3dDataset& dataset,
    const std::vector<Gs3dTileRecord>& records,
    float tile_size_x,
    float tile_size_y,
    std::uint32_t grid_count_x,
    std::uint32_t grid_count_y
) {
    create_parent_directories(index_path);

    std::ofstream file(
        index_path,
        std::ios::binary | std::ios::trunc
    );

    if (!file.is_open()) {
        throw std::runtime_error(
            "Gs3dTileWriter: failed to open tile index file: " +
            index_path.string()
        );
    }

    const auto index_header =
        Gs3dTileFormat::make_index_file_header(
            dataset.header(),
            static_cast<std::uint64_t>(records.size()),
            dataset.point_count(),
            tile_size_x,
            tile_size_y,
            dataset.bbox_min_x(),
            dataset.bbox_min_y(),
            grid_count_x,
            grid_count_y,
            Gs3dTileSplitMode::XY
        );

    Gs3dTileFormat::validate_index_file_header(
        index_header
    );

    write_binary(
        file,
        index_header,
        "Gs3dTileWriter: failed to write tile index file header"
    );

    for (const auto& record : records) {
        Gs3dTileFormat::validate_tile_record(
            record
        );

        write_binary(
            file,
            record,
            "Gs3dTileWriter: failed to write tile record"
        );
    }

    file.flush();

    if (!file.good()) {
        throw std::runtime_error(
            "Gs3dTileWriter: failed to flush tile index file"
        );
    }

    file.close();
}

} // namespace

Gs3dTileWriteStats Gs3dTileWriter::write(
    const std::filesystem::path& index_path,
    const std::filesystem::path& data_path,
    const Gs3dDataset& dataset,
    const Gs3dTileWriteConfig& config
) {
    gs3d::util::Stopwatch total_timer;

    if (index_path.empty()) {
        throw std::runtime_error(
            "Gs3dTileWriter: index_path is empty"
        );
    }

    if (data_path.empty()) {
        throw std::runtime_error(
            "Gs3dTileWriter: data_path is empty"
        );
    }

    if (!dataset.is_consistent()) {
        throw std::runtime_error(
            "Gs3dTileWriter: source dataset is inconsistent"
        );
    }

    if (dataset.empty()) {
        throw std::runtime_error(
            "Gs3dTileWriter: source dataset is empty"
        );
    }

    if (config.tile_size_x <= 0.0f ||
        config.tile_size_y <= 0.0f) {
        throw std::runtime_error(
            "Gs3dTileWriter: invalid tile size"
        );
    }

    const std::uint32_t grid_count_x =
        compute_grid_count(
            dataset.bbox_min_x(),
            dataset.bbox_max_x(),
            config.tile_size_x
        );

    const std::uint32_t grid_count_y =
        compute_grid_count(
            dataset.bbox_min_y(),
            dataset.bbox_max_y(),
            config.tile_size_y
        );

    if (config.verbose) {
        std::cout << "[TILE] building XY tiles\n";
        std::cout << "tile_size = ["
                  << config.tile_size_x
                  << ", "
                  << config.tile_size_y
                  << "]\n";
        std::cout << "grid_count = ["
                  << grid_count_x
                  << ", "
                  << grid_count_y
                  << "]\n";
    }

    gs3d::util::Stopwatch bucket_timer;
    auto tiles =
        build_tile_buckets(
            dataset,
            config.tile_size_x,
            config.tile_size_y,
            grid_count_x,
            grid_count_y,
            config.num_threads
        );
    const double bucket_build_seconds =
        bucket_timer.elapsed_seconds();

    gs3d::util::Stopwatch tile_stats_timer;
    std::uint64_t non_empty_tile_count = 0;
    std::uint64_t min_tile_points =
        std::numeric_limits<std::uint64_t>::max();
    std::uint64_t max_tile_points = 0;

    for (const auto& tile : tiles) {
        if (tile.empty()) {
            continue;
        }

        ++non_empty_tile_count;

        min_tile_points =
            std::min(
                min_tile_points,
                tile.point_count()
            );

        max_tile_points =
            std::max(
                max_tile_points,
                tile.point_count()
            );
    }

    if (non_empty_tile_count == 0) {
        throw std::runtime_error(
            "Gs3dTileWriter: no non-empty tiles generated"
        );
    }
    const double tile_stats_seconds =
        tile_stats_timer.elapsed_seconds();

    gs3d::util::Stopwatch data_write_timer;
    auto records =
        write_tile_data_file(
            data_path,
            dataset,
            tiles,
            non_empty_tile_count
        );
    const double data_write_seconds =
        data_write_timer.elapsed_seconds();

    gs3d::util::Stopwatch index_write_timer;
    write_tile_index_file(
        index_path,
        dataset,
        records,
        config.tile_size_x,
        config.tile_size_y,
        grid_count_x,
        grid_count_y
    );
    const double index_write_seconds =
        index_write_timer.elapsed_seconds();

    Gs3dTileWriteStats stats;
    stats.index_path = index_path;
    stats.data_path = data_path;
    stats.grid_count_x = grid_count_x;
    stats.grid_count_y = grid_count_y;
    stats.tile_count = non_empty_tile_count;
    stats.total_points = dataset.point_count();
    stats.min_tile_points =
        min_tile_points == std::numeric_limits<std::uint64_t>::max()
            ? 0
            : min_tile_points;
    stats.max_tile_points = max_tile_points;
    stats.index_file_bytes =
        file_size_or_zero(index_path);
    stats.data_file_bytes =
        file_size_or_zero(data_path);
    stats.tile_size_x = config.tile_size_x;
    stats.tile_size_y = config.tile_size_y;
    stats.bucket_build_seconds = bucket_build_seconds;
    stats.tile_stats_seconds = tile_stats_seconds;
    stats.data_write_seconds = data_write_seconds;
    stats.index_write_seconds = index_write_seconds;
    stats.total_write_seconds = total_timer.elapsed_seconds();
    stats.success = true;

    if (config.verbose) {
        std::cout << "[TILE] write completed\n";
        std::cout << "tile_count = "
                  << stats.tile_count
                  << '\n';
        std::cout << "total_points = "
                  << stats.total_points
                  << '\n';
        std::cout << "min_tile_points = "
                  << stats.min_tile_points
                  << '\n';
        std::cout << "max_tile_points = "
                  << stats.max_tile_points
                  << '\n';
        std::cout << "index_file_bytes = "
                  << stats.index_file_bytes
                  << '\n';
        std::cout << "data_file_bytes = "
                  << stats.data_file_bytes
                  << '\n';
        std::cout << "[TIME] tile.bucket_build_seconds = "
                  << stats.bucket_build_seconds
                  << '\n';
        std::cout << "[TIME] tile.tile_stats_seconds = "
                  << stats.tile_stats_seconds
                  << '\n';
        std::cout << "[TIME] tile.data_write_seconds = "
                  << stats.data_write_seconds
                  << '\n';
        std::cout << "[TIME] tile.index_write_seconds = "
                  << stats.index_write_seconds
                  << '\n';
        std::cout << "[TIME] tile.total_write_seconds = "
                  << stats.total_write_seconds
                  << '\n';
    }

    return stats;
}

} // namespace gs3d::preprocess
