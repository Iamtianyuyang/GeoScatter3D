#pragma once

#include "render/VulkanContext.hpp"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>

namespace gs3d::render {

class VulkanBuffer {
public:
    VulkanBuffer() = default;

    VulkanBuffer(
        const VulkanContext& context,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties
    );

    ~VulkanBuffer();

    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;

    VulkanBuffer(VulkanBuffer&& other) noexcept;
    VulkanBuffer& operator=(VulkanBuffer&& other) noexcept;

    void create(
        const VulkanContext& context,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties
    );

    /*
     * 外部内存子绑定模式：buffer 绑定到调用方持有的 VkDeviceMemory 的
     * 指定偏移，本对象不拥有该 memory（destroy 只销毁 buffer）。用于
     * arena 子分配——上万个小 buffer 共享少量大块分配，绕开逐对象
     * vkAllocateMemory 的驱动内核调用开销（实测 ~111µs/次）。
     * 调用方必须保证 memory 的生存期覆盖本 buffer 的使用与销毁，且
     * offset 满足该 buffer 的对齐要求。
     */
    void create_bound(
        const VulkanContext& context,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkDeviceMemory external_memory,
        VkDeviceSize memory_offset,
        VkDeviceSize bound_capacity
    );

    void destroy() noexcept;

    void upload(
        const void* data,
        VkDeviceSize size,
        VkDeviceSize offset = 0
    );

    [[nodiscard]]
    VkBuffer handle() const noexcept;

    [[nodiscard]]
    VkDeviceMemory memory() const noexcept;

    [[nodiscard]]
    VkDeviceSize size() const noexcept;

    [[nodiscard]]
    bool valid() const noexcept;

private:
    const VulkanContext* context_ = nullptr;

    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkDeviceSize size_ = 0;
    // false = memory_ 由外部 arena 持有，destroy 不 vkFreeMemory。
    bool owns_memory_ = true;

public:
    // 查询该 buffer 的内存需求（未创建时构造临时 buffer 探测）。
    [[nodiscard]]
    static VkMemoryRequirements probe_memory_requirements(
        const VulkanContext& context,
        VkDeviceSize size,
        VkBufferUsageFlags usage
    );

    [[nodiscard]]
    static std::uint32_t memory_type_for(
        const VulkanContext& context,
        std::uint32_t type_filter,
        VkMemoryPropertyFlags properties
    );

private:
    [[nodiscard]]
    static std::uint32_t find_memory_type(
        VkPhysicalDevice physical_device,
        std::uint32_t type_filter,
        VkMemoryPropertyFlags properties
    );
};

class VulkanBufferUtils {
public:
    static void copy_buffer(
        const VulkanContext& context,
        VkCommandPool command_pool,
        VkQueue queue,
        VkBuffer src_buffer,
        VkBuffer dst_buffer,
        VkDeviceSize size
    );

    [[nodiscard]]
    static VkCommandBuffer begin_single_time_commands(
        const VulkanContext& context,
        VkCommandPool command_pool
    );

    static void end_single_time_commands(
        const VulkanContext& context,
        VkCommandPool command_pool,
        VkQueue queue,
        VkCommandBuffer command_buffer
    );
};

} // namespace gs3d::render
