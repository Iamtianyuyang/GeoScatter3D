#pragma once

#include "core/PointData.hpp"
#include "render/VulkanBuffer.hpp"
#include "render/VulkanContext.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace gs3d::render {

struct PointVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float value = 0.0f;
    std::uint32_t point_id = 0;
};

class PointCloudGpu {
public:
    PointCloudGpu() = default;

    PointCloudGpu(
        const VulkanContext& context,
        VkCommandPool command_pool,
        VkQueue transfer_queue,
        const gs3d::core::PointDataView& points
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
        const gs3d::core::PointDataView& points
    );

    void upload_points(
        const VulkanContext& context,
        VkCommandPool command_pool,
        VkQueue transfer_queue,
        const gs3d::core::PointDataView& points
    );

    void prepare_upload(
        const VulkanContext& context,
        const gs3d::core::PointDataView& points
    );

    void record_prepared_upload(
        VkCommandBuffer command_buffer
    ) const;

    /*
     * 共享 staging 批量上传（见 TileStagingPlan.hpp）：只分配/复用本 cloud
     * 的 device-local vertex buffer，不创建私有 staging。调用方把点数据用
     * pack_points 打进外部 staging buffer，再用 record_upload_from_external_staging
     * 记录拷贝。一个共享 staging + 一次提交即可服务整批瓦片，避免逐块
     * 分配私有 staging（预加载上万瓦片时那是上万次永不释放的 host 分配）。
     */
    void prepare_device_buffer(
        const VulkanContext& context,
        std::uint64_t point_count
    );

    void record_upload_from_external_staging(
        VkCommandBuffer command_buffer,
        VkBuffer staging_buffer,
        VkDeviceSize staging_offset
    ) const;

    // 把点视图按 PointVertex 布局打进调用方已 map 的内存。
    static void pack_points(
        void* destination,
        const gs3d::core::PointDataView& points
    );

    // point_count 个 PointVertex 记录的字节数（打包后 = GPU buffer 尺寸）。
    [[nodiscard]]
    static VkDeviceSize packed_point_bytes(std::uint64_t point_count);

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
