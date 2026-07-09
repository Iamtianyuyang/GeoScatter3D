#include "ui/SvgLogoTexture.hpp"

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace gs3d::ui {
namespace {

void check_vk(VkResult result, const char* message) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(message);
    }
}

std::uint32_t find_memory_type(
    VkPhysicalDevice physical_device,
    std::uint32_t type_filter,
    VkMemoryPropertyFlags properties
) {
    VkPhysicalDeviceMemoryProperties mem_props{};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);
    for (std::uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if ((type_filter & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error(
        "SvgLogoTexture: failed to find suitable memory type"
    );
}

VkCommandBuffer begin_single_time_commands(
    VkDevice device,
    VkCommandPool command_pool
) {
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandPool = command_pool;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    check_vk(
        vkAllocateCommandBuffers(device, &alloc_info, &cmd),
        "SvgLogoTexture: failed to allocate command buffer"
    );

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check_vk(
        vkBeginCommandBuffer(cmd, &begin_info),
        "SvgLogoTexture: failed to begin command buffer"
    );
    return cmd;
}

void end_single_time_commands(
    VkDevice device,
    VkCommandPool command_pool,
    VkQueue queue,
    VkCommandBuffer cmd
) {
    check_vk(
        vkEndCommandBuffer(cmd),
        "SvgLogoTexture: failed to end command buffer"
    );

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;

    check_vk(
        vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE),
        "SvgLogoTexture: failed to submit"
    );
    check_vk(
        vkQueueWaitIdle(queue),
        "SvgLogoTexture: queue wait idle failed"
    );

    vkFreeCommandBuffers(device, command_pool, 1, &cmd);
}

} // anonymous namespace

SvgLogoTexture::SvgLogoTexture(
    VkDevice device,
    VkPhysicalDevice physical_device,
    VkQueue graphics_queue,
    VkCommandPool command_pool,
    const std::string& svg_path,
    std::uint32_t size
) : device_(device),
    physical_device_(physical_device),
    graphics_queue_(graphics_queue),
    command_pool_(command_pool)
{
    // 1. Load and rasterize SVG
    NSVGimage* svg = nsvgParseFromFile(svg_path.c_str(), "px", 96.0f);
    if (!svg) {
        throw std::runtime_error(
            "SvgLogoTexture: failed to load SVG: " + svg_path
        );
    }

    const float max_dim = static_cast<float>(
        (std::max)(svg->width, svg->height)
    );
    const float scale = static_cast<float>(size) / max_dim;
    const int w = (std::max)(1, static_cast<int>(svg->width * scale));
    const int h = (std::max)(1, static_cast<int>(svg->height * scale));
    const int stride = w * 4;

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) {
        nsvgDelete(svg);
        throw std::runtime_error("SvgLogoTexture: failed to create rasterizer");
    }
    std::vector<unsigned char> pixels(
        static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0
    );
    nsvgRasterize(rast, svg, 0, 0, scale, pixels.data(), w, h, stride);
    nsvgDeleteRasterizer(rast);
    nsvgDelete(svg);

    // 2. Create Vulkan image (TRANSFER_DST | SAMPLED)
    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent = {
        static_cast<std::uint32_t>(w),
        static_cast<std::uint32_t>(h),
        1
    };
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    // SRGB 格式：采样时解码到线性，写入 sRGB 交换链时再编码，
    // logo 颜色显示为素材原值（UNORM 会被双重编码提亮）。
    image_info.format = VK_FORMAT_R8G8B8A8_SRGB;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                       VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    check_vk(
        vkCreateImage(device_, &image_info, nullptr, &image_),
        "SvgLogoTexture: failed to create image"
    );

    VkMemoryRequirements mem_req{};
    vkGetImageMemoryRequirements(device_, image_, &mem_req);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_req.size;
    alloc_info.memoryTypeIndex = find_memory_type(
        physical_device_,
        mem_req.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    check_vk(
        vkAllocateMemory(device_, &alloc_info, nullptr, &image_memory_),
        "SvgLogoTexture: failed to allocate image memory"
    );
    check_vk(
        vkBindImageMemory(device_, image_, image_memory_, 0),
        "SvgLogoTexture: failed to bind image memory"
    );

    // 3. Create staging buffer and upload pixel data
    const VkDeviceSize buffer_size =
        static_cast<VkDeviceSize>(w) * h * 4;

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = buffer_size;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer staging_buffer = VK_NULL_HANDLE;
    check_vk(
        vkCreateBuffer(device_, &buffer_info, nullptr, &staging_buffer),
        "SvgLogoTexture: failed to create staging buffer"
    );

    VkMemoryRequirements buf_mem_req{};
    vkGetBufferMemoryRequirements(device_, staging_buffer, &buf_mem_req);

    VkMemoryAllocateInfo buf_alloc{};
    buf_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    buf_alloc.allocationSize = buf_mem_req.size;
    buf_alloc.memoryTypeIndex = find_memory_type(
        physical_device_,
        buf_mem_req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    VkDeviceMemory staging_memory = VK_NULL_HANDLE;
    check_vk(
        vkAllocateMemory(device_, &buf_alloc, nullptr, &staging_memory),
        "SvgLogoTexture: failed to allocate staging memory"
    );
    check_vk(
        vkBindBufferMemory(device_, staging_buffer, staging_memory, 0),
        "SvgLogoTexture: failed to bind staging memory"
    );

    void* mapped = nullptr;
    check_vk(
        vkMapMemory(device_, staging_memory, 0, buffer_size, 0, &mapped),
        "SvgLogoTexture: failed to map staging memory"
    );
    std::memcpy(mapped, pixels.data(), static_cast<std::size_t>(buffer_size));
    vkUnmapMemory(device_, staging_memory);

    // 4. Copy buffer → image via single-time command buffer
    VkCommandBuffer cmd = begin_single_time_commands(device_, command_pool_);

    // UNDEFINED → TRANSFER_DST_OPTIMAL
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {
        static_cast<std::uint32_t>(w),
        static_cast<std::uint32_t>(h),
        1
    };

    vkCmdCopyBufferToImage(
        cmd,
        staging_buffer,
        image_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );

    // TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    end_single_time_commands(device_, command_pool_, graphics_queue_, cmd);

    // 5. Clean up staging resources
    vkDestroyBuffer(device_, staging_buffer, nullptr);
    vkFreeMemory(device_, staging_memory, nullptr);

    // 6. Create image view
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image_;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8G8B8A8_SRGB;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    check_vk(
        vkCreateImageView(device_, &view_info, nullptr, &image_view_),
        "SvgLogoTexture: failed to create image view"
    );

    // 7. Register with ImGui
    descriptor_ = ImGui_ImplVulkan_AddTexture(
        image_view_,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
}

SvgLogoTexture::~SvgLogoTexture() {
    cleanup();
}

SvgLogoTexture::SvgLogoTexture(SvgLogoTexture&& other) noexcept
    : device_(other.device_),
      physical_device_(other.physical_device_),
      graphics_queue_(other.graphics_queue_),
      command_pool_(other.command_pool_),
      image_(other.image_),
      image_memory_(other.image_memory_),
      image_view_(other.image_view_),
      descriptor_(other.descriptor_)
{
    other.device_ = VK_NULL_HANDLE;
    other.physical_device_ = VK_NULL_HANDLE;
    other.graphics_queue_ = VK_NULL_HANDLE;
    other.command_pool_ = VK_NULL_HANDLE;
    other.image_ = VK_NULL_HANDLE;
    other.image_memory_ = VK_NULL_HANDLE;
    other.image_view_ = VK_NULL_HANDLE;
    other.descriptor_ = VK_NULL_HANDLE;
}

SvgLogoTexture& SvgLogoTexture::operator=(SvgLogoTexture&& other) noexcept {
    if (this != &other) {
        cleanup();
        device_ = other.device_;
        physical_device_ = other.physical_device_;
        graphics_queue_ = other.graphics_queue_;
        command_pool_ = other.command_pool_;
        image_ = other.image_;
        image_memory_ = other.image_memory_;
        image_view_ = other.image_view_;
        descriptor_ = other.descriptor_;

        other.device_ = VK_NULL_HANDLE;
        other.physical_device_ = VK_NULL_HANDLE;
        other.graphics_queue_ = VK_NULL_HANDLE;
        other.command_pool_ = VK_NULL_HANDLE;
        other.image_ = VK_NULL_HANDLE;
        other.image_memory_ = VK_NULL_HANDLE;
        other.image_view_ = VK_NULL_HANDLE;
        other.descriptor_ = VK_NULL_HANDLE;
    }
    return *this;
}

void SvgLogoTexture::cleanup() noexcept {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }

    vkDeviceWaitIdle(device_);

    if (descriptor_ != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(descriptor_);
        descriptor_ = VK_NULL_HANDLE;
    }
    if (image_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, image_view_, nullptr);
        image_view_ = VK_NULL_HANDLE;
    }
    if (image_ != VK_NULL_HANDLE) {
        vkDestroyImage(device_, image_, nullptr);
        image_ = VK_NULL_HANDLE;
    }
    if (image_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, image_memory_, nullptr);
        image_memory_ = VK_NULL_HANDLE;
    }
}

} // namespace gs3d::ui
