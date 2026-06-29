#include "render/PointCloudLodGpu.hpp"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace gs3d::render {

bool PointCloudLodGpuLevel::valid() const noexcept {
    return gpu_cloud.valid() &&
           gpu_point_count > 0;
}

bool PointCloudLodGpuLevel::empty() const noexcept {
    return gpu_point_count == 0 ||
           gpu_cloud.empty();
}

PointCloudLodGpu::PointCloudLodGpu(
    const VulkanContext& context,
    VkCommandPool command_pool,
    VkQueue transfer_queue,
    const PointCloudLodSource& lod_source
) {
    upload(
        context,
        command_pool,
        transfer_queue,
        lod_source
    );
}

void PointCloudLodGpu::upload(
    const VulkanContext& context,
    VkCommandPool command_pool,
    VkQueue transfer_queue,
    const PointCloudLodSource& lod_source
) {
    if (lod_source.levels.empty()) {
        throw std::runtime_error(
            "PointCloudLodGpu: source LOD dataset is empty"
        );
    }

    destroy();

    levels_.reserve(lod_source.levels.size());

    for (const auto& source_level : lod_source.levels) {
        if (source_level.points.empty()) {
            continue;
        }

        PointCloudLodGpuLevel gpu_level;
        gpu_level.name = source_level.name;
        gpu_level.level_index = source_level.level_index;
        gpu_level.source_point_count = source_level.source_point_count;
        gpu_level.target_point_count = source_level.target_point_count;
        gpu_level.gpu_point_count = source_level.points.point_count;
        gpu_level.voxel_size = source_level.voxel_size;
        gpu_level.voxel_mode_name = source_level.voxel_mode_name;

        gpu_level.gpu_cloud.upload(
            context,
            command_pool,
            transfer_queue,
            source_level.points
        );

        levels_.push_back(std::move(gpu_level));
    }

    if (levels_.empty()) {
        throw std::runtime_error(
            "PointCloudLodGpu: no valid LOD levels were uploaded"
        );
    }
}

void PointCloudLodGpu::destroy() noexcept {
    for (auto& level : levels_) {
        level.gpu_cloud.destroy();
        level.gpu_point_count = 0;
    }

    levels_.clear();
}

bool PointCloudLodGpu::valid() const noexcept {
    if (levels_.empty()) {
        return false;
    }

    for (const auto& level : levels_) {
        if (!level.valid()) {
            return false;
        }
    }

    return true;
}

bool PointCloudLodGpu::empty() const noexcept {
    return levels_.empty();
}

std::size_t PointCloudLodGpu::level_count() const noexcept {
    return levels_.size();
}

const PointCloudLodGpuLevel& PointCloudLodGpu::level(
    std::size_t index
) const {
    if (index >= levels_.size()) {
        throw std::out_of_range(
            "PointCloudLodGpu: level index out of range"
        );
    }

    return levels_[index];
}

PointCloudLodGpuLevel& PointCloudLodGpu::level(
    std::size_t index
) {
    if (index >= levels_.size()) {
        throw std::out_of_range(
            "PointCloudLodGpu: level index out of range"
        );
    }

    return levels_[index];
}

const PointCloudLodGpuLevel& PointCloudLodGpu::highest_detail() const {
    if (levels_.empty()) {
        throw std::runtime_error(
            "PointCloudLodGpu: no LOD levels available"
        );
    }

    return levels_.front();
}

const PointCloudLodGpuLevel& PointCloudLodGpu::lowest_detail() const {
    if (levels_.empty()) {
        throw std::runtime_error(
            "PointCloudLodGpu: no LOD levels available"
        );
    }

    return levels_.back();
}

const PointCloudGpu& PointCloudLodGpu::gpu_cloud(
    std::size_t index
) const {
    return level(index).gpu_cloud;
}

std::string PointCloudLodGpu::summary() const {
    std::ostringstream oss;

    oss << "PointCloudLodGpu\n";
    oss << "  level_count: "
        << level_count()
        << '\n';

    for (const auto& level : levels_) {
        oss << "  level "
            << level.level_index
            << " ["
            << level.name
            << "]"
            << ": points="
            << level.gpu_point_count
            << ", target="
            << level.target_point_count
            << ", source="
            << level.source_point_count
            << ", voxel_size="
            << level.voxel_size
            << ", mode="
            << level.voxel_mode_name
            << ", buffer_bytes="
            << level.gpu_cloud.vertex_buffer_size()
            << '\n';
    }

    return oss.str();
}

} // namespace gs3d::render
