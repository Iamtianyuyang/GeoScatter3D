#pragma once

#include "render/VulkanContext.hpp"

#include <vulkan/vulkan.h>

namespace gs3d::render {

class VulkanDepthBuffer {
public:
    VulkanDepthBuffer() = default;

    VulkanDepthBuffer(
        const VulkanContext& context,
        VkExtent2D extent
    );

    ~VulkanDepthBuffer();

    VulkanDepthBuffer(const VulkanDepthBuffer&) = delete;
    VulkanDepthBuffer& operator=(const VulkanDepthBuffer&) = delete;

    VulkanDepthBuffer(VulkanDepthBuffer&& other) noexcept;
    VulkanDepthBuffer& operator=(VulkanDepthBuffer&& other) noexcept;

    void create(
        const VulkanContext& context,
        VkExtent2D extent
    );

    void destroy() noexcept;

    [[nodiscard]]
    VkImage image() const noexcept;

    [[nodiscard]]
    VkDeviceMemory memory() const noexcept;

    [[nodiscard]]
    VkImageView image_view() const noexcept;

    [[nodiscard]]
    VkFormat format() const noexcept;

    [[nodiscard]]
    VkExtent2D extent() const noexcept;

    [[nodiscard]]
    bool valid() const noexcept;

    [[nodiscard]]
    static VkFormat find_depth_format(
        const VulkanContext& context
    );

private:
    const VulkanContext* context_ = nullptr;

    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView image_view_ = VK_NULL_HANDLE;

    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};

private:
    [[nodiscard]]
    static std::uint32_t find_memory_type(
        VkPhysicalDevice physical_device,
        std::uint32_t type_filter,
        VkMemoryPropertyFlags properties
    );

    [[nodiscard]]
    static VkFormat find_supported_format(
        VkPhysicalDevice physical_device,
        const VkFormat* candidates,
        std::uint32_t candidate_count,
        VkImageTiling tiling,
        VkFormatFeatureFlags features
    );
};

} // namespace gs3d::render