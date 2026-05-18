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

private:
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