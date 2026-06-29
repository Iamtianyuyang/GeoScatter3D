#include "render/PointCloudTileGpu.hpp"
#include "render/FrameUploadBudget.hpp"
#include "util/ThreadPool.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace gs3d::render {

PointCloudTileGpu::PointCloudTileGpu(
    const VulkanContext& context,
    VkCommandPool command_pool,
    VkQueue transfer_queue,
    const gs3d::data::Gs3dTileReader& reader,
    const std::vector<std::uint64_t>& tile_ids
) {
    update_from_tiles(
        context,
        command_pool,
        transfer_queue,
        reader,
        tile_ids
    );
}

void PointCloudTileGpu::update_from_tiles(
    const VulkanContext& context,
    VkCommandPool command_pool,
    VkQueue transfer_queue,
    const gs3d::data::Gs3dTileReader& reader,
    const std::vector<std::uint64_t>& tile_ids,
    const gs3d::data::Gs3dTileQueryBox* filter_box
) {
    if (!reader.valid()) {
        throw std::runtime_error(
            "PointCloudTileGpu: tile reader is invalid"
        );
    }

    if (tile_ids.empty()) {
        clear();
        return;
    }

    auto merged_points =
        read_and_merge_tiles(
            reader,
            tile_ids,
            filter_box
        );

    if (merged_points.empty()) {
        clear();
        return;
    }

    clear();

    ResidentTileGpu aggregate_tile;
    aggregate_tile.gpu_cloud.upload_points(
        context,
        command_pool,
        transfer_queue,
        merged_points.data(),
        static_cast<std::uint64_t>(merged_points.size())
    );
    aggregate_tile.point_count =
        static_cast<std::uint64_t>(merged_points.size());

    resident_tiles_.emplace(
        std::numeric_limits<std::uint64_t>::max(),
        std::move(aggregate_tile)
    );

    loaded_tile_ids_ = tile_ids;
    stats_.tile_count = static_cast<std::uint64_t>(tile_ids.size());
    stats_.point_count = static_cast<std::uint64_t>(merged_points.size());
    stats_.point_bytes =
        stats_.point_count *
        static_cast<std::uint64_t>(sizeof(gs3d::data::Gs3dPoint));
    stats_.gpu_buffer_bytes = stats_.point_bytes;
    stats_.resident_tile_count = 1;
    stats_.success = true;
}

void PointCloudTileGpu::clear() noexcept {
    resident_tiles_.clear();
    loaded_tile_ids_.clear();
    stats_ = {};
    usage_tick_ = 0;
}

bool PointCloudTileGpu::valid() const noexcept {
    return !loaded_tile_ids_.empty() &&
           stats_.success &&
           stats_.point_count > 0;
}

bool PointCloudTileGpu::empty() const noexcept {
    return !valid();
}

const PointCloudGpu& PointCloudTileGpu::gpu_cloud() const {
    if (!valid() || resident_tiles_.size() != 1) {
        throw std::runtime_error(
            "PointCloudTileGpu: aggregate gpu cloud is not available"
        );
    }

    return resident_tiles_.begin()->second.gpu_cloud;
}

PointCloudGpu& PointCloudTileGpu::gpu_cloud() {
    if (!valid() || resident_tiles_.size() != 1) {
        throw std::runtime_error(
            "PointCloudTileGpu: aggregate gpu cloud is not available"
        );
    }

    return resident_tiles_.begin()->second.gpu_cloud;
}

const PointCloudGpu& PointCloudTileGpu::gpu_cloud_for_tile(
    std::uint64_t tile_id
) const {
    const auto it = resident_tiles_.find(tile_id);
    if (it == resident_tiles_.end() ||
        !it->second.gpu_cloud.valid()) {
        throw std::runtime_error(
            "PointCloudTileGpu: tile gpu cloud is not available"
        );
    }

    return it->second.gpu_cloud;
}

bool PointCloudTileGpu::has_resident_tile(
    std::uint64_t tile_id
) const noexcept {
    const auto found = resident_tiles_.find(tile_id);
    return
        found != resident_tiles_.end() &&
        found->second.gpu_cloud.valid();
}

std::uint64_t PointCloudTileGpu::tile_count() const noexcept {
    return stats_.tile_count;
}

std::uint64_t PointCloudTileGpu::point_count() const noexcept {
    return stats_.point_count;
}

std::uint64_t PointCloudTileGpu::point_bytes() const noexcept {
    return stats_.point_bytes;
}

const std::vector<std::uint64_t>&
PointCloudTileGpu::loaded_tile_ids() const noexcept {
    return loaded_tile_ids_;
}

const PointCloudTileGpuStats&
PointCloudTileGpu::stats() const noexcept {
    return stats_;
}

void PointCloudTileGpu::set_resident_tile_budget(
    std::uint32_t max_tiles
) noexcept {
    resident_tile_budget_ = max_tiles;
}

PointCloudTileGpuSyncResult PointCloudTileGpu::sync_from_cached_tiles(
    const VulkanContext& context,
    VkCommandPool command_pool,
    VkQueue transfer_queue,
    const std::vector<std::pair<
        std::uint64_t,
        std::shared_ptr<const std::vector<gs3d::data::Gs3dPoint>>
    >>& tiles,
    std::uint64_t max_upload_bytes
) {
    if (tiles.empty()) {
        clear();
        PointCloudTileGpuSyncResult result;
        result.complete = true;
        return result;
    }

    PointCloudTileGpuSyncResult result;
    FrameUploadBudget upload_budget(max_upload_bytes);
    std::vector<std::pair<std::uint64_t, ResidentTileGpu>>
        prepared_tiles;
    prepared_tiles.reserve(tiles.size());

    for (const auto& [tile_id, points] : tiles) {
        if (!points || points->empty()) {
            throw std::runtime_error(
                "PointCloudTileGpu: cached tile points are missing"
            );
        }

        const auto point_count =
            static_cast<std::uint64_t>(points->size());
        const auto point_bytes =
            point_count *
            static_cast<std::uint64_t>(sizeof(gs3d::data::Gs3dPoint));

        if (resident_tiles_.contains(tile_id)) {
            continue;
        }

        if (!upload_budget.try_reserve(point_bytes)) {
            continue;
        }

        ResidentTileGpu tile_gpu;
        tile_gpu.gpu_cloud.prepare_upload(
            context,
            points->data(),
            point_count
        );
        tile_gpu.point_count = point_count;
        prepared_tiles.emplace_back(tile_id, std::move(tile_gpu));
        result.uploaded_tile_count += 1;
        result.uploaded_point_count += point_count;
        result.uploaded_bytes = upload_budget.reserved_bytes();
    }

    if (!prepared_tiles.empty()) {
        const VkCommandBuffer command_buffer =
            VulkanBufferUtils::begin_single_time_commands(
                context,
                command_pool
            );
        for (const auto& [tile_id, tile] : prepared_tiles) {
            (void)tile_id;
            tile.gpu_cloud.record_prepared_upload(command_buffer);
        }
        VulkanBufferUtils::end_single_time_commands(
            context,
            command_pool,
            transfer_queue,
            command_buffer
        );

        for (auto& [tile_id, tile] : prepared_tiles) {
            resident_tiles_.emplace(tile_id, std::move(tile));
        }
    }

    std::unordered_set<std::uint64_t> active_tile_ids;
    active_tile_ids.reserve(tiles.size());
    loaded_tile_ids_.clear();
    loaded_tile_ids_.reserve(tiles.size());

    std::uint64_t active_point_count = 0;
    std::uint64_t active_point_bytes = 0;
    for (const auto& [tile_id, points] : tiles) {
        const auto resident = resident_tiles_.find(tile_id);
        if (resident == resident_tiles_.end()) {
            continue;
        }

        resident->second.last_used_tick = ++usage_tick_;
        loaded_tile_ids_.push_back(tile_id);
        active_tile_ids.insert(tile_id);
        const auto point_count =
            static_cast<std::uint64_t>(points->size());
        active_point_count += point_count;
        active_point_bytes +=
            point_count *
            static_cast<std::uint64_t>(sizeof(gs3d::data::Gs3dPoint));
    }

    evict_to_budget(active_tile_ids);

    std::uint64_t resident_gpu_bytes = 0;
    for (const auto& [tile_id, resident] : resident_tiles_) {
        (void)tile_id;
        resident_gpu_bytes +=
            static_cast<std::uint64_t>(
                resident.gpu_cloud.vertex_buffer_size()
            );
    }

    stats_.tile_count =
        static_cast<std::uint64_t>(loaded_tile_ids_.size());
    stats_.point_count = active_point_count;
    stats_.point_bytes = active_point_bytes;
    stats_.gpu_buffer_bytes = resident_gpu_bytes;
    stats_.resident_tile_count =
        static_cast<std::uint64_t>(resident_tiles_.size());
    stats_.success = !loaded_tile_ids_.empty();

    result.resident_tile_count = stats_.resident_tile_count;
    result.resident_gpu_buffer_bytes = resident_gpu_bytes;
    result.complete = loaded_tile_ids_.size() == tiles.size();
    return result;
}

void PointCloudTileGpu::evict_to_budget(
    const std::unordered_set<std::uint64_t>& pinned_tile_ids
) {
    if (resident_tile_budget_ == 0 ||
        resident_tiles_.size() <= resident_tile_budget_) {
        return;
    }

    while (resident_tiles_.size() > resident_tile_budget_) {
        std::optional<std::uint64_t> eviction_tile_id;
        std::uint64_t oldest_tick =
            std::numeric_limits<std::uint64_t>::max();

        for (const auto& [tile_id, resident] : resident_tiles_) {
            if (pinned_tile_ids.contains(tile_id)) {
                continue;
            }

            if (resident.last_used_tick < oldest_tick) {
                oldest_tick = resident.last_used_tick;
                eviction_tile_id = tile_id;
            }
        }

        if (!eviction_tile_id.has_value()) {
            break;
        }

        resident_tiles_.erase(*eviction_tile_id);
    }
}

std::vector<gs3d::data::Gs3dPoint>
PointCloudTileGpu::read_and_merge_tiles(
    const gs3d::data::Gs3dTileReader& reader,
    const std::vector<std::uint64_t>& tile_ids,
    const gs3d::data::Gs3dTileQueryBox* filter_box
) {
    const std::uint64_t total_point_count =
        estimate_total_point_count(
            reader,
            tile_ids
        );

    if (total_point_count == 0) {
        return {};
    }

    if (total_point_count >
        static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max()
        )) {
        throw std::runtime_error(
            "PointCloudTileGpu: merged point count exceeds size_t range"
        );
    }

    std::vector<gs3d::data::Gs3dPoint> merged_points;
    merged_points.reserve(
        static_cast<std::size_t>(
            total_point_count
        )
    );

    for (const auto tile_id : tile_ids) {
        auto tile_points =
            reader.read_tile_points(tile_id);

        if (!filter_box) {
            merged_points.insert(
                merged_points.end(),
                std::make_move_iterator(tile_points.begin()),
                std::make_move_iterator(tile_points.end())
            );
            continue;
        }

        for (const auto& point : tile_points) {
            if (point_inside_box(point, *filter_box)) {
                merged_points.push_back(point);
            }
        }
    }

    if (!filter_box &&
        merged_points.size() !=
            static_cast<std::size_t>(total_point_count)) {
        throw std::runtime_error(
            "PointCloudTileGpu: merged point count mismatch"
        );
    }

    return merged_points;
}

std::uint64_t PointCloudTileGpu::estimate_total_point_count(
    const gs3d::data::Gs3dTileReader& reader,
    const std::vector<std::uint64_t>& tile_ids
) {
    std::uint64_t total = 0;

    for (const auto tile_id : tile_ids) {
        const auto& record =
            reader.record(tile_id);

        if (total >
            std::numeric_limits<std::uint64_t>::max() -
            record.point_count) {
            throw std::runtime_error(
                "PointCloudTileGpu: total point count overflow"
            );
        }

        total += record.point_count;
    }

    return total;
}

bool PointCloudTileGpu::point_inside_box(
    const gs3d::data::Gs3dPoint& point,
    const gs3d::data::Gs3dTileQueryBox& box
) noexcept {
    return point.x >= box.min_x &&
           point.x <= box.max_x &&
           point.y >= box.min_y &&
           point.y <= box.max_y &&
           point.z >= box.min_z &&
           point.z <= box.max_z;
}

std::vector<gs3d::data::Gs3dPoint>
PointCloudTileGpu::read_tiles(
    const gs3d::data::Gs3dTileReader& reader,
    const std::vector<std::uint64_t>& tile_ids
) {
    if (tile_ids.empty()) {
        return {};
    }

    /*
     * 线程池并行读取（Cesium-Native AsyncSystem / PDAL 7-thread 模式）：
     * 每个 tile 的磁盘读取提交为独立任务，N 个 tile 由 min(N, pool) 线程并行处理。
     *
     * 线程安全：read_tile_points 每次调用独立开文件描述符，无共享状态。
     * 单 tile 或少量 tile 退化为顺序读，开销可忽略。
     *
     * 按磁盘偏移排序：HDD 顺序读加速；SSD 也受益于 OS 预取预测。
     */
    std::vector<std::uint64_t> sorted_ids = tile_ids;
    std::sort(sorted_ids.begin(), sorted_ids.end(),
        [&reader](std::uint64_t a, std::uint64_t b) {
            return reader.record(a).point_data_offset <
                   reader.record(b).point_data_offset;
        }
    );

    // Static pool: created once, lives for the program's duration.
    // Cesium-Native uses a similarly long-lived AsyncSystem thread pool.
    static gs3d::util::ThreadPool pool(
        gs3d::util::recommended_io_threads()
    );

    // Submit each tile as an independent read task
    std::vector<std::future<std::vector<gs3d::data::Gs3dPoint>>> futures;
    futures.reserve(sorted_ids.size());
    for (const auto tid : sorted_ids) {
        futures.push_back(pool.submit([&reader, tid] {
            return reader.read_tile_points(tid);
        }));
    }

    // Pre-allocate result buffer (avoids repeated reallocations during merge)
    std::uint64_t total_points = 0;
    for (const auto tid : sorted_ids) {
        total_points += reader.record(tid).point_count;
    }

    std::vector<gs3d::data::Gs3dPoint> result;
    result.reserve(static_cast<std::size_t>(total_points));

    // Collect in sorted order (preserves disk-offset ordering for coherent render)
    for (auto& f : futures) {
        auto tile_points = f.get();
        result.insert(
            result.end(),
            std::make_move_iterator(tile_points.begin()),
            std::make_move_iterator(tile_points.end())
        );
    }

    return result;
}

void PointCloudTileGpu::upload_from_points(
    const VulkanContext& context,
    VkCommandPool command_pool,
    VkQueue transfer_queue,
    const std::vector<gs3d::data::Gs3dPoint>& points,
    const std::vector<std::uint64_t>& tile_ids
) {
    if (points.empty()) {
        clear();
        return;
    }

    clear();

    ResidentTileGpu aggregate_tile;
    aggregate_tile.gpu_cloud.upload_points(
        context,
        command_pool,
        transfer_queue,
        points.data(),
        static_cast<std::uint64_t>(points.size())
    );
    aggregate_tile.point_count =
        static_cast<std::uint64_t>(points.size());

    resident_tiles_.emplace(
        std::numeric_limits<std::uint64_t>::max(),
        std::move(aggregate_tile)
    );

    loaded_tile_ids_ = tile_ids;

    stats_.tile_count  = static_cast<std::uint64_t>(tile_ids.size());
    stats_.point_count = static_cast<std::uint64_t>(points.size());
    stats_.point_bytes = stats_.point_count *
        static_cast<std::uint64_t>(sizeof(gs3d::data::Gs3dPoint));
    stats_.gpu_buffer_bytes = stats_.point_bytes;
    stats_.resident_tile_count = 1;
    stats_.success = true;
}

} // namespace gs3d::render
