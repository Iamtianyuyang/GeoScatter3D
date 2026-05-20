#pragma once

#include "data/Gs3dDataset.hpp"
#include "data/Gs3dFormat.hpp"
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

    PointCloudGpu(
        const VulkanContext& context,
        VkCommandPool command_pool,
        VkQueue transfer_queue,
        const gs3d::data::Gs3dPoint* points,
        std::uint64_t point_count
    );

    ~PointCloudGpu();

    PointCloudGpu(const PointCloudGpu&) = delete;
    PointCloudGpu& operator=(const PointCloudGpu&) = delete;

    PointCloudGpu(PointCloudGpu&&) noexcept;
    PointCloudGpu& operator=(PointCloudGpu&&) noexcept;

    void upload(
        const VulkanContext& context,
        VkCommandPool command_pool,
        VkQueue transfer_queue,
        const gs3d::data::Gs3dDataset& dataset
    );

    void upload_points(
        const VulkanContext& context,
        VkCommandPool command_pool,
        VkQueue transfer_queue,
        const gs3d::data::Gs3dPoint* points,
        std::uint64_t point_count
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
    VulkanBuffer  vertex_buffer_{};
    std::uint64_t point_count_           = 0;
    VkDeviceSize  vertex_buffer_size_    = 0; // active bytes
    VkDeviceSize  vertex_buffer_capacity_= 0; // allocated bytes

    /*
     * Persistent staging buffer (Vulkan Tutorial / VMA 最佳实践):
     * 分配一次、持久 map，跨上传复用，消除每次上传的
     * vkAllocateMemory + vkMapMemory 调用开销。
     */
    VulkanBuffer         staging_buffer_{};
    void*                staging_mapped_  = nullptr;
    const VulkanContext* staging_context_ = nullptr;

    void ensure_staging(const VulkanContext& context, VkDeviceSize needed);
    void release_staging() noexcept;
};

} // namespace gs3d::render