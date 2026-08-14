#include "preprocess/Gs3dTileWriter.hpp"
#include "util/Log.hpp"

#include "data/Gs3dTileFormat.hpp"
#include "util/Stopwatch.hpp"
#include "util/ThreadPool.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>
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

struct LocalTileStats {
    std::uint64_t point_count = 0;

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
        return point_count == 0;
    }
};

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
        return std::min<std::uint32_t>(
            requested_threads,
            static_cast<std::uint32_t>(
                std::min<std::uint64_t>(
                    point_count,
                    std::numeric_limits<std::uint32_t>::max()
                )
            )
        );
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

void update_tile_bounds(
    LocalTileStats& tile,
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
        gs3d::util::log::info() << "[TILE] parallel bucketing: threads="
                  << worker_count
                  << ", mode=counting-scatter"
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

    const std::size_t tile_slot_count =
        static_cast<std::size_t>(total_tile_slots);
    if (tile_slot_count >
        std::numeric_limits<std::size_t>::max() /
            static_cast<std::size_t>(worker_count)) {
        throw std::runtime_error(
            "Gs3dTileWriter: worker tile statistics exceed size_t range"
        );
    }

    const std::size_t worker_tile_slot_count =
        tile_slot_count *
        static_cast<std::size_t>(worker_count);
    std::vector<LocalTileStats> local_stats(
        worker_tile_slot_count
    );

    gs3d::util::ThreadPool pool(worker_count);
    std::vector<std::future<void>> futures;
    futures.reserve(worker_count);

    const std::uint64_t point_count =
        dataset.point_count();
    const std::uint64_t chunk_size =
        (point_count + worker_count - 1) /
        worker_count;

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
                auto* worker_stats =
                    local_stats.data() +
                    static_cast<std::size_t>(worker_index) *
                        tile_slot_count;

                for (std::uint64_t i = begin; i < end; ++i) {
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
                        static_cast<std::size_t>(
                            linear_tile_id(
                                tx,
                                ty,
                                grid_count_x
                            )
                        );
                    auto& stats = worker_stats[tile_id];
                    ++stats.point_count;
                    update_tile_bounds(stats, point);
                }
            }
        ));
    }

    for (auto& future : futures) {
        future.get();
    }

    std::vector<std::uint64_t> worker_write_offsets(
        worker_tile_slot_count,
        0
    );
    for (std::size_t tile_index = 0;
         tile_index < tile_slot_count;
         ++tile_index) {
        auto& tile = tiles[tile_index];
        std::uint64_t total_indices = 0;

        for (std::uint32_t worker_index = 0;
             worker_index < worker_count;
             ++worker_index) {
            const std::size_t worker_tile_index =
                static_cast<std::size_t>(worker_index) *
                    tile_slot_count +
                tile_index;
            const auto& stats =
                local_stats[worker_tile_index];

            worker_write_offsets[worker_tile_index] =
                total_indices;
            total_indices += stats.point_count;

            if (stats.empty()) {
                continue;
            }

            tile.bbox_min_x =
                std::min(tile.bbox_min_x, stats.bbox_min_x);
            tile.bbox_min_y =
                std::min(tile.bbox_min_y, stats.bbox_min_y);
            tile.bbox_min_z =
                std::min(tile.bbox_min_z, stats.bbox_min_z);

            tile.bbox_max_x =
                std::max(tile.bbox_max_x, stats.bbox_max_x);
            tile.bbox_max_y =
                std::max(tile.bbox_max_y, stats.bbox_max_y);
            tile.bbox_max_z =
                std::max(tile.bbox_max_z, stats.bbox_max_z);

            tile.value_min =
                std::min(tile.value_min, stats.value_min);
            tile.value_max =
                std::max(tile.value_max, stats.value_max);
        }

        if (total_indices >
            static_cast<std::uint64_t>(
                tile.point_indices.max_size()
            )) {
            throw std::runtime_error(
                "Gs3dTileWriter: tile point count exceeds vector range"
            );
        }
        tile.point_indices.resize(
            static_cast<std::size_t>(total_indices)
        );
    }

    futures.clear();
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
                auto* write_offsets =
                    worker_write_offsets.data() +
                    static_cast<std::size_t>(worker_index) *
                        tile_slot_count;

                for (std::uint64_t i = begin; i < end; ++i) {
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
                        static_cast<std::size_t>(
                            linear_tile_id(
                                tx,
                                ty,
                                grid_count_x
                            )
                        );
                    auto& write_offset =
                        write_offsets[tile_id];
                    tiles[tile_id].point_indices[
                        static_cast<std::size_t>(write_offset++)
                    ] = static_cast<std::uint32_t>(i);
                }
            }
        ));
    }

    for (auto& future : futures) {
        future.get();
    }

    return tiles;
}

[[nodiscard]]
std::vector<Gs3dTileRecord> write_tile_data_file(
    const std::filesystem::path& data_path,
    const Gs3dDataset& dataset,
    const std::vector<TileBuildBucket>& tiles,
    std::uint64_t non_empty_tile_count,
    std::uint32_t num_threads
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

    Gs3dTileFormat::write_data_file_header(
        file,
        data_header
    );

    if (!file.good()) {
        throw std::runtime_error(
            "Gs3dTileWriter: failed to write tile data file header"
        );
    }

    std::vector<Gs3dTileRecord> records;
    records.reserve(
        static_cast<std::size_t>(non_empty_tile_count)
    );

    std::uint64_t point_data_offset =
        sizeof(Gs3dTileDataFileHeader);

    const auto& points =
        dataset.points();
    std::vector<const TileBuildBucket*> non_empty_tiles;
    non_empty_tiles.reserve(
        static_cast<std::size_t>(non_empty_tile_count)
    );
    for (const auto& tile : tiles) {
        if (!tile.empty()) {
            non_empty_tiles.push_back(&tile);
        }
    }

    struct PreparedTileMetadata {
        const TileBuildBucket* tile = nullptr;
        std::uint64_t point_count = 0;
    };

    struct PreparedTileChunk {
        std::vector<PreparedTileMetadata> tiles;
        std::vector<gs3d::data::Gs3dPointWithId> points;
    };

    constexpr std::uint64_t kTargetChunkPoints = 1'000'000ull;
    std::vector<std::pair<std::size_t, std::size_t>> chunk_ranges;
    chunk_ranges.reserve(non_empty_tiles.size());

    std::size_t chunk_begin = 0;
    std::uint64_t chunk_points = 0;
    for (std::size_t i = 0; i < non_empty_tiles.size(); ++i) {
        const auto tile_points =
            non_empty_tiles[i]->point_count();

        if (i > chunk_begin &&
            chunk_points + tile_points > kTargetChunkPoints) {
            chunk_ranges.push_back({chunk_begin, i});
            chunk_begin = i;
            chunk_points = 0;
        }

        chunk_points += tile_points;
    }

    if (chunk_begin < non_empty_tiles.size()) {
        chunk_ranges.push_back(
            {chunk_begin, non_empty_tiles.size()}
        );
    }

    const auto gather_threads =
        resolve_thread_count(
            num_threads,
            static_cast<std::uint64_t>(chunk_ranges.size())
        );

    const auto prepare_chunk =
        [&](std::size_t begin, std::size_t end) {
            PreparedTileChunk prepared;
            std::uint64_t total_chunk_points = 0;
            prepared.tiles.reserve(end - begin);

            for (std::size_t i = begin; i < end; ++i) {
                const auto* tile = non_empty_tiles[i];
                prepared.tiles.push_back(
                    PreparedTileMetadata{tile, tile->point_count()}
                );
                total_chunk_points += tile->point_count();
            }

            prepared.points.reserve(
                static_cast<std::size_t>(total_chunk_points)
            );
            for (const auto& meta : prepared.tiles) {
                for (const auto point_index : meta.tile->point_indices) {
                    const auto& src =
                        points[static_cast<std::size_t>(point_index)];
                    prepared.points.push_back({
                        src.x,
                        src.y,
                        src.z,
                        src.value,
                        static_cast<std::uint32_t>(point_index) + 1
                    });
                }
            }

            return prepared;
        };

    std::uint64_t stable_tile_id = 0;
    const auto write_prepared_chunk =
        [&](const PreparedTileChunk& prepared_chunk) {
            if (!prepared_chunk.points.empty()) {
                Gs3dTileFormat::write_points(
                    file,
                    prepared_chunk.points
                );

                if (!file.good()) {
                    throw std::runtime_error(
                        "Gs3dTileWriter: failed to write tile point data"
                    );
                }
            }

            for (const auto& meta : prepared_chunk.tiles) {
                const auto& tile = *meta.tile;
                const auto record =
                    Gs3dTileFormat::make_tile_record(
                        stable_tile_id,
                        tile.tile_x,
                        tile.tile_y,
                        meta.point_count,
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

                point_data_offset += record.point_data_bytes;
                records.push_back(record);
                ++stable_tile_id;
            }
        };

    if (gather_threads <= 1 || chunk_ranges.size() <= 1) {
        for (const auto [begin, end] : chunk_ranges) {
            write_prepared_chunk(
                prepare_chunk(begin, end)
            );
        }
    } else {
        constexpr std::size_t kMaxPreparedChunksInFlight = 3;
        const auto pipeline_threads =
            std::min<std::size_t>({
                static_cast<std::size_t>(gather_threads),
                chunk_ranges.size(),
                kMaxPreparedChunksInFlight
            });

        gs3d::util::log::info()
            << "[TILE] bounded data pipeline: chunks="
            << chunk_ranges.size()
            << ", threads="
            << pipeline_threads
            << ", max_in_flight="
            << pipeline_threads
            << '\n';

        gs3d::util::ThreadPool pool(
            static_cast<std::uint32_t>(pipeline_threads)
        );
        std::deque<std::future<PreparedTileChunk>> pending;
        std::size_t next_chunk = 0;

        const auto submit_next = [&] {
            const auto [begin, end] =
                chunk_ranges[next_chunk++];
            pending.push_back(
                pool.submit([&, begin, end] {
                    return prepare_chunk(begin, end);
                })
            );
        };

        while (next_chunk < pipeline_threads) {
            submit_next();
        }

        while (!pending.empty()) {
            {
                auto prepared = pending.front().get();
                pending.pop_front();
                write_prepared_chunk(prepared);
            }

            if (next_chunk < chunk_ranges.size()) {
                submit_next();
            }
        }
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

    Gs3dTileFormat::write_index_file_header(
        file,
        index_header
    );

    if (!file.good()) {
        throw std::runtime_error(
            "Gs3dTileWriter: failed to write tile index file header"
        );
    }

    for (const auto& record : records) {
        Gs3dTileFormat::validate_tile_record(
            record,
            index_header.point_stride
        );

        Gs3dTileFormat::write_tile_record(
            file,
            record
        );

        if (!file.good()) {
            throw std::runtime_error(
                "Gs3dTileWriter: failed to write tile record"
            );
        }
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

    if (!dataset.has_point_data()) {
        throw std::runtime_error(
            "Gs3dTileWriter: source dataset has no loaded point data"
        );
    }

    if (config.tile_size_x <= 0.0f ||
        config.tile_size_y <= 0.0f) {
        throw std::runtime_error(
            "Gs3dTileWriter: invalid tile size"
        );
    }

    // Auto-scale tile size so the grid has a manageable number of
    // cells — a 512×512 tile on a dataset spanning millions of units
    // would produce billions of empty tiles and exhaust memory.
    float effective_tile_size_x = config.tile_size_x;
    float effective_tile_size_y = config.tile_size_y;

    const float extent_x = safe_extent(
        dataset.bbox_min_x(), dataset.bbox_max_x());
    const float extent_y = safe_extent(
        dataset.bbox_min_y(), dataset.bbox_max_y());

    // Target ≤ 40 000 tiles (200×200 grid).
    constexpr float kMaxGridPerAxis = 200.0f;
    const float scale_x = std::max(
        1.0f,
        std::ceil(extent_x / effective_tile_size_x) / kMaxGridPerAxis);
    const float scale_y = std::max(
        1.0f,
        std::ceil(extent_y / effective_tile_size_y) / kMaxGridPerAxis);
    const float scale = std::max(scale_x, scale_y);

    effective_tile_size_x *= scale;
    effective_tile_size_y *= scale;

    const std::uint32_t grid_count_x =
        compute_grid_count(
            dataset.bbox_min_x(),
            dataset.bbox_max_x(),
            effective_tile_size_x
        );

    const std::uint32_t grid_count_y =
        compute_grid_count(
            dataset.bbox_min_y(),
            dataset.bbox_max_y(),
            effective_tile_size_y
        );

    if (config.verbose) {
        gs3d::util::log::info() << "[TILE] building XY tiles\n";
        if (scale > 1.0f) {
            gs3d::util::log::info() << "tile_size = ["
                      << effective_tile_size_x
                      << ", "
                      << effective_tile_size_y
                      << "] (auto-scaled "
                      << scale
                      << "× from ["
                      << config.tile_size_x
                      << ", "
                      << config.tile_size_y
                      << "])\n";
        } else {
            gs3d::util::log::info() << "tile_size = ["
                      << config.tile_size_x
                      << ", "
                      << config.tile_size_y
                      << "]\n";
        }
        gs3d::util::log::info() << "grid_count = ["
                  << grid_count_x
                  << ", "
                  << grid_count_y
                  << "]\n";
    }

    gs3d::util::Stopwatch bucket_timer;
    auto tiles =
        build_tile_buckets(
            dataset,
            effective_tile_size_x,
            effective_tile_size_y,
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
            non_empty_tile_count,
            config.num_threads
        );
    const double data_write_seconds =
        data_write_timer.elapsed_seconds();

    gs3d::util::Stopwatch index_write_timer;
    write_tile_index_file(
        index_path,
        dataset,
        records,
        effective_tile_size_x,
        effective_tile_size_y,
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
    stats.tile_size_x = effective_tile_size_x;
    stats.tile_size_y = effective_tile_size_y;
    stats.bucket_build_seconds = bucket_build_seconds;
    stats.tile_stats_seconds = tile_stats_seconds;
    stats.data_write_seconds = data_write_seconds;
    stats.index_write_seconds = index_write_seconds;
    stats.total_write_seconds = total_timer.elapsed_seconds();
    stats.success = true;

    if (config.verbose) {
        gs3d::util::log::info() << "[TILE] write completed\n";
        gs3d::util::log::info() << "tile_count = "
                  << stats.tile_count
                  << '\n';
        gs3d::util::log::info() << "total_points = "
                  << stats.total_points
                  << '\n';
        gs3d::util::log::info() << "min_tile_points = "
                  << stats.min_tile_points
                  << '\n';
        gs3d::util::log::info() << "max_tile_points = "
                  << stats.max_tile_points
                  << '\n';
        gs3d::util::log::info() << "index_file_bytes = "
                  << stats.index_file_bytes
                  << '\n';
        gs3d::util::log::info() << "data_file_bytes = "
                  << stats.data_file_bytes
                  << '\n';
        gs3d::util::log::info() << "[TIME] tile.bucket_build_seconds = "
                  << stats.bucket_build_seconds
                  << '\n';
        gs3d::util::log::info() << "[TIME] tile.tile_stats_seconds = "
                  << stats.tile_stats_seconds
                  << '\n';
        gs3d::util::log::info() << "[TIME] tile.data_write_seconds = "
                  << stats.data_write_seconds
                  << '\n';
        gs3d::util::log::info() << "[TIME] tile.index_write_seconds = "
                  << stats.index_write_seconds
                  << '\n';
        gs3d::util::log::info() << "[TIME] tile.total_write_seconds = "
                  << stats.total_write_seconds
                  << '\n';
        gs3d::util::log::info()
            << "[PERF] tile.bucket_mpoints_per_second = "
            << (
                   stats.bucket_build_seconds > 0.0
                       ? static_cast<double>(stats.total_points) /
                             stats.bucket_build_seconds /
                             1'000'000.0
                       : 0.0
               )
            << '\n';
        gs3d::util::log::info()
            << "[PERF] tile.data_mpoints_per_second = "
            << (
                   stats.data_write_seconds > 0.0
                       ? static_cast<double>(stats.total_points) /
                             stats.data_write_seconds /
                             1'000'000.0
                       : 0.0
               )
            << '\n';
        gs3d::util::log::info()
            << "[PERF] tile.data_mib_per_second = "
            << (
                   stats.data_write_seconds > 0.0
                       ? static_cast<double>(stats.data_file_bytes) /
                             stats.data_write_seconds /
                             (1024.0 * 1024.0)
                       : 0.0
               )
            << '\n';
    }

    return stats;
}

} // namespace gs3d::preprocess
