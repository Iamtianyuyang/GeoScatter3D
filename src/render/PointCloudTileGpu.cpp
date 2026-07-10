#include "render/PointCloudTileGpu.hpp"

#include "render/FrameUploadBudget.hpp"

#include <limits>
#include <stdexcept>

namespace gs3d::render {

namespace {

std::uint64_t point_bytes_for(
    const gs3d::core::PointDataView& points
) noexcept {
    return points.point_count *
           static_cast<std::uint64_t>(sizeof(PointVertex));
}

} // namespace

void PointCloudTileGpu::upload_from_points(
    const VulkanContext& context,
    VkCommandPool command_pool,
    VkQueue transfer_queue,
    const gs3d::core::PointDataView& points,
    const std::vector<std::uint64_t>& tile_ids
) {
    if (points.empty() || tile_ids.empty()) {
        clear();
        return;
    }

    clear();

    ResidentTileGpu aggregate_tile;
    aggregate_tile.gpu_cloud.upload(
        context,
        command_pool,
        transfer_queue,
        points
    );
    aggregate_tile.point_count = points.point_count;
    aggregate_tile.point_bytes = point_bytes_for(points);

    resident_tiles_.emplace(
        std::numeric_limits<std::uint64_t>::max(),
        std::move(aggregate_tile)
    );

    loaded_tile_ids_ = tile_ids;
    stats_.tile_count = static_cast<std::uint64_t>(tile_ids.size());
    stats_.point_count = points.point_count;
    stats_.point_bytes = point_bytes_for(points);
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
    const std::vector<std::pair<std::uint64_t, gs3d::core::PointDataView>>&
        tiles,
    const std::vector<std::uint64_t>& required_tile_ids,
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
    std::vector<std::pair<std::uint64_t, ResidentTileGpu>> prepared_tiles;
    prepared_tiles.reserve(tiles.size());

    for (const auto& [tile_id, points] : tiles) {
        if (!points.valid() || points.empty()) {
            throw std::runtime_error(
                "PointCloudTileGpu: cached tile points are missing"
            );
        }

        const auto point_bytes = point_bytes_for(points);

        if (resident_tiles_.contains(tile_id)) {
            continue;
        }

        if (!upload_budget.try_reserve(point_bytes)) {
            continue;
        }

        ResidentTileGpu tile_gpu;
        tile_gpu.gpu_cloud.prepare_upload(context, points);
        tile_gpu.point_count = points.point_count;
        tile_gpu.point_bytes = point_bytes;
        prepared_tiles.emplace_back(tile_id, std::move(tile_gpu));
        result.uploaded_tile_count += 1;
        result.uploaded_point_count += points.point_count;
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
        active_point_count += points.point_count;
        active_point_bytes += point_bytes_for(points);
    }

    // Pin required+resident tiles that were absent from |tiles|
    // (e.g. CPU cache miss).  These tiles are part of the current GPU
    // working set and must not be evicted, but they have no point data
    // in this call so they contribute nothing to loaded_tile_ids_ or
    // point statistics.
    for (const auto tile_id : required_tile_ids) {
        if (active_tile_ids.contains(tile_id)) {
            continue;
        }
        const auto resident = resident_tiles_.find(tile_id);
        if (resident == resident_tiles_.end()) {
            continue;
        }
        resident->second.last_used_tick = ++usage_tick_;
        active_tile_ids.insert(tile_id);
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

void PointCloudTileGpu::touch_tile(std::uint64_t tile_id) noexcept {
    auto it = resident_tiles_.find(tile_id);
    if (it != resident_tiles_.end()) {
        it->second.last_used_tick = ++usage_tick_;
    }
}

void PointCloudTileGpu::evict_to_budget(
    const std::unordered_set<std::uint64_t>& pinned_tile_ids
) {
    if (resident_tile_budget_ == 0 ||
        resident_tiles_.size() <= resident_tile_budget_) {
        return;
    }

    while (resident_tiles_.size() > resident_tile_budget_) {
        bool found = false;
        std::uint64_t eviction_tile_id = 0;
        std::uint64_t oldest_tick =
            std::numeric_limits<std::uint64_t>::max();

        for (const auto& [tile_id, resident] : resident_tiles_) {
            if (pinned_tile_ids.contains(tile_id)) {
                continue;
            }

            if (resident.last_used_tick < oldest_tick) {
                oldest_tick = resident.last_used_tick;
                eviction_tile_id = tile_id;
                found = true;
            }
        }

        if (!found) {
            break;
        }

        resident_tiles_.erase(eviction_tile_id);
    }
}

} // namespace gs3d::render
