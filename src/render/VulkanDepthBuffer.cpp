#include "render/VulkanDepthBuffer.hpp"

#include <stdexcept>
#include <utility>

namespace gs3d::render {

namespace {

void check_vk(VkResult result, const char* message) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(message);
    }
}

} // namespace

VulkanDepthBuffer::VulkanDepthBuffer(
    const VulkanContext& context,
    VkExtent2D extent
) {
    create(context, extent);
}

VulkanDepthBuffer::~VulkanDepthBuffer() {
    destroy();
}

VulkanDepthBuffer::VulkanDepthBuffer(
    VulkanDepthBuffer&& other
) noexcept {
    context_ = other.context_;
    image_ = other.image_;
    memory_ = other.memory_;
    image_view_ = other.image_view_;
    format_ = other.format_;
    extent_ = other.extent_;

    other.context_ = nullptr;
    other.image_ = VK_NULL_HANDLE;
    other.memory_ = VK_NULL_HANDLE;
    other.image_view_ = VK_NULL_HANDLE;
    other.format_ = VK_FORMAT_UNDEFINED;
    other.extent_ = {};
}

VulkanDepthBuffer& VulkanDepthBuffer::operator=(
    VulkanDepthBuffer&& other
) noexcept {
    if (this != &other) {
        destroy();

        context_ = other.context_;
        image_ = other.image_;
        memory_ = other.memory_;
        image_view_ = other.image_view_;
        format_ = other.format_;
        extent_ = other.extent_;

        other.context_ = nullptr;
        other.image_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
        other.image_view_ = VK_NULL_HANDLE;
        other.format_ = VK_FORMAT_UNDEFINED;
        other.extent_ = {};
    }

    return *this;
}

void VulkanDepthBuffer::create(
    const VulkanContext& context,
    VkExtent2D extent,
    VkImageUsageFlags extra_usage
) {
    if (extent.width == 0 || extent.height == 0) {
        throw std::runtime_error(
            "VulkanDepthBuffer: invalid extent"
        );
    }

    destroy();

    context_ = &context;
    extent_ = extent;
    format_ = find_depth_format(context);

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = extent.width;
    image_info.extent.height = extent.height;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = format_;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage =
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
        extra_usage;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    check_vk(
        vkCreateImage(
            context.device(),
            &image_info,
            nullptr,
            &image_
        ),
        "VulkanDepthBuffer: failed to create depth image"
    );

    VkMemoryRequirements memory_requirements{};
    vkGetImageMemoryRequirements(
        context.device(),
        image_,
        &memory_requirements
    );

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = memory_requirements.size;
    alloc_info.memoryTypeIndex = find_memory_type(
        context.physical_device(),
        memory_requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    check_vk(
        vkAllocateMemory(
            context.device(),
            &alloc_info,
            nullptr,
            &memory_
        ),
        "VulkanDepthBuffer: failed to allocate depth memory"
    );

    check_vk(
        vkBindImageMemory(
            context.device(),
            image_,
            memory_,
            0
        ),
        "VulkanDepthBuffer: failed to bind depth image memory"
    );

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format_;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    check_vk(
        vkCreateImageView(
            context.device(),
            &view_info,
            nullptr,
            &image_view_
        ),
        "VulkanDepthBuffer: failed to create depth image view"
    );
}

void VulkanDepthBuffer::destroy() noexcept {
    if (!context_) {
        return;
    }

    const VkDevice device = context_->device();

    if (image_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device, image_view_, nullptr);
        image_view_ = VK_NULL_HANDLE;
    }

    if (image_ != VK_NULL_HANDLE) {
        vkDestroyImage(device, image_, nullptr);
        image_ = VK_NULL_HANDLE;
    }

    if (memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device, memory_, nullptr);
        memory_ = VK_NULL_HANDLE;
    }

    context_ = nullptr;
    format_ = VK_FORMAT_UNDEFINED;
    extent_ = {};
}

VkImage VulkanDepthBuffer::image() const noexcept {
    return image_;
}

VkDeviceMemory VulkanDepthBuffer::memory() const noexcept {
    return memory_;
}

VkImageView VulkanDepthBuffer::image_view() const noexcept {
    return image_view_;
}

VkFormat VulkanDepthBuffer::format() const noexcept {
    return format_;
}

VkExtent2D VulkanDepthBuffer::extent() const noexcept {
    return extent_;
}

bool VulkanDepthBuffer::valid() const noexcept {
    return context_ != nullptr &&
           image_ != VK_NULL_HANDLE &&
           memory_ != VK_NULL_HANDLE &&
           image_view_ != VK_NULL_HANDLE &&
           format_ != VK_FORMAT_UNDEFINED &&
           extent_.width > 0 &&
           extent_.height > 0;
}

VkFormat VulkanDepthBuffer::find_depth_format(
    const VulkanContext& context
) {
    const VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT
    };

    return find_supported_format(
        context.physical_device(),
        candidates,
        3,
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}

std::uint32_t VulkanDepthBuffer::find_memory_type(
    VkPhysicalDevice physical_device,
    std::uint32_t type_filter,
    VkMemoryPropertyFlags properties
) {
    VkPhysicalDeviceMemoryProperties memory_properties{};
    vkGetPhysicalDeviceMemoryProperties(
        physical_device,
        &memory_properties
    );

    for (std::uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
        const bool type_supported =
            (type_filter & (1u << i)) != 0;

        const bool properties_supported =
            (memory_properties.memoryTypes[i].propertyFlags & properties)
            == properties;

        if (type_supported && properties_supported) {
            return i;
        }
    }

    throw std::runtime_error(
        "VulkanDepthBuffer: failed to find suitable memory type"
    );
}

VkFormat VulkanDepthBuffer::find_supported_format(
    VkPhysicalDevice physical_device,
    const VkFormat* candidates,
    std::uint32_t candidate_count,
    VkImageTiling tiling,
    VkFormatFeatureFlags features
) {
    for (std::uint32_t i = 0; i < candidate_count; ++i) {
        const VkFormat format = candidates[i];

        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(
            physical_device,
            format,
            &properties
        );

        if (tiling == VK_IMAGE_TILING_LINEAR &&
            (properties.linearTilingFeatures & features) == features) {
            return format;
        }

        if (tiling == VK_IMAGE_TILING_OPTIMAL &&
            (properties.optimalTilingFeatures & features) == features) {
            return format;
        }
    }

    throw std::runtime_error(
        "VulkanDepthBuffer: failed to find supported depth format"
    );
}

} // namespace gs3d::render
