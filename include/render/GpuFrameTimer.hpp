#pragma once

#include "render/VulkanContext.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace gs3d::render {

class GpuFrameTimer {
public:
    GpuFrameTimer(
        const VulkanContext& context,
        std::uint32_t frames_in_flight
    );

    ~GpuFrameTimer();

    GpuFrameTimer(const GpuFrameTimer&) = delete;
    GpuFrameTimer& operator=(const GpuFrameTimer&) = delete;

    GpuFrameTimer(GpuFrameTimer&&) = delete;
    GpuFrameTimer& operator=(GpuFrameTimer&&) = delete;

    void begin_frame(
        std::uint32_t frame_index,
        VkCommandBuffer command_buffer
    );

    void end_frame(
        std::uint32_t frame_index,
        VkCommandBuffer command_buffer
    );

    void collect_frame(std::uint32_t frame_index) noexcept;

    [[nodiscard]]
    bool supported() const noexcept;

    [[nodiscard]]
    bool has_last_frame_time() const noexcept;

    [[nodiscard]]
    double last_frame_time_ms() const noexcept;

private:
    const VulkanContext& context_;
    VkQueryPool query_pool_ = VK_NULL_HANDLE;
    std::uint32_t frames_in_flight_ = 0;
    double timestamp_period_ns_ = 0.0;
    double last_frame_time_ms_ = 0.0;
    bool has_last_frame_time_ = false;
};

} // namespace gs3d::render
