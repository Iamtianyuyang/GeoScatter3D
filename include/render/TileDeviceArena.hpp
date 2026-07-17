#pragma once

#include "render/VulkanBuffer.hpp"
#include "render/VulkanContext.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace gs3d::render {

/*
 * 预加载专用的显存 arena：以大块（默认 256 MiB）调用 vkAllocateMemory，
 * 瓦片 vertex buffer 通过 VulkanBuffer::create_bound 子绑定到块内偏移。
 * 实测独立分配 ~111 µs/次（驱动内核调用），3 万块瓦片 ≈ 3.4 s；arena
 * 子绑定把它压到毫秒级。
 *
 * 生命周期与线程约定：
 * - allocate 由预加载后台线程单线程调用（vkAllocateMemory 对同一
 *   device 并发合法，但本类内部记账不加锁，禁止多线程 allocate）。
 * - 只增长不回收：预加载集合全量常驻、同生共死，不做块内空洞管理。
 * - destroy/clear 前必须先销毁其上的全部 buffer（clear_cache 已按此
 *   顺序执行）；对象析构同理，须早于 VulkanContext 析构。
 */
class TileDeviceArena {
public:
    TileDeviceArena() = default;

    explicit TileDeviceArena(VkDeviceSize chunk_bytes)
        : chunk_bytes_(chunk_bytes)
    {
    }

    ~TileDeviceArena() { destroy(); }

    TileDeviceArena(const TileDeviceArena&) = delete;
    TileDeviceArena& operator=(const TileDeviceArena&) = delete;
    TileDeviceArena(TileDeviceArena&&) = delete;
    TileDeviceArena& operator=(TileDeviceArena&&) = delete;

    struct Slot {
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize offset = 0;
        // 切片容量（≥ 请求字节数，按 alignment 取整）。
        VkDeviceSize capacity = 0;
    };

    /*
     * 为 byte_size/usage 的 buffer 划出一段偏移；空间不足时增长一个
     * 新块（单块超大请求按其自身大小独立成块）。alignment 与 memory
     * type 在首次调用时 probe 并复用——同 usage 的 buffer 在同一设备
     * 上共享这两项。
     */
    [[nodiscard]]
    Slot allocate(
        const VulkanContext& context,
        VkDeviceSize byte_size,
        VkBufferUsageFlags usage
    );

    void destroy() noexcept;

    [[nodiscard]]
    std::size_t chunk_count() const noexcept { return chunks_.size(); }

    [[nodiscard]]
    VkDeviceSize allocated_bytes() const noexcept {
        return allocated_bytes_;
    }

private:
    VkDeviceSize chunk_bytes_ = 256ull * 1024ull * 1024ull;
    const VulkanContext* context_ = nullptr;
    std::vector<VkDeviceMemory> chunks_;
    VkDeviceSize current_chunk_capacity_ = 0;
    VkDeviceSize current_chunk_used_ = 0;
    VkDeviceSize allocated_bytes_ = 0;

    // 首次 probe 缓存。
    bool probed_ = false;
    VkDeviceSize alignment_ = 1;
    std::uint32_t memory_type_index_ = 0;
    VkBufferUsageFlags probed_usage_ = 0;
};

} // namespace gs3d::render
