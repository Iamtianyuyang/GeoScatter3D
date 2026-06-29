#include "render/GpuFrameTimer.hpp"

#include <array>

namespace gs3d::render {

GpuFrameTimer::GpuFrameTimer(
    const VulkanContext& context,
    std::uint32_t frames_in_flight
)
    : context_(context)
    , frames_in_flight_(frames_in_flight)
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(context_.physical_device(), &properties);
    timestamp_period_ns_ = properties.limits.timestampPeriod;

    if (timestamp_period_ns_ <= 0.0 || frames_in_flight_ == 0) {
        return;
    }

    VkQueryPoolCreateInfo query_pool_info{};
    query_pool_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    query_pool_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    query_pool_info.queryCount = frames_in_flight_ * 2;

    if (vkCreateQueryPool(
            context_.device(),
            &query_pool_info,
            nullptr,
            &query_pool_
        ) != VK_SUCCESS) {
        query_pool_ = VK_NULL_HANDLE;
        timestamp_period_ns_ = 0.0;
    }
}

GpuFrameTimer::~GpuFrameTimer() {
    if (query_pool_ != VK_NULL_HANDLE) {
        vkDestroyQueryPool(context_.device(), query_pool_, nullptr);
        query_pool_ = VK_NULL_HANDLE;
    }
}

void GpuFrameTimer::begin_frame(
    std::uint32_t frame_index,
    VkCommandBuffer command_buffer
) {
    if (!supported()) {
        return;
    }

    const std::uint32_t query_base = frame_index * 2;
    vkCmdResetQueryPool(command_buffer, query_pool_, query_base, 2);
    vkCmdWriteTimestamp(
        command_buffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        query_pool_,
        query_base
    );
}

void GpuFrameTimer::end_frame(
    std::uint32_t frame_index,
    VkCommandBuffer command_buffer
) {
    if (!supported()) {
        return;
    }

    vkCmdWriteTimestamp(
        command_buffer,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        query_pool_,
        frame_index * 2 + 1
    );
}

void GpuFrameTimer::collect_frame(std::uint32_t frame_index) noexcept {
    if (!supported()) {
        return;
    }

    std::array<std::uint64_t, 2> timestamps{};
    const VkResult result = vkGetQueryPoolResults(
        context_.device(),
        query_pool_,
        frame_index * 2,
        2,
        sizeof(timestamps),
        timestamps.data(),
        sizeof(std::uint64_t),
        VK_QUERY_RESULT_64_BIT
    );

    if (result != VK_SUCCESS || timestamps[1] < timestamps[0]) {
        return;
    }

    const double elapsed_ticks =
        static_cast<double>(timestamps[1] - timestamps[0]);
    last_frame_time_ms_ =
        elapsed_ticks * timestamp_period_ns_ / 1'000'000.0;
    has_last_frame_time_ = true;
}

bool GpuFrameTimer::supported() const noexcept {
    return query_pool_ != VK_NULL_HANDLE &&
           timestamp_period_ns_ > 0.0;
}

bool GpuFrameTimer::has_last_frame_time() const noexcept {
    return has_last_frame_time_;
}

double GpuFrameTimer::last_frame_time_ms() const noexcept {
    return last_frame_time_ms_;
}

} // namespace gs3d::render
