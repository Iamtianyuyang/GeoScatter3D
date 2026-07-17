#include "render/TileDeviceArena.hpp"

#include <stdexcept>

namespace gs3d::render {

namespace {

void check_vk(VkResult result, const char* message) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(message);
    }
}

VkDeviceSize align_up(VkDeviceSize value, VkDeviceSize alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

} // namespace

TileDeviceArena::Slot TileDeviceArena::allocate(
    const VulkanContext& context,
    VkDeviceSize byte_size,
    VkBufferUsageFlags usage
) {
    if (byte_size == 0) {
        throw std::runtime_error("TileDeviceArena: byte_size is zero");
    }
    if (context_ != nullptr && context_ != &context) {
        throw std::runtime_error(
            "TileDeviceArena: mixed VulkanContext usage"
        );
    }
    context_ = &context;

    if (!probed_ || probed_usage_ != usage) {
        if (probed_) {
            // 不同 usage 的对齐/内存类型可能不同；本 arena 只服务
            // 单一 usage（瓦片 vertex buffer）。
            throw std::runtime_error(
                "TileDeviceArena: usage mismatch"
            );
        }
        const auto requirements =
            VulkanBuffer::probe_memory_requirements(
                context,
                byte_size,
                usage
            );
        alignment_ = requirements.alignment == 0
            ? 1
            : requirements.alignment;
        memory_type_index_ = VulkanBuffer::memory_type_for(
            context,
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );
        probed_usage_ = usage;
        probed_ = true;
    }

    const VkDeviceSize slot_capacity = align_up(byte_size, alignment_);

    const VkDeviceSize offset =
        align_up(current_chunk_used_, alignment_);
    if (chunks_.empty() || offset + slot_capacity > current_chunk_capacity_) {
        // 增长一块；单个超大请求按自身大小独立成块。
        const VkDeviceSize chunk_capacity =
            slot_capacity > chunk_bytes_ ? slot_capacity : chunk_bytes_;
        VkMemoryAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = chunk_capacity;
        alloc_info.memoryTypeIndex = memory_type_index_;

        VkDeviceMemory chunk = VK_NULL_HANDLE;
        check_vk(
            vkAllocateMemory(
                context.device(),
                &alloc_info,
                nullptr,
                &chunk
            ),
            "TileDeviceArena: failed to allocate device chunk"
        );
        chunks_.push_back(chunk);
        current_chunk_capacity_ = chunk_capacity;
        current_chunk_used_ = 0;
        allocated_bytes_ += chunk_capacity;
    }

    Slot slot;
    slot.memory = chunks_.back();
    slot.offset = align_up(current_chunk_used_, alignment_);
    slot.capacity = slot_capacity;
    current_chunk_used_ = slot.offset + slot_capacity;
    return slot;
}

void TileDeviceArena::destroy() noexcept {
    if (context_ == nullptr) {
        return;
    }
    for (VkDeviceMemory chunk : chunks_) {
        vkFreeMemory(context_->device(), chunk, nullptr);
    }
    chunks_.clear();
    current_chunk_capacity_ = 0;
    current_chunk_used_ = 0;
    allocated_bytes_ = 0;
    probed_ = false;
    alignment_ = 1;
    memory_type_index_ = 0;
    probed_usage_ = 0;
    context_ = nullptr;
}

} // namespace gs3d::render
