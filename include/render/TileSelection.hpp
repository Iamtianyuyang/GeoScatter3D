#pragma once

#include "camera/Camera.hpp"
#include "data/Gs3dTileReader.hpp"

#include <cstdint>
#include <vector>

namespace gs3d::render {

struct TileSelectionConfig {
    /*
     * 距离大于该值时，不加载 full-resolution tile，只显示 LOD。
     */
    float enable_distance = 12000.0f;

    /*
     * 不同距离下的局部查询半径。
     *
     * distance <= near_distance:
     *     使用 near_half_size
     *
     * distance <= middle_distance:
     *     使用 middle_half_size
     *
     * distance <= enable_distance:
     *     使用 far_half_size
     */
    float near_distance = 3000.0f;
    float middle_distance = 6000.0f;

    float near_half_size = 256.0f;
    float middle_half_size = 512.0f;
    float far_half_size = 1024.0f;

    /*
     * z 方向是否限制。
     *
     * 第一版默认不限制 z，使用全数据 z 范围。
     * 对 2.5D 地震散点数据更稳。
     */
    bool use_full_z_range = true;
};

struct TileSelectionResult {
    bool enabled = false;
    bool changed = false;

    float camera_distance = 0.0f;
    float query_half_size = 0.0f;

    gs3d::data::Gs3dTileQueryBox query_box{};

    std::vector<std::uint64_t> tile_ids;
};

class TileSelection {
public:
    TileSelection() = default;

    explicit TileSelection(TileSelectionConfig config);

    void set_config(const TileSelectionConfig& config) noexcept;

    void reset();

    [[nodiscard]]
    TileSelectionResult update(
        const gs3d::camera::Camera& camera,
        const gs3d::data::Gs3dTileReader& tile_reader
    );

    [[nodiscard]]
    const std::vector<std::uint64_t>& current_tile_ids() const noexcept;

    [[nodiscard]]
    bool active() const noexcept;

private:
    TileSelectionConfig config_{};

    std::vector<std::uint64_t> current_tile_ids_{};
    bool active_ = false;

private:
    [[nodiscard]]
    float select_half_size(float camera_distance) const noexcept;

    [[nodiscard]]
    bool should_enable(float camera_distance) const noexcept;

    [[nodiscard]]
    static bool same_tile_ids(
        const std::vector<std::uint64_t>& a,
        const std::vector<std::uint64_t>& b
    ) noexcept;

    [[nodiscard]]
    static gs3d::data::Gs3dTileQueryBox make_query_box(
        const gs3d::camera::Camera& camera,
        const gs3d::data::Gs3dTileReader& tile_reader,
        float half_size,
        bool use_full_z_range
    );
};

} // namespace gs3d::render