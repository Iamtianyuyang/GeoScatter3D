#include "ffi/FrameChannel.hpp"

#include <cstring>
#include <utility>

namespace gs3d::ffi {

void FrameChannel::publish(
    std::vector<std::uint8_t>& buffer,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t stride
) {
    if (width == 0 || height == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    frame_.swap(buffer);
    width_ = width;
    height_ = height;
    stride_ = stride != 0 ? stride : width * 4u;
    ++sequence_;
}

int FrameChannel::copy(
    std::uint8_t* out,
    std::uint32_t max_bytes,
    std::uint32_t* out_w,
    std::uint32_t* out_h,
    std::uint32_t* out_stride,
    std::uint64_t* out_sequence
) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (width_ == 0 || height_ == 0 || frame_.empty()) {
        return -2;
    }

    const std::uint32_t stride = stride_ != 0 ? stride_ : width_ * 4u;
    const std::uint32_t pixels_stride = width_ * 4u;
    const std::uint32_t visible = height_ * pixels_stride;

    if (out_w != nullptr) *out_w = width_;
    if (out_h != nullptr) *out_h = height_;
    if (out_stride != nullptr) *out_stride = stride;
    if (out_sequence != nullptr) *out_sequence = sequence_;

    if (out == nullptr || max_bytes < visible) {
        return -1;
    }

    if (stride == pixels_stride && frame_.size() >= visible) {
        std::memcpy(out, frame_.data(), visible);
        return 0;
    }

    // 行距可能大于紧密排列（例如对齐过的 GPU 回读行距）：逐行拷贝，
    // 目标缓冲始终按 width*4 的紧密布局输出。
    for (std::uint32_t row = 0; row < height_; ++row) {
        const std::size_t source_offset = static_cast<std::size_t>(row) * stride;
        if (source_offset + pixels_stride > frame_.size()) {
            return -3;
        }
        std::memcpy(
            out + static_cast<std::size_t>(row) * pixels_stride,
            frame_.data() + source_offset,
            pixels_stride
        );
    }
    return 0;
}

bool FrameChannel::ready() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return width_ != 0 && height_ != 0 && !frame_.empty();
}

std::uint64_t FrameChannel::sequence() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sequence_;
}

FrameChannel::Frame FrameChannel::info() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Frame frame;
    frame.width = width_;
    frame.height = height_;
    frame.stride = stride_;
    frame.sequence = sequence_;
    return frame;
}

}  // namespace gs3d::ffi
