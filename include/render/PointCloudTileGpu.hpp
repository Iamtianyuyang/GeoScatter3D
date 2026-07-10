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
        std::uint32_t max_tiles
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

private:
    void evict_to_budget(
        const std::unordered_set<std::uint64_t>& pinned_tile_ids
    );
};

} // namespace gs3d::render
