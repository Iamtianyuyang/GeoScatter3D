#pragma once

#include "data/Gs3dFormat.hpp"
#include "data/Gs3dTileReader.hpp"
#include "render/PointCloudGpu.hpp"
#include "render/VulkanContext.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <optional>
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
    std::uint64_t resident_tile_count = 0;
    std::uint64_t resident_gpu_buffer_bytes = 0;
};

class PointCloudTileGpu {
public:
    PointCloudTileGpu() = default;

    PointCloudTileGpu(
        const VulkanContext& context,
        VkCommandPool command_pool,
        VkQueue transfer_queue,
        const gs3d::data::Gs3dTileReader& reader,
        const std::vector<std::uint64_t>& tile_ids
    );

    ~PointCloudTileGpu() = default;

    PointCloudTileGpu(const PointCloudTileGpu&) = delete;
    PointCloudTileGpu& operator=(const PointCloudTileGpu&) = delete;

    PointCloudTileGpu(PointCloudTileGpu&&) noexcept = default;
    PointCloudTileGpu& operator=(PointCloudTileGpu&&) noexcept = default;

    void update_from_tiles(
        const VulkanContext& context,
        VkCommandPool command_pool,
        VkQueue transfer_queue,
        const gs3d::data::Gs3dTileReader& reader,
        const std::vector<std::uint64_t>& tile_ids,
        const gs3d::data::Gs3dTileQueryBox* filter_box = nullptr
    );

    /*
     * 步骤 1：磁盘读取（线程安全，可在后台线程调用，不接触 Vulkan）。
     */
    [[nodiscard]]
    static std::vector<gs3d::data::Gs3dPoint> read_tiles(
        const gs3d::data::Gs3dTileReader& reader,
        const std::vector<std::uint64_t>& tile_ids
    );

    /*
     * 步骤 2：GPU 上传（主线程，调用前需等 in-flight fence）。
     * 接受已读取的点，不再访问磁盘。
     */
    void upload_from_points(
        const VulkanContext& context,
        VkCommandPool command_pool,
        VkQueue transfer_queue,
        const std::vector<gs3d::data::Gs3dPoint>& points,
        const std::vector<std::uint64_t>& tile_ids
    );

    void set_resident_tile_budget(
        std::uint32_t max_tiles
    ) noexcept;

    [[nodiscard]]
    PointCloudTileGpuSyncResult sync_from_cached_tiles(
        const VulkanContext& context,
        VkCommandPool command_pool,
        VkQueue transfer_queue,
        const std::vector<std::pair<
            std::uint64_t,
            std::shared_ptr<const std::vector<gs3d::data::Gs3dPoint>>
        >>& tiles
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
        std::uint64_t last_used_tick = 0;
    };

    std::unordered_map<std::uint64_t, ResidentTileGpu> resident_tiles_{};

    std::vector<std::uint64_t> loaded_tile_ids_{};

    PointCloudTileGpuStats stats_{};
    std::uint32_t resident_tile_budget_ = 0;
    std::uint64_t usage_tick_ = 0;

private:
    [[nodiscard]]
    static std::vector<gs3d::data::Gs3dPoint> read_and_merge_tiles(
        const gs3d::data::Gs3dTileReader& reader,
        const std::vector<std::uint64_t>& tile_ids,
        const gs3d::data::Gs3dTileQueryBox* filter_box
    );

    [[nodiscard]]
    static std::uint64_t estimate_total_point_count(
        const gs3d::data::Gs3dTileReader& reader,
        const std::vector<std::uint64_t>& tile_ids
    );

    [[nodiscard]]
    static bool point_inside_box(
        const gs3d::data::Gs3dPoint& point,
        const gs3d::data::Gs3dTileQueryBox& box
    ) noexcept;

    void evict_to_budget(
        const std::unordered_set<std::uint64_t>& pinned_tile_ids
    );
};

} // namespace gs3d::render
