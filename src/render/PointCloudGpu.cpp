#include "render/PointCloudGpu.hpp"

#include <cstring>
#include <limits>
#include <stdexcept>

namespace gs3d::render {

namespace {

void check_vk(VkResult result, const char* message) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(message);
    }
}

VkDeviceSize point_buffer_size_bytes(
    std::uint64_t point_count
) {
    if (point_count == 0) {
        throw std::runtime_error(
            "PointCloudGpu: point_count is zero"
        );
    }

    constexpr std::uint64_t point_size =
        static_cast<std::uint64_t>(sizeof(gs3d::data::Gs3dPoint));

    if (point_count >
        std::numeric_limits<std::uint64_t>::max() / point_size) {
        throw std::runtime_error(
            "PointCloudGpu: point buffer size overflow"
        );
    }

    return static_cast<VkDeviceSize>(point_count * point_size);
}

} // namespace

PointCloudGpu::PointCloudGpu(
    const VulkanContext& context,
    VkCommandPool command_pool,
    VkQueue transfer_queue,
    const gs3d::data::Gs3dDataset& dataset
) {
    upload(context, command_pool, transfer_queue, dataset);
}

PointCloudGpu::PointCloudGpu(
    const VulkanContext& context,
    VkCommandPool command_pool,
    VkQueue transfer_queue,
    const gs3d::data::Gs3dPoint* points,
    std::uint64_t point_count
) {
    upload_points(context, command_pool, transfer_queue, points, point_count);
}

PointCloudGpu::~PointCloudGpu() {
    release_staging();
}

PointCloudGpu::PointCloudGpu(PointCloudGpu&& other) noexcept
    : vertex_buffer_(std::move(other.vertex_buffer_))
    , point_count_(other.point_count_)
    , vertex_buffer_size_(other.vertex_buffer_size_)
    , vertex_buffer_capacity_(other.vertex_buffer_capacity_)
    , staging_buffer_(std::move(other.staging_buffer_))
    , staging_mapped_(other.staging_mapped_)
    , staging_context_(other.staging_context_)
{
    other.point_count_ = 0;
    other.vertex_buffer_size_ = 0;
    other.vertex_buffer_capacity_ = 0;
    other.staging_mapped_ = nullptr;
    other.staging_context_ = nullptr;
}

PointCloudGpu& PointCloudGpu::operator=(PointCloudGpu&& other) noexcept {
    if (this != &other) {
        release_staging();
        destroy();

        vertex_buffer_          = std::move(other.vertex_buffer_);
        point_count_            = other.point_count_;
        vertex_buffer_size_     = other.vertex_buffer_size_;
        vertex_buffer_capacity_ = other.vertex_buffer_capacity_;
        staging_buffer_         = std::move(other.staging_buffer_);
        staging_mapped_         = other.staging_mapped_;
        staging_context_        = other.staging_context_;

        other.point_count_ = 0;
        other.vertex_buffer_size_ = 0;
        other.vertex_buffer_capacity_ = 0;
        other.staging_mapped_ = nullptr;
        other.staging_context_ = nullptr;
    }
    return *this;
}

void PointCloudGpu::upload(
    const VulkanContext& context,
    VkCommandPool command_pool,
    VkQueue transfer_queue,
    const gs3d::data::Gs3dDataset& dataset
) {
    if (!dataset.is_consistent()) {
        throw std::runtime_error(
            "PointCloudGpu: dataset is inconsistent"
        );
    }

    if (dataset.empty()) {
        throw std::runtime_error(
            "PointCloudGpu: dataset is empty"
        );
    }

    if (!dataset.has_point_data()) {
        throw std::runtime_error(
            "PointCloudGpu: dataset has no loaded point data"
        );
    }

    upload_points(
        context,
        command_pool,
        transfer_queue,
        dataset.point_data(),
        dataset.point_count()
    );
}

void PointCloudGpu::upload_points(
    const VulkanContext& context,
    VkCommandPool command_pool,
    VkQueue transfer_queue,
    const gs3d::data::Gs3dPoint* points,
    std::uint64_t point_count
) {
    prepare_upload(context, points, point_count);

    const VkCommandBuffer command_buffer =
        VulkanBufferUtils::begin_single_time_commands(
            context,
            command_pool
        );
    record_prepared_upload(command_buffer);
    VulkanBufferUtils::end_single_time_commands(
        context,
        command_pool,
        transfer_queue,
        command_buffer
    );
}

void PointCloudGpu::prepare_upload(
    const VulkanContext& context,
    const gs3d::data::Gs3dPoint* points,
    std::uint64_t point_count
) {
    if (!points) {
        throw std::runtime_error("PointCloudGpu: points pointer is null");
    }

    if (point_count == 0) {
        throw std::runtime_error("PointCloudGpu: point_count is zero");
    }

    const VkDeviceSize point_bytes = point_buffer_size_bytes(point_count);

    /*
     * 1. 持久化 staging buffer（Vulkan Tutorial 最佳实践）：
     *    只在容量不足时重新分配；否则直接写入已 map 的指针。
     *    消除每次上传的 vkAllocateMemory + vkMapMemory 开销。
     */
    ensure_staging(context, point_bytes);
    std::memcpy(staging_mapped_, points, static_cast<std::size_t>(point_bytes));

    /*
     * 2. 顶点 buffer 复用：
     *    若新数据 ≤ 已分配容量，直接复用，不销毁/重建。
     *    消除 vkAllocateMemory（DEVICE_LOCAL）开销。
     */
    if (!vertex_buffer_.valid() || vertex_buffer_capacity_ < point_bytes) {
        vertex_buffer_.destroy();
        vertex_buffer_.create(
            context,
            point_bytes,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT |
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );
        vertex_buffer_capacity_ = point_bytes;
    }

    point_count_         = point_count;
    vertex_buffer_size_  = point_bytes;
}

void PointCloudGpu::record_prepared_upload(
    VkCommandBuffer command_buffer
) const {
    if (command_buffer == VK_NULL_HANDLE ||
        !staging_buffer_.valid() ||
        !vertex_buffer_.valid() ||
        vertex_buffer_size_ == 0) {
        throw std::runtime_error(
            "PointCloudGpu: no prepared upload to record"
        );
    }

    VkBufferCopy copy_region{};
    copy_region.size = vertex_buffer_size_;
    vkCmdCopyBuffer(
        command_buffer,
        staging_buffer_.handle(),
        vertex_buffer_.handle(),
        1,
        &copy_region
    );

    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = vertex_buffer_.handle();
    barrier.offset = 0;
    barrier.size = vertex_buffer_size_;

    vkCmdPipelineBarrier(
        command_buffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        0,
        0,
        nullptr,
        1,
        &barrier,
        0,
        nullptr
    );
}

void PointCloudGpu::ensure_staging(
    const VulkanContext& context,
    VkDeviceSize needed
) {
    if (staging_buffer_.valid() && staging_buffer_.size() >= needed) {
        return; // Already large enough, keep existing mapping
    }

    // Release existing mapping and buffer before reallocating
    release_staging();

    staging_buffer_.create(
        context,
        needed,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    check_vk(
        vkMapMemory(
            context.device(),
            staging_buffer_.memory(),
            0,
            needed,
            0,
            &staging_mapped_
        ),
        "PointCloudGpu: failed to map persistent staging buffer"
    );

    staging_context_ = &context;
}

void PointCloudGpu::release_staging() noexcept {
    if (staging_mapped_ && staging_context_ &&
        staging_buffer_.memory() != VK_NULL_HANDLE) {
        vkUnmapMemory(staging_context_->device(), staging_buffer_.memory());
        staging_mapped_ = nullptr;
    }
    staging_buffer_.destroy();
    staging_context_ = nullptr;
}

void PointCloudGpu::destroy() noexcept {
    vertex_buffer_.destroy();
    point_count_            = 0;
    vertex_buffer_size_     = 0;
    vertex_buffer_capacity_ = 0;
    // Staging buffer is intentionally NOT destroyed here —
    // it is kept alive for reuse on the next upload.
    // Call release_staging() explicitly when done with this object.
}

bool PointCloudGpu::valid() const noexcept {
    return vertex_buffer_.valid() &&
           point_count_ > 0 &&
           vertex_buffer_size_ > 0;
}

VkBuffer PointCloudGpu::vertex_buffer() const noexcept {
    return vertex_buffer_.handle();
}

VkDeviceSize PointCloudGpu::vertex_buffer_size() const noexcept {
    return vertex_buffer_size_;
}

std::uint64_t PointCloudGpu::point_count() const noexcept {
    return point_count_;
}

bool PointCloudGpu::empty() const noexcept {
    return point_count_ == 0;
}

} // namespace gs3d::render
