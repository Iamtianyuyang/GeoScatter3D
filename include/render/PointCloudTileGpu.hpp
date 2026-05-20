#pragma once

#include "data/Gs3dFormat.hpp"
#include "data/Gs3dTileReader.hpp"
#include "render/PointCloudGpu.hpp"
#include "render/VulkanContext.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace gs3d::render {

struct PointCloudTileGpuStats {
    std::uint64_t tile_count = 0;
    std::uint64_t point_count = 0;
    std::uint64_t point_bytes = 0;
    std::uint64_t gpu_buffer_bytes = 0;

    bool success = false;
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
    PointCloudGpu gpu_cloud_{};

    std::vector<std::uint64_t> loaded_tile_ids_{};

    PointCloudTileGpuStats stats_{};

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
};

} // namespace gs3d::render
