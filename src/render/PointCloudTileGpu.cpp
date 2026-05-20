#include "render/PointCloudTileGpu.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace gs3d::render {

PointCloudTileGpu::PointCloudTileGpu(
    const VulkanContext& context,
    VkCommandPool command_pool,
    VkQueue transfer_queue,
    const gs3d::data::Gs3dTileReader& reader,
    const std::vector<std::uint64_t>& tile_ids
) {
    update_from_tiles(
        context,
        command_pool,
        transfer_queue,
        reader,
        tile_ids
    );
}

void PointCloudTileGpu::update_from_tiles(
    const VulkanContext& context,
    VkCommandPool command_pool,
    VkQueue transfer_queue,
    const gs3d::data::Gs3dTileReader& reader,
    const std::vector<std::uint64_t>& tile_ids,
    const gs3d::data::Gs3dTileQueryBox* filter_box
) {
    if (!reader.valid()) {
        throw std::runtime_error(
            "PointCloudTileGpu: tile reader is invalid"
        );
    }

    if (tile_ids.empty()) {
        clear();
        return;
    }

    auto merged_points =
        read_and_merge_tiles(
            reader,
            tile_ids,
            filter_box
        );

    if (merged_points.empty()) {
        clear();
        return;
    }

    gpu_cloud_.upload_points(
        context,
        command_pool,
        transfer_queue,
        merged_points.data(),
        static_cast<std::uint64_t>(merged_points.size())
    );

    loaded_tile_ids_ = tile_ids;

    stats_.tile_count =
        static_cast<std::uint64_t>(tile_ids.size());

    stats_.point_count =
        static_cast<std::uint64_t>(merged_points.size());

    stats_.point_bytes =
        stats_.point_count *
        static_cast<std::uint64_t>(
            sizeof(gs3d::data::Gs3dPoint)
        );

    stats_.gpu_buffer_bytes =
        static_cast<std::uint64_t>(
            gpu_cloud_.vertex_buffer_size()
        );

    stats_.success = true;
}

void PointCloudTileGpu::clear() noexcept {
    gpu_cloud_.destroy();
    loaded_tile_ids_.clear();
    stats_ = {};
}

bool PointCloudTileGpu::valid() const noexcept {
    return gpu_cloud_.valid() &&
           stats_.success &&
           stats_.point_count > 0;
}

bool PointCloudTileGpu::empty() const noexcept {
    return !valid();
}

const PointCloudGpu& PointCloudTileGpu::gpu_cloud() const {
    if (!valid()) {
        throw std::runtime_error(
            "PointCloudTileGpu: gpu cloud is not valid"
        );
    }

    return gpu_cloud_;
}

PointCloudGpu& PointCloudTileGpu::gpu_cloud() {
    if (!valid()) {
        throw std::runtime_error(
            "PointCloudTileGpu: gpu cloud is not valid"
        );
    }

    return gpu_cloud_;
}

std::uint64_t PointCloudTileGpu::tile_count() const noexcept {
    return stats_.tile_count;
}

std::uint64_t PointCloudTileGpu::point_count() const noexcept {
    return stats_.point_count;
}

std::uint64_t PointCloudTileGpu::point_bytes() const noexcept {
    return stats_.point_bytes;
}

const std::vector<std::uint64_t>&
PointCloudTileGpu::loaded_tile_ids() const noexcept {
    return loaded_tile_ids_;
}

const PointCloudTileGpuStats&
PointCloudTileGpu::stats() const noexcept {
    return stats_;
}

std::vector<gs3d::data::Gs3dPoint>
PointCloudTileGpu::read_and_merge_tiles(
    const gs3d::data::Gs3dTileReader& reader,
    const std::vector<std::uint64_t>& tile_ids,
    const gs3d::data::Gs3dTileQueryBox* filter_box
) {
    const std::uint64_t total_point_count =
        estimate_total_point_count(
            reader,
            tile_ids
        );

    if (total_point_count == 0) {
        return {};
    }

    if (total_point_count >
        static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max()
        )) {
        throw std::runtime_error(
            "PointCloudTileGpu: merged point count exceeds size_t range"
        );
    }

    std::vector<gs3d::data::Gs3dPoint> merged_points;
    merged_points.reserve(
        static_cast<std::size_t>(
            total_point_count
        )
    );

    for (const auto tile_id : tile_ids) {
        auto tile_points =
            reader.read_tile_points(tile_id);

        if (!filter_box) {
            merged_points.insert(
                merged_points.end(),
                tile_points.begin(),
                tile_points.end()
            );
            continue;
        }

        for (const auto& point : tile_points) {
            if (point_inside_box(point, *filter_box)) {
                merged_points.push_back(point);
            }
        }
    }

    if (!filter_box &&
        merged_points.size() !=
            static_cast<std::size_t>(total_point_count)) {
        throw std::runtime_error(
            "PointCloudTileGpu: merged point count mismatch"
        );
    }

    return merged_points;
}

std::uint64_t PointCloudTileGpu::estimate_total_point_count(
    const gs3d::data::Gs3dTileReader& reader,
    const std::vector<std::uint64_t>& tile_ids
) {
    std::uint64_t total = 0;

    for (const auto tile_id : tile_ids) {
        const auto& record =
            reader.record(tile_id);

        if (total >
            std::numeric_limits<std::uint64_t>::max() -
            record.point_count) {
            throw std::runtime_error(
                "PointCloudTileGpu: total point count overflow"
            );
        }

        total += record.point_count;
    }

    return total;
}

bool PointCloudTileGpu::point_inside_box(
    const gs3d::data::Gs3dPoint& point,
    const gs3d::data::Gs3dTileQueryBox& box
) noexcept {
    return point.x >= box.min_x &&
           point.x <= box.max_x &&
           point.y >= box.min_y &&
           point.y <= box.max_y &&
           point.z >= box.min_z &&
           point.z <= box.max_z;
}

std::vector<gs3d::data::Gs3dPoint>
PointCloudTileGpu::read_tiles(
    const gs3d::data::Gs3dTileReader& reader,
    const std::vector<std::uint64_t>& tile_ids
) {
    // Delegate to existing implementation (no filter box = all points).
    return read_and_merge_tiles(reader, tile_ids, nullptr);
}

void PointCloudTileGpu::upload_from_points(
    const VulkanContext& context,
    VkCommandPool command_pool,
    VkQueue transfer_queue,
    std::vector<gs3d::data::Gs3dPoint> points,
    const std::vector<std::uint64_t>& tile_ids
) {
    if (points.empty()) {
        clear();
        return;
    }

    gpu_cloud_.upload_points(
        context,
        command_pool,
        transfer_queue,
        points.data(),
        static_cast<std::uint64_t>(points.size())
    );

    loaded_tile_ids_ = tile_ids;

    stats_.tile_count  = static_cast<std::uint64_t>(tile_ids.size());
    stats_.point_count = static_cast<std::uint64_t>(points.size());
    stats_.point_bytes = stats_.point_count *
        static_cast<std::uint64_t>(sizeof(gs3d::data::Gs3dPoint));
    stats_.gpu_buffer_bytes =
        static_cast<std::uint64_t>(gpu_cloud_.vertex_buffer_size());
    stats_.success = true;
}

} // namespace gs3d::render
