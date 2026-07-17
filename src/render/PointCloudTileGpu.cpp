#include "render/PointCloudTileGpu.hpp"

#include "render/TileStagingPlan.hpp"

#include <limits>
#include <stdexcept>
#include <vector>

namespace gs3d::render {

namespace {

std::uint64_t point_bytes_for(
    const gs3d::core::PointDataView& points
) noexcept {
    return points.point_count *
           static_cast<std::uint64_t>(sizeof(PointVertex));
}

void check_vk(VkResult result, const char* message) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(message);
    }
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
    std::uint32_t soft_max_tiles
) noexcept {
    resident_tile_budget_ = soft_max_tiles;
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

    // ── 1. 选出本帧要上传的非驻留瓦片，并规划它们在共享 staging 里的偏移 ──
    // 校验所有传入瓦片（含已驻留者）非空，语义与旧实现一致。
    std::vector<std::size_t> candidate_tile_indices;
    std::vector<std::uint64_t> candidate_bytes;
    candidate_tile_indices.reserve(tiles.size());
    candidate_bytes.reserve(tiles.size());
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        const auto& [tile_id, points] = tiles[i];
        if (!points.valid() || points.empty()) {
            throw std::runtime_error(
                "PointCloudTileGpu: cached tile points are missing"
            );
        }
        if (resident_tiles_.contains(tile_id)) {
            continue;
        }
        candidate_tile_indices.push_back(i);
        candidate_bytes.push_back(point_bytes_for(points));
    }

    const TileStagingPlan plan =
        plan_tile_staging_batch(candidate_bytes, max_upload_bytes);

    if (!plan.entries.empty()) {
        // ── 2. 共享 staging：一次分配/复用、一次 map，容纳整批瓦片 ──
        ensure_shared_staging(context, plan.total_bytes);

        void* staging_mapped = nullptr;
        check_vk(
            vkMapMemory(
                context.device(),
                shared_staging_buffer_.memory(),
                0,
                plan.total_bytes,
                0,
                &staging_mapped
            ),
            "PointCloudTileGpu: failed to map shared staging buffer"
        );

        struct PreparedTile {
            std::uint64_t tile_id;
            ResidentTileGpu tile;
            VkDeviceSize staging_offset;
        };
        std::vector<PreparedTile> prepared_tiles;
        prepared_tiles.reserve(plan.entries.size());

        auto* staging_bytes = static_cast<std::uint8_t*>(staging_mapped);
        for (const auto& entry : plan.entries) {
            const std::size_t tile_index =
                candidate_tile_indices[entry.candidate_index];
            const auto& [tile_id, points] = tiles[tile_index];

            ResidentTileGpu tile_gpu;
            tile_gpu.gpu_cloud.prepare_device_buffer(
                context, points.point_count);
            PointCloudGpu::pack_points(
                staging_bytes + entry.offset, points);
            tile_gpu.point_count = points.point_count;
            tile_gpu.point_bytes = entry.bytes;

            prepared_tiles.push_back(PreparedTile{
                tile_id,
                std::move(tile_gpu),
                static_cast<VkDeviceSize>(entry.offset)
            });

            result.uploaded_tile_count += 1;
            result.uploaded_point_count += points.point_count;
        }
        result.uploaded_bytes = plan.total_bytes;

        vkUnmapMemory(
            context.device(),
            shared_staging_buffer_.memory()
        );

        // ── 3. 整批拷贝 + 单次提交 ──
        const VkCommandBuffer command_buffer =
            VulkanBufferUtils::begin_single_time_commands(
                context,
                command_pool
            );
        for (const auto& prepared : prepared_tiles) {
            prepared.tile.gpu_cloud.record_upload_from_external_staging(
                command_buffer,
                shared_staging_buffer_.handle(),
                prepared.staging_offset
            );
        }
        VulkanBufferUtils::end_single_time_commands(
            context,
            command_pool,
            transfer_queue,
            command_buffer
        );

        for (auto& prepared : prepared_tiles) {
            resident_tiles_.emplace(
                prepared.tile_id, std::move(prepared.tile));
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

PointCloudTileGpuSyncResult PointCloudTileGpu::adopt_prepared_tiles(
    const VulkanContext& context,
    VkCommandPool command_pool,
    VkQueue transfer_queue,
    std::vector<PreparedTileUpload>& pending,
    std::uint64_t max_upload_bytes
) {
    PointCloudTileGpuSyncResult result;
    if (pending.empty()) {
        result.resident_tile_count = stats_.resident_tile_count;
        result.resident_gpu_buffer_bytes = stats_.gpu_buffer_bytes;
        result.complete = true;
        return result;
    }

    // ── 1. 从前端截取本帧批次：至少一条，之后按剩余预算继续 ──
    std::size_t batch_count = 0;
    std::uint64_t batch_bytes = 0;
    for (const auto& prepared : pending) {
        const std::uint64_t bytes =
            static_cast<std::uint64_t>(
                prepared.gpu_cloud.vertex_buffer_size()
            );
        if (batch_count > 0 && batch_bytes + bytes > max_upload_bytes) {
            break;
        }
        ++batch_count;
        batch_bytes += bytes;
    }

    // ── 2. 共享 staging：一次 map，打包整批点数据 ──
    ensure_shared_staging(context, batch_bytes);

    void* staging_mapped = nullptr;
    check_vk(
        vkMapMemory(
            context.device(),
            shared_staging_buffer_.memory(),
            0,
            batch_bytes,
            0,
            &staging_mapped
        ),
        "PointCloudTileGpu: failed to map shared staging buffer"
    );

    auto* staging_bytes = static_cast<std::uint8_t*>(staging_mapped);
    std::vector<VkDeviceSize> staging_offsets;
    staging_offsets.reserve(batch_count);
    VkDeviceSize offset = 0;
    for (std::size_t i = 0; i < batch_count; ++i) {
        const auto& prepared = pending[i];
        PointCloudGpu::pack_points(
            staging_bytes + offset, prepared.points);
        staging_offsets.push_back(offset);
        offset += prepared.gpu_cloud.vertex_buffer_size();

        result.uploaded_tile_count += 1;
        result.uploaded_point_count += prepared.points.point_count;
    }
    result.uploaded_bytes = batch_bytes;

    vkUnmapMemory(
        context.device(),
        shared_staging_buffer_.memory()
    );

    // ── 3. 整批拷贝 + 单次提交 ──
    const VkCommandBuffer command_buffer =
        VulkanBufferUtils::begin_single_time_commands(
            context,
            command_pool
        );
    for (std::size_t i = 0; i < batch_count; ++i) {
        pending[i].gpu_cloud.record_upload_from_external_staging(
            command_buffer,
            shared_staging_buffer_.handle(),
            staging_offsets[i]
        );
    }
    VulkanBufferUtils::end_single_time_commands(
        context,
        command_pool,
        transfer_queue,
        command_buffer
    );

    // ── 4. 收编为驻留瓦片，维护统计 ──
    for (std::size_t i = 0; i < batch_count; ++i) {
        auto& prepared = pending[i];
        ResidentTileGpu tile_gpu;
        const std::uint64_t bytes =
            static_cast<std::uint64_t>(
                prepared.gpu_cloud.vertex_buffer_size()
            );
        tile_gpu.point_count = prepared.points.point_count;
        tile_gpu.point_bytes = bytes;
        tile_gpu.gpu_cloud = std::move(prepared.gpu_cloud);
        tile_gpu.last_used_tick = ++usage_tick_;

        loaded_tile_ids_.push_back(prepared.tile_id);
        stats_.tile_count += 1;
        stats_.point_count += tile_gpu.point_count;
        stats_.point_bytes += bytes;
        stats_.gpu_buffer_bytes += bytes;
        resident_tiles_.emplace(prepared.tile_id, std::move(tile_gpu));
    }
    pending.erase(
        pending.begin(),
        pending.begin() + static_cast<std::ptrdiff_t>(batch_count)
    );

    stats_.resident_tile_count =
        static_cast<std::uint64_t>(resident_tiles_.size());
    stats_.success = !loaded_tile_ids_.empty();

    result.resident_tile_count = stats_.resident_tile_count;
    result.resident_gpu_buffer_bytes = stats_.gpu_buffer_bytes;
    result.complete = pending.empty();
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

void PointCloudTileGpu::ensure_shared_staging(
    const VulkanContext& context,
    VkDeviceSize needed
) {
    if (needed == 0) {
        return;
    }
    // Grow-only: keep the buffer when it is already large enough so a full
    // preload reuses a single staging allocation across every frame.
    if (shared_staging_buffer_.valid() &&
        shared_staging_buffer_.size() >= needed) {
        return;
    }
    shared_staging_buffer_.destroy();
    shared_staging_buffer_.create(
        context,
        needed,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
}

} // namespace gs3d::render
