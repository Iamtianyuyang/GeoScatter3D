#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

namespace gs3d::ffi {

/*
 * 帧通道：渲染线程发布、任意线程读取的单一「最近完成帧」。
 *
 * 契约（对应 docs/spec/vulkan-core.md §4/§5）：
 * - publish 只由渲染线程调用，内部为 O(1) 缓冲置换；渲染期间不持锁，
 *   因此读取方不会被一次 20ms 的 Vulkan 提交阻塞。
 * - copy 不触发任何 Vulkan 调用、不等待渲染；容量不足返回 -1 且不写越界，
 *   无可用帧返回 -2；两种情况都会回填实际宽高，供调用方扩容重试。
 * - 缓冲复用：publish 会把调用方的 vector 换成上一帧的前置缓冲，
 *   调用方下一帧继续往同一块内存渲染，稳态下零额外分配。
 */
class FrameChannel {
public:
    struct Frame {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t stride = 0;
        std::uint64_t sequence = 0;
    };

    /// 发布一帧；buffer 与当前前置缓冲置换（调用方可复用其容量）。
    void publish(
        std::vector<std::uint8_t>& buffer,
        std::uint32_t width,
        std::uint32_t height,
        std::uint32_t stride
    );

    /// 拷贝最近一帧。0 成功、-1 容量不足、-2 尚无帧。
    int copy(
        std::uint8_t* out,
        std::uint32_t max_bytes,
        std::uint32_t* out_w,
        std::uint32_t* out_h,
        std::uint32_t* out_stride,
        std::uint64_t* out_sequence
    ) const;

    [[nodiscard]] bool ready() const;
    [[nodiscard]] std::uint64_t sequence() const;
    [[nodiscard]] Frame info() const;

private:
    mutable std::mutex mutex_;
    std::vector<std::uint8_t> frame_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint32_t stride_ = 0;
    std::uint64_t sequence_ = 0;
};

}  // namespace gs3d::ffi
