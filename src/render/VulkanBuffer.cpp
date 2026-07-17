#include "render/VulkanBuffer.hpp"

#include <cstring>
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

VulkanBuffer::VulkanBuffer(
    const VulkanContext& context,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties
) {
    create(context, size, usage, properties);
}

VulkanBuffer::~VulkanBuffer() {
    destroy();
}

VulkanBuffer::VulkanBuffer(VulkanBuffer&& other) noexcept {
    context_ = other.context_;
    buffer_ = other.buffer_;
    memory_ = other.memory_;
    size_ = other.size_;
    owns_memory_ = other.owns_memory_;

    other.context_ = nullptr;
    other.buffer_ = VK_NULL_HANDLE;
    other.memory_ = VK_NULL_HANDLE;
    other.size_ = 0;
    other.owns_memory_ = true;
}

VulkanBuffer& VulkanBuffer::operator=(VulkanBuffer&& other) noexcept {
    if (this != &other) {
        destroy();

        context_ = other.context_;
        buffer_ = other.buffer_;
        memory_ = other.memory_;
        size_ = other.size_;
        owns_memory_ = other.owns_memory_;

        other.context_ = nullptr;
        other.buffer_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
        other.size_ = 0;
        other.owns_memory_ = true;
    }

    return *this;
}

void VulkanBuffer::create(
    const VulkanContext& context,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties
) {
    if (size == 0) {
        throw std::runtime_error("VulkanBuffer: buffer size is zero");
    }

    destroy();

    context_ = &context;
    size_ = size;

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    check_vk(
        vkCreateBuffer(
            context.device(),
            &buffer_info,
            nullptr,
            &buffer_
        ),
        "VulkanBuffer: failed to create buffer"
    );

    VkMemoryRequirements memory_requirements{};
    vkGetBufferMemoryRequirements(
        context.device(),
        buffer_,
        &memory_requirements
    );

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = memory_requirements.size;
    alloc_info.memoryTypeIndex = find_memory_type(
        context.physical_device(),
        memory_requirements.memoryTypeBits,
        properties
    );

    check_vk(
        vkAllocateMemory(
            context.device(),
            &alloc_info,
            nullptr,
            &memory_
        ),
        "VulkanBuffer: failed to allocate buffer memory"
    );

    check_vk(
        vkBindBufferMemory(
            context.device(),
            buffer_,
            memory_,
            0
        ),
        "VulkanBuffer: failed to bind buffer memory"
    );
}

void VulkanBuffer::create_bound(
    const VulkanContext& context,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkDeviceMemory external_memory,
    VkDeviceSize memory_offset,
    VkDeviceSize bound_capacity
) {
    if (size == 0) {
        throw std::runtime_error("VulkanBuffer: buffer size is zero");
    }
    if (external_memory == VK_NULL_HANDLE) {
        throw std::runtime_error(
            "VulkanBuffer: external memory is null"
        );
    }

    destroy();

    context_ = &context;
    size_ = size;
    owns_memory_ = false;

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    check_vk(
        vkCreateBuffer(
            context.device(),
            &buffer_info,
            nullptr,
            &buffer_
        ),
        "VulkanBuffer: failed to create buffer"
    );

    // arena 切片按首次 probe 的 alignment 估算 req.size；此处以真实
    // requirements 兜底校验，异常实现直接失败（调用方回退独立分配）。
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(
        context.device(),
        buffer_,
        &requirements
    );
    if (requirements.size > bound_capacity ||
        memory_offset % requirements.alignment != 0) {
        vkDestroyBuffer(context.device(), buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
        context_ = nullptr;
        size_ = 0;
        owns_memory_ = true;
        throw std::runtime_error(
            "VulkanBuffer: external memory slot does not satisfy "
            "buffer requirements"
        );
    }

    memory_ = external_memory;

    check_vk(
        vkBindBufferMemory(
            context.device(),
            buffer_,
            external_memory,
            memory_offset
        ),
        "VulkanBuffer: failed to bind external buffer memory"
    );
}

void VulkanBuffer::destroy() noexcept {
    if (!context_) {
        return;
    }

    const VkDevice device = context_->device();

    if (buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
    }

    if (memory_ != VK_NULL_HANDLE) {
        if (owns_memory_) {
            vkFreeMemory(device, memory_, nullptr);
        }
        memory_ = VK_NULL_HANDLE;
    }

    context_ = nullptr;
    size_ = 0;
    owns_memory_ = true;
}

VkMemoryRequirements VulkanBuffer::probe_memory_requirements(
    const VulkanContext& context,
    VkDeviceSize size,
    VkBufferUsageFlags usage
) {
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer probe = VK_NULL_HANDLE;
    check_vk(
        vkCreateBuffer(
            context.device(),
            &buffer_info,
            nullptr,
            &probe
        ),
        "VulkanBuffer: failed to create probe buffer"
    );
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(
        context.device(),
        probe,
        &requirements
    );
    vkDestroyBuffer(context.device(), probe, nullptr);
    return requirements;
}

std::uint32_t VulkanBuffer::memory_type_for(
    const VulkanContext& context,
    std::uint32_t type_filter,
    VkMemoryPropertyFlags properties
) {
    return find_memory_type(
        context.physical_device(),
        type_filter,
        properties
    );
}

void VulkanBuffer::upload(
    const void* data,
    VkDeviceSize size,
    VkDeviceSize offset
) {
    if (!context_ || buffer_ == VK_NULL_HANDLE || memory_ == VK_NULL_HANDLE) {
        throw std::runtime_error("VulkanBuffer: upload called on invalid buffer");
    }

    if (!data) {
        throw std::runtime_error("VulkanBuffer: upload data is null");
    }

    if (offset + size > size_) {
        throw std::runtime_error("VulkanBuffer: upload range exceeds buffer size");
    }

    void* mapped = nullptr;

    check_vk(
        vkMapMemory(
            context_->device(),
            memory_,
            offset,
            size,
            0,
            &mapped
        ),
        "VulkanBuffer: failed to map buffer memory"
    );

    std::memcpy(mapped, data, static_cast<std::size_t>(size));

    vkUnmapMemory(context_->device(), memory_);
}

VkBuffer VulkanBuffer::handle() const noexcept {
    return buffer_;
}

VkDeviceMemory VulkanBuffer::memory() const noexcept {
    return memory_;
}

VkDeviceSize VulkanBuffer::size() const noexcept {
    return size_;
}

bool VulkanBuffer::valid() const noexcept {
    return context_ != nullptr &&
           buffer_ != VK_NULL_HANDLE &&
           memory_ != VK_NULL_HANDLE &&
           size_ > 0;
}

std::uint32_t VulkanBuffer::find_memory_type(
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

        const bool property_supported =
            (memory_properties.memoryTypes[i].propertyFlags & properties)
            == properties;

        if (type_supported && property_supported) {
            return i;
        }
    }

    throw std::runtime_error("VulkanBuffer: failed to find suitable memory type");
}

void VulkanBufferUtils::copy_buffer(
    const VulkanContext& context,
    VkCommandPool command_pool,
    VkQueue queue,
    VkBuffer src_buffer,
    VkBuffer dst_buffer,
    VkDeviceSize size
) {
    if (size == 0) {
        throw std::runtime_error("VulkanBufferUtils: copy size is zero");
    }

    VkCommandBuffer command_buffer =
        begin_single_time_commands(context, command_pool);

    VkBufferCopy copy_region{};
    copy_region.srcOffset = 0;
    copy_region.dstOffset = 0;
    copy_region.size = size;

    vkCmdCopyBuffer(
        command_buffer,
        src_buffer,
        dst_buffer,
        1,
        &copy_region
    );

    end_single_time_commands(
        context,
        command_pool,
        queue,
        command_buffer
    );
}

VkCommandBuffer VulkanBufferUtils::begin_single_time_commands(
    const VulkanContext& context,
    VkCommandPool command_pool
) {
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandPool = command_pool;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;

    check_vk(
        vkAllocateCommandBuffers(
            context.device(),
            &alloc_info,
            &command_buffer
        ),
        "VulkanBufferUtils: failed to allocate single-time command buffer"
    );

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    check_vk(
        vkBeginCommandBuffer(
            command_buffer,
            &begin_info
        ),
        "VulkanBufferUtils: failed to begin single-time command buffer"
    );

    return command_buffer;
}

void VulkanBufferUtils::end_single_time_commands(
    const VulkanContext& context,
    VkCommandPool command_pool,
    VkQueue queue,
    VkCommandBuffer command_buffer
) {
    check_vk(
        vkEndCommandBuffer(command_buffer),
        "VulkanBufferUtils: failed to end single-time command buffer"
    );

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    check_vk(
        vkQueueSubmit(
            queue,
            1,
            &submit_info,
            VK_NULL_HANDLE
        ),
        "VulkanBufferUtils: failed to submit single-time command buffer"
    );

    check_vk(
        vkQueueWaitIdle(queue),
        "VulkanBufferUtils: failed to wait for queue idle"
    );

    vkFreeCommandBuffers(
        context.device(),
        command_pool,
        1,
        &command_buffer
    );
}

} // namespace gs3d::render