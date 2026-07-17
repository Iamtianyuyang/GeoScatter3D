#pragma once

#include "core/PointData.hpp"
#include "render/PointCloudGpu.hpp"
#include "render/VulkanContext.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace gs3d::render {

struct PointCloudTileGpuStats {
    std::uint64_t tile_count = 0;
    std::uint64_t point_count = 0;
    std::uint64_t point_bytes = 0;
    std::uint64_t gpu_buffer_bytes = 0;
    std::uint64_t resident_tile_count = 0;

    bool success = false;
};

struct PointCloudTileGpuSyncResult {
    std::uint64_t uploaded_tile_count = 0;
    std::uint64_t uploaded_point_count = 0;
    std::uint64_t uploaded_bytes = 0;
    std::uint64_t resident_tile_count = 0;
    std::uint64_t resident_gpu_buffer_bytes = 0;
    bool complete = false;
};

/*
 * 预加载专用的「已备好上传」瓦片：device-local vertex buffer 已由后台
 * 线程通过 PointCloudGpu::prepare_device_buffer 创建（vkCreateBuffer /
 * vkAllocateMemory 允许对同一 device 并发调用，且不触碰 queue），主线程
 * 每帧只需把点数据经共享 staging 拷入并提交。points 视图指向调用方持有
 * 的 CPU 缓存数据，生存期必须覆盖 adopt_prepared_tiles 的消费。
 */
struct PreparedTileUpload {
    std::uint64_t tile_id = 0;
    PointCloudGpu gpu_cloud{};
    gs3d::core::PointDataView points{};
};

class PointCloudTileGpu {
public:
    PointCloudTileGpu() = default;

    ~PointCloudTileGpu() = default;

    PointCloudTileGpu(const PointCloudTileGpu&) = delete;
    PointCloudTileGpu& operator=(const PointCloudTileGpu&) = delete;

    PointCloudTileGpu(PointCloudTileGpu&&) noexcept = default;
    PointCloudTileGpu& operator=(PointCloudTileGpu&&) noexcept = default;

    void upload_from_points(
        const VulkanContext& context,
        VkCommandPool command_pool,
        VkQueue transfer_queue,
        const gs3d::core::PointDataView& points,
        const std::vector<std::uint64_t>& tile_ids
    );

    void set_resident_tile_budget(
        std::uint32_t soft_max_tiles
    ) noexcept;

    /*
     * Synchronise the GPU-resident tile set with the given inputs.
     *
     *  tiles            — CPU point data for tiles that need uploading
     *                     (only tiles in this list are considered for upload).
     *  required_tile_ids — current GPU working set (IDs only, no point data).
     *                     Already-resident tiles in this set are pinned against
     *                     eviction even when absent from |tiles| (e.g. CPU
     *                     cache miss).  Non-resident tiles are ignored.
     *  max_upload_bytes  — per-frame upload byte budget.
     */
    [[nodiscard]]
    PointCloudTileGpuSyncResult sync_from_cached_tiles(
        const VulkanContext& context,
        VkCommandPool command_pool,
        VkQueue transfer_queue,
        const std::vector<std::pair<std::uint64_t, gs3d::core::PointDataView>>&
            tiles,
        const std::vector<std::uint64_t>& required_tile_ids,
        std::uint64_t max_upload_bytes =
            std::numeric_limits<std::uint64_t>::max()
    );

    /*
     * 预加载专用：从 pending 前端按 max_upload_bytes 消费若干条目，经
     * 共享 staging 一次提交后成为驻留瓦片；被消费的条目从 pending 移除。
     * 每帧至少消费一条（即便单条超预算），保证进度单调、不会停摆。
     * pending 消费完毕时 result.complete = true。不做 LRU 驱逐——预加载
     * 集合就是全量驻留集。
     */
    [[nodiscard]]
    PointCloudTileGpuSyncResult adopt_prepared_tiles(
        const VulkanContext& context,
        VkCommandPool command_pool,
        VkQueue transfer_queue,
        std::vector<PreparedTileUpload>& pending,
        std::uint64_t max_upload_bytes =
            std::numeric_limits<std::uint64_t>::max()
    );

    void clear() noexcept;

    [[nodiscard]]
    bool valid() const noexcept;

    [[nodiscard]]
    bool empty() const noexcept;

    [[nodiscard]]
    const PointCloudGpu& gpu_cloud() const;

    [[nodiscard]]
    PointCloudGpu& gpu_cloud();

    [[nodiscard]]
    const PointCloudGpu& gpu_cloud_for_tile(
        std::uint64_t tile_id
    ) const;

    [[nodiscard]]
    bool has_resident_tile(std::uint64_t tile_id) const noexcept;

    /*
     * Bump the LRU tick for a resident tile so evict_to_budget won't
     * target it.  No-op if the tile is not currently resident.
     * Call after sync_from_cached_tiles for every desired tile whose
     * PointDataView wasn't passed to sync (e.g. not in CPU cache yet).
     */
    void touch_tile(std::uint64_t tile_id) noexcept;

    [[nodiscard]]
    std::uint64_t tile_count() const noexcept;

    [[nodiscard]]
    std::uint64_t point_count() const noexcept;

    [[nodiscard]]
    std::uint64_t point_bytes() const noexcept;

    [[nodiscard]]
    const std::vector<std::uint64_t>& loaded_tile_ids() const noexcept;

    [[nodiscard]]
    const PointCloudTileGpuStats& stats() const noexcept;

private:
    struct ResidentTileGpu {
        PointCloudGpu gpu_cloud{};
        std::uint64_t point_count = 0;
        std::uint64_t point_bytes = 0;
        std::uint64_t last_used_tick = 0;
    };

    std::unordered_map<std::uint64_t, ResidentTileGpu> resident_tiles_{};

    std::vector<std::uint64_t> loaded_tile_ids_{};

    PointCloudTileGpuStats stats_{};
    std::uint32_t resident_tile_budget_ = 0;
    std::uint64_t usage_tick_ = 0;

    /*
     * 一个可复用的共享 staging buffer，承载每次 sync 里整批新瓦片的点数据。
     * 只增长不缩小；每帧上传只在此 map/unmap 一次（拷贝多块瓦片），一次
     * vkQueueSubmit 完成整批拷贝。取代旧的「每块瓦片一个私有 staging」——
     * 那对上万块瓦片的预加载意味着上万次 host 分配且从不释放。
     */
    VulkanBuffer shared_staging_buffer_{};

private:
    void evict_to_budget(
        const std::unordered_set<std::uint64_t>& pinned_tile_ids
    );

    // 确保共享 staging buffer 至少有 needed 字节（不足时重建，够大则复用）。
    void ensure_shared_staging(
        const VulkanContext& context,
        VkDeviceSize needed
    );
};

} // namespace gs3d::render
