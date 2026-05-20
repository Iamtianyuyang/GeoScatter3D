#include "render/PointCloudGpu.hpp"

#include <limits>
#include <stdexcept>

namespace gs3d::render {

namespace {

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
    upload(
        context,
        command_pool,
        transfer_queue,
        dataset
    );
}

PointCloudGpu::PointCloudGpu(
    const VulkanContext& context,
    VkCommandPool command_pool,
    VkQueue transfer_queue,
    const gs3d::data::Gs3dPoint* points,
    std::uint64_t point_count
) {
    upload_points(
        context,
        command_pool,
        transfer_queue,
        points,
        point_count
    );
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
    if (!points) {
        throw std::runtime_error(
            "PointCloudGpu: points pointer is null"
        );
    }

    if (point_count == 0) {
        throw std::runtime_error(
            "PointCloudGpu: point_count is zero"
        );
    }

    const VkDeviceSize point_bytes =
        point_buffer_size_bytes(point_count);

    destroy();

    VulkanBuffer staging_buffer(
        context,
        point_bytes,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    staging_buffer.upload(
        points,
        point_bytes
    );

    vertex_buffer_.create(
        context,
        point_bytes,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    VulkanBufferUtils::copy_buffer(
        context,
        command_pool,
        transfer_queue,
        staging_buffer.handle(),
        vertex_buffer_.handle(),
        point_bytes
    );

    point_count_ = point_count;
    vertex_buffer_size_ = point_bytes;
}

void PointCloudGpu::destroy() noexcept {
    vertex_buffer_.destroy();
    point_count_ = 0;
    vertex_buffer_size_ = 0;
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