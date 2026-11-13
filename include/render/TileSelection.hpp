#pragma once

#include "camera/Camera.hpp"
#include "data/core/TileData.hpp"

#include <cstdint>
#include <vector>

namespace gs3d::render {

struct TileSelectionConfig {
    /*
     * 屏幕空间触发阈值（像素）。
     *
     * 对应 Potree 的 projected_area > threshold 判断：
     *   pixel_size = tile_size / (camera_distance × tan(fov_y/2)) × viewport_height
     *
     * 当 tile 的投影宽度 ≥ min_tile_pixel_size 时，该 tile 需要加载全精度数据。
     * 值越小 = 越早进入全精度；值越大 = 只在非常近时才加载。
     * 推荐范围：20–100。
     */
    float min_tile_pixel_size = 50.0f;
    /*
     * 渲染像素 / 逻辑像素之比（见 HeadlessRenderKernel::Config::view_scale）。
     *
     * [min_tile_pixel_size] 是「屏幕上多大才值得加载全精度」的视觉门槛，按逻辑
     * 像素标定；物理像素纹理（dpr>1）下必须先除回来，否则阈值提前触发、
     * 选取的瓦片翻几倍并撞上预算上限。
     */
    float view_scale = 1.0f;
    // Retained only for backwards-compatible configuration parsing. Visible
    // tiles are never truncated: a hard cap produces obvious block artifacts.
    std::uint32_t max_visible_tiles = 0;

    /*
     * z 方向是否使用全数据 z 范围。
     * 默认 true，适合 2.5D 地震散点数据。
     */
    bool use_full_z_range = true;

    // Match tile culling with the point shader's height mapping so camera
    // focus, exaggeration, and value-as-height all operate in one space.
    float height_offset = 0.0f;
    float height_mult = 1.0f;
    std::uint32_t height_source = 0;
};

struct TileSelectionResult {
    bool enabled = false;
    bool changed = false;

    float camera_distance = 0.0f;

    gs3d::core::Bounds3f query_bounds{};
    std::uint32_t total_candidate_tiles = 0;

    /**
     * 与视锥相交、但投影尺寸低于 `min_tile_pixel_size` 而**未**被选中的瓦片数。
     *
     * 这些区域只有底图（LOD）覆盖。底图选层必须知道这件事：瓦片层只要露出一块
     * 可见面，底图就不能退回粗层，否则那块区域会稀疏到像「模型缺一半」
     * （真机 1M / fov≈25°：瓦片只盖住近处一条带，远处那半屏只剩 6.3 万点底图）。
     */
    std::uint32_t tiles_below_threshold = 0;

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
        const gs3d::core::TileIndexView& tile_index
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
    bool should_enable(
        const gs3d::camera::Camera& camera,
        const gs3d::core::TileHeaderView& header
    ) const noexcept;

    [[nodiscard]]
    static bool same_tile_ids(
        const std::vector<std::uint64_t>& a,
        const std::vector<std::uint64_t>& b
    ) noexcept;

    /*
     * Gribb/Hartmann frustum plane extraction (Vulkan z∈[0,1]).
     * Returns 6 planes: left, right, bottom, top, near, far.
     * A world-space point X is inside if plane.dot(X,1) >= 0 for all planes.
     */
    struct FrustumPlane { float a, b, c, d; };
    using Frustum = std::array<FrustumPlane, 6>;

    [[nodiscard]]
    static Frustum extract_frustum(
        const std::array<float, 16>& vp
    ) noexcept;

    /*
     * AABB-frustum intersection via p-vertex method.
     * Returns true if the AABB is fully OUTSIDE any frustum plane (cull).
     */
    [[nodiscard]]
    static bool aabb_outside_frustum(
        const Frustum& frustum,
        float min_x, float min_y, float min_z,
        float max_x, float max_y, float max_z
    ) noexcept;

    /*
     * Projected screen width of the tile in pixels (Potree SSE equivalent).
     */
    [[nodiscard]]
    static float tile_projected_pixels(
        const gs3d::camera::Camera& camera,
        const TileSelectionConfig& config,
        const gs3d::core::TileHeaderView& header,
        const gs3d::core::TileRecordView& record
    ) noexcept;
};

} // namespace gs3d::render
