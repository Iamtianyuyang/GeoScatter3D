#include "render/PointCloudGpu.hpp"

#include <stdexcept>

namespace gs3d::render {

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

    const VkDeviceSize point_bytes =
        static_cast<VkDeviceSize>(dataset.point_bytes());

    if (point_bytes == 0) {
        throw std::runtime_error(
            "PointCloudGpu: point byte size is zero"
        );
    }

    destroy();

    VulkanBuffer staging_buffer(
        context,
        point_bytes,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    staging_buffer.upload(
        dataset.point_data(),
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

    point_count_ = dataset.point_count();
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