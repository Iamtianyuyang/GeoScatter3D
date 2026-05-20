#include "render/TileSelection.hpp"

#include <algorithm>

namespace gs3d::render {

TileSelection::TileSelection(
    TileSelectionConfig config
)
    : config_(config)
{
}

void TileSelection::set_config(
    const TileSelectionConfig& config
) noexcept {
    config_ = config;
}

void TileSelection::reset() {
    current_tile_ids_.clear();
    active_ = false;
}

TileSelectionResult TileSelection::update(
    const gs3d::camera::Camera& camera,
    const gs3d::data::Gs3dTileReader& tile_reader
) {
    TileSelectionResult result;

    result.camera_distance =
        camera.distance();

    if (!tile_reader.valid() ||
        !should_enable(result.camera_distance)) {
        result.enabled = false;
        result.changed = active_ || !current_tile_ids_.empty();

        current_tile_ids_.clear();
        active_ = false;

        return result;
    }

    result.enabled = true;
    result.query_half_size =
        select_half_size(result.camera_distance);

    result.query_box =
        make_query_box(
            camera,
            tile_reader,
            result.query_half_size,
            config_.use_full_z_range
        );

    result.tile_ids =
        tile_reader.query_tile_ids_by_bbox(
            result.query_box
        );

    std::sort(
        result.tile_ids.begin(),
        result.tile_ids.end()
    );

    result.tile_ids.erase(
        std::unique(
            result.tile_ids.begin(),
            result.tile_ids.end()
        ),
        result.tile_ids.end()
    );

    result.changed =
        !active_ ||
        !same_tile_ids(
            current_tile_ids_,
            result.tile_ids
        );

    if (result.changed) {
        current_tile_ids_ =
            result.tile_ids;
    }

    active_ = true;

    return result;
}

const std::vector<std::uint64_t>&
TileSelection::current_tile_ids() const noexcept {
    return current_tile_ids_;
}

bool TileSelection::active() const noexcept {
    return active_;
}

float TileSelection::select_half_size(
    float camera_distance
) const noexcept {
    if (camera_distance <= config_.near_distance) {
        return config_.near_half_size;
    }

    if (camera_distance <= config_.middle_distance) {
        return config_.middle_half_size;
    }

    return config_.far_half_size;
}

bool TileSelection::should_enable(
    float camera_distance
) const noexcept {
    return camera_distance <= config_.enable_distance;
}

bool TileSelection::same_tile_ids(
    const std::vector<std::uint64_t>& a,
    const std::vector<std::uint64_t>& b
) noexcept {
    if (a.size() != b.size()) {
        return false;
    }

    return std::equal(
        a.begin(),
        a.end(),
        b.begin()
    );
}

gs3d::data::Gs3dTileQueryBox TileSelection::make_query_box(
    const gs3d::camera::Camera& camera,
    const gs3d::data::Gs3dTileReader& tile_reader,
    float half_size,
    bool use_full_z_range
) {
    const auto target =
        camera.target();

    const auto& header =
        tile_reader.index_header();

    gs3d::data::Gs3dTileQueryBox box;

    box.min_x = target.x - half_size;
    box.max_x = target.x + half_size;

    box.min_y = target.y - half_size;
    box.max_y = target.y + half_size;

    if (use_full_z_range) {
        box.min_z = header.bbox_min_z;
        box.max_z = header.bbox_max_z;
    } else {
        box.min_z = target.z - half_size;
        box.max_z = target.z + half_size;
    }

    box.min_x =
        std::max(
            box.min_x,
            header.bbox_min_x
        );

    box.max_x =
        std::min(
            box.max_x,
            header.bbox_max_x
        );

    box.min_y =
        std::max(
            box.min_y,
            header.bbox_min_y
        );

    box.max_y =
        std::min(
            box.max_y,
            header.bbox_max_y
        );

    box.min_z =
        std::max(
            box.min_z,
            header.bbox_min_z
        );

    box.max_z =
        std::min(
            box.max_z,
            header.bbox_max_z
        );

    return box;
}

} // namespace gs3d::render