#pragma once

#include "data/Gs3dLodDataset.hpp"
#include "render/PointCloudGpu.hpp"
#include "render/VulkanContext.hpp"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gs3d::render {

struct PointCloudLodGpuLevel {
    std::string name;

    std::uint32_t level_index = 0;
    std::uint64_t source_point_count = 0;
    std::uint64_t target_point_count = 0;
    std::uint64_t gpu_point_count = 0;

    float voxel_size = 0.0f;

    gs3d::data::Gs3dLodVoxelMode voxel_mode =
        gs3d::data::Gs3dLodVoxelMode::XY;

    PointCloudGpu gpu_cloud;

    [[nodiscard]]
    bool valid() const noexcept;

    [[nodiscard]]
    bool empty() const noexcept;
};

class PointCloudLodGpu {
public:
    PointCloudLodGpu() = default;

    PointCloudLodGpu(
        const VulkanContext& context,
        VkCommandPool command_pool,
        VkQueue transfer_queue,
        const gs3d::data::Gs3dLodDataset& lod_dataset
    );

    ~PointCloudLodGpu() = default;

    PointCloudLodGpu(const PointCloudLodGpu&) = delete;
    PointCloudLodGpu& operator=(const PointCloudLodGpu&) = delete;

    PointCloudLodGpu(PointCloudLodGpu&&) noexcept = default;
    PointCloudLodGpu& operator=(PointCloudLodGpu&&) noexcept = default;

    void upload(
        const VulkanContext& context,
        VkCommandPool command_pool,
        VkQueue transfer_queue,
        const gs3d::data::Gs3dLodDataset& lod_dataset
    );

    void destroy() noexcept;

    [[nodiscard]]
    bool valid() const noexcept;

    [[nodiscard]]
    bool empty() const noexcept;

    [[nodiscard]]
    std::size_t level_count() const noexcept;

    [[nodiscard]]
    const PointCloudLodGpuLevel& level(std::size_t index) const;

    [[nodiscard]]
    PointCloudLodGpuLevel& level(std::size_t index);

    [[nodiscard]]
    const PointCloudLodGpuLevel& highest_detail() const;

    [[nodiscard]]
    const PointCloudLodGpuLevel& lowest_detail() const;

    [[nodiscard]]
    const PointCloudGpu& gpu_cloud(std::size_t index) const;

    [[nodiscard]]
    std::string summary() const;

private:
    std::vector<PointCloudLodGpuLevel> levels_;
};

} // namespace gs3d::render