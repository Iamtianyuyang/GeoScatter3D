#pragma once

#include "data/Gs3dDataset.hpp"
#include "render/VulkanBuffer.hpp"
#include "render/VulkanContext.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace gs3d::render {

class PointCloudGpu {
public:
    PointCloudGpu() = default;

    PointCloudGpu(
        const VulkanContext& context,
        VkCommandPool command_pool,
        VkQueue transfer_queue,
        const gs3d::data::Gs3dDataset& dataset
    );

    ~PointCloudGpu() = default;

    PointCloudGpu(const PointCloudGpu&) = delete;
    PointCloudGpu& operator=(const PointCloudGpu&) = delete;

    PointCloudGpu(PointCloudGpu&&) noexcept = default;
    PointCloudGpu& operator=(PointCloudGpu&&) noexcept = default;

    void upload(
        const VulkanContext& context,
        VkCommandPool command_pool,
        VkQueue transfer_queue,
        const gs3d::data::Gs3dDataset& dataset
    );

    void destroy() noexcept;

    [[nodiscard]]
    bool valid() const noexcept;

    [[nodiscard]]
    VkBuffer vertex_buffer() const noexcept;

    [[nodiscard]]
    VkDeviceSize vertex_buffer_size() const noexcept;

    [[nodiscard]]
    std::uint64_t point_count() const noexcept;

    [[nodiscard]]
    bool empty() const noexcept;

private:
    VulkanBuffer vertex_buffer_{};

    std::uint64_t point_count_ = 0;
    VkDeviceSize vertex_buffer_size_ = 0;
};

} // namespace gs3d::render