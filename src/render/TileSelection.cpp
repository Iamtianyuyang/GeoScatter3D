#include "render/TileSelection.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>
#include <unordered_set>

namespace gs3d::render {

namespace {

constexpr float kPi = 3.14159265358979323846f;

struct Range1f {
    float min = 0.0f;
    float max = 0.0f;
};

[[nodiscard]]
Range1f mapped_height_range(
    const TileSelectionConfig& config,
    const gs3d::core::TileHeaderView& header,
    const gs3d::core::TileRecordView& record
) noexcept {
    const bool use_value = config.height_source == 1u;
    const float raw_min = use_value
        ? (config.use_full_z_range ? header.value_min : record.value_min)
        : (config.use_full_z_range ? header.bbox_min_z : record.bbox_min_z);
    const float raw_max = use_value
        ? (config.use_full_z_range ? header.value_max : record.value_max)
        : (config.use_full_z_range ? header.bbox_max_z : record.bbox_max_z);

    const float mapped_min = config.height_offset + raw_min * config.height_mult;
    const float mapped_max = config.height_offset + raw_max * config.height_mult;
    return {
        std::min(mapped_min, mapped_max),
        std::max(mapped_min, mapped_max)
    };
}

} // namespace

TileSelection::TileSelection(TileSelectionConfig config)
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
    const gs3d::core::TileIndexView& tile_index
) {
    struct CandidateTile {
        std::uint64_t tile_id = 0;
        float projected_pixels = 0.0f;
        float center_distance_sq = 0.0f;
        float bbox_min_x = 0.0f;
        float bbox_min_y = 0.0f;
        float bbox_min_z = 0.0f;
        float bbox_max_x = 0.0f;
        float bbox_max_y = 0.0f;
        float bbox_max_z = 0.0f;
    };

    TileSelectionResult result;
    result.camera_distance = camera.distance();

    if (tile_index.records.empty() ||
        !should_enable(camera, tile_index.header)) {
        result.enabled = false;
        result.changed = active_ || !current_tile_ids_.empty();

        current_tile_ids_.clear();
        active_ = false;

        return result;
    }

    result.enabled = true;

    /*
     * Potree/Cesium 标准方案：
     * 1. 从 VP 矩阵提取 6 个 frustum 平面（Gribb/Hartmann，Vulkan z∈[0,1]）
     * 2. 对每个 tile 的 grid-cell bbox 做 p-vertex frustum 裁剪
     * 3. 通过裁剪的 tile 再做 Potree-equivalent 投影像素大小检测
     *
     * 不做 Z 平面投影 —— 侧视角、俯视角下都正确。
     */
    const auto frustum = extract_frustum(
        camera.view_projection_matrix().m
    );

    const auto& header = tile_index.header;

    std::vector<CandidateTile> candidates;
    candidates.reserve(tile_index.records.size());

    for (const auto& record : tile_index.records) {
        // Grid cell bbox (conservative: full Z extent of dataset)
        const float cell_min_x =
            header.grid_origin_x + record.tile_x * header.tile_size_x;
        const float cell_max_x =
            header.grid_origin_x + (record.tile_x + 1) * header.tile_size_x;
        const float cell_min_y =
            header.grid_origin_y + record.tile_y * header.tile_size_y;
        const float cell_max_y =
            header.grid_origin_y + (record.tile_y + 1) * header.tile_size_y;
        const Range1f mapped_z =
            mapped_height_range(config_, header, record);

        if (aabb_outside_frustum(
                frustum,
                cell_min_x, cell_min_y, mapped_z.min,
                cell_max_x, cell_max_y, mapped_z.max)) {
            continue;
        }

        const float projected_pixels =
            tile_projected_pixels(camera, config_, header, record);

        if (projected_pixels < config_.min_tile_pixel_size) {
            continue;
        }

        const float center_x = 0.5f * (cell_min_x + cell_max_x);
        const float center_y = 0.5f * (cell_min_y + cell_max_y);
        const float center_z = 0.5f * (mapped_z.min + mapped_z.max);
        const float dx = center_x - camera.target().x;
        const float dy = center_y - camera.target().y;
        const float dz = center_z - camera.target().z;
        const float center_distance_sq = dx * dx + dy * dy + dz * dz;

        candidates.push_back({
            record.tile_id,
            projected_pixels,
            center_distance_sq,
            record.bbox_min_x,
            record.bbox_min_y,
            record.bbox_min_z,
            record.bbox_max_x,
            record.bbox_max_y,
            record.bbox_max_z
        });
    }

    result.total_candidate_tiles =
        static_cast<std::uint32_t>(candidates.size());

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const CandidateTile& a, const CandidateTile& b) {
            if (a.projected_pixels != b.projected_pixels) {
                return a.projected_pixels > b.projected_pixels;
            }

            if (a.center_distance_sq != b.center_distance_sq) {
                return a.center_distance_sq < b.center_distance_sq;
            }

            return a.tile_id < b.tile_id;
        }
    );

    // Keep the highest-priority full-resolution tiles within the explicit
    // active-set cap. The renderer leaves LOD unclipped when this trims the
    // candidate set, so non-selected areas remain covered instead of holes.
    if (config_.max_visible_tiles > 0 &&
        candidates.size() > config_.max_visible_tiles) {
        candidates.resize(config_.max_visible_tiles);
    }

    float sel_min_x = std::numeric_limits<float>::max();
    float sel_min_y = std::numeric_limits<float>::max();
    float sel_min_z = std::numeric_limits<float>::max();
    float sel_max_x = std::numeric_limits<float>::lowest();
    float sel_max_y = std::numeric_limits<float>::lowest();
    float sel_max_z = std::numeric_limits<float>::lowest();

    for (const auto& candidate : candidates) {
        result.tile_ids.push_back(candidate.tile_id);

        sel_min_x = std::min(sel_min_x, candidate.bbox_min_x);
        sel_min_y = std::min(sel_min_y, candidate.bbox_min_y);
        sel_min_z = std::min(sel_min_z, candidate.bbox_min_z);
        sel_max_x = std::max(sel_max_x, candidate.bbox_max_x);
        sel_max_y = std::max(sel_max_y, candidate.bbox_max_y);
        sel_max_z = std::max(sel_max_z, candidate.bbox_max_z);
    }

    if (!result.tile_ids.empty()) {
        result.query_bounds.min_x = sel_min_x;
        result.query_bounds.min_y = sel_min_y;
        result.query_bounds.min_z = sel_min_z;
        result.query_bounds.max_x = sel_max_x;
        result.query_bounds.max_y = sel_max_y;
        result.query_bounds.max_z = sel_max_z;
    }

    // Keep candidates' projected-pixels-descending order for upload
    // priority (no re-sort by tile_id).

    result.changed =
        !active_ ||
        !same_tile_ids(current_tile_ids_, result.tile_ids);

    if (result.changed) {
        current_tile_ids_ = result.tile_ids;
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

bool TileSelection::should_enable(
    const gs3d::camera::Camera& camera,
    const gs3d::core::TileHeaderView& header
) const noexcept {
    const float tile_size =
        std::min(header.tile_size_x, header.tile_size_y);

    if (tile_size <= 0.0f) {
        return false;
    }

    // Orthographic: uniform pixel scale across the viewport
    if (camera.projection_mode() == gs3d::camera::ProjectionMode::Orthographic) {
        const float ortho_h = camera.ortho_height();
        if (ortho_h <= 0.0f) {
            return false;
        }
        const float pixel_size =
            tile_size / ortho_h *
            static_cast<float>(camera.viewport_height());
        return pixel_size >= config_.min_tile_pixel_size;
    }

    // Perspective: Potree-equivalent screen-space criterion
    const float dist = camera.distance();
    if (dist <= 0.0f) {
        return false;
    }

    const float fov_y_rad = camera.fov_y_degrees() * kPi / 180.0f;
    const float tan_half  = std::tan(fov_y_rad * 0.5f);

    if (tan_half <= 0.0f) {
        return false;
    }

    // pixel_size = tile_size / (distance × tan(fov/2)) × viewport_height
    const float pixel_size =
        tile_size / (dist * tan_half) *
        static_cast<float>(camera.viewport_height());

    return pixel_size >= config_.min_tile_pixel_size;
}

bool TileSelection::same_tile_ids(
    const std::vector<std::uint64_t>& a,
    const std::vector<std::uint64_t>& b
) noexcept {
    if (a.size() != b.size()) {
        return false;
    }

    // Order-independent set comparison — preserves the priority-sorted
    // order in result.tile_ids for the upload queue.
    const std::unordered_set<std::uint64_t> set_a(a.begin(), a.end());
    for (const auto id : b) {
        if (set_a.find(id) == set_a.end()) {
            return false;
        }
    }
    return true;
}

TileSelection::Frustum TileSelection::extract_frustum(
    const std::array<float, 16>& m
) noexcept {
    /*
     * Gribb/Hartmann 方法（2001）：从 column-major VP 矩阵提取 6 个 frustum 平面。
     *
     * 矩阵布局：m[col*4 + row]，即
     *   行 i 的分量 = (m[i], m[4+i], m[8+i], m[12+i])
     *
     * 平面方程：对 homogeneous 世界点 X=(x,y,z,1)，
     *   plane.a*x + plane.b*y + plane.c*z + plane.d >= 0 → 在平面内侧
     *
     * Vulkan NDC z∈[0,1]：near 平面用 row2（不加 row3），与 OpenGL [-1,1] 不同。
     */
    Frustum f;
    //        a              b               c               d
    // Left  (R0+R3)
    f[0] = { m[0]+m[3],  m[4]+m[7],  m[8]+m[11],  m[12]+m[15] };
    // Right (R3-R0)
    f[1] = { m[3]-m[0],  m[7]-m[4],  m[11]-m[8],  m[15]-m[12] };
    // Bottom (R1+R3)
    f[2] = { m[1]+m[3],  m[5]+m[7],  m[9]+m[11],  m[13]+m[15] };
    // Top (R3-R1)
    f[3] = { m[3]-m[1],  m[7]-m[5],  m[11]-m[9],  m[15]-m[13] };
    // Near  (R2, Vulkan z>=0)
    f[4] = { m[2],       m[6],       m[10],        m[14]        };
    // Far   (R3-R2)
    f[5] = { m[3]-m[2],  m[7]-m[6],  m[11]-m[10], m[15]-m[14] };
    return f;
}

bool TileSelection::aabb_outside_frustum(
    const Frustum& frustum,
    float min_x, float min_y, float min_z,
    float max_x, float max_y, float max_z
) noexcept {
    /*
     * p-vertex（正顶点）方法：对每个平面，找 AABB 中距平面最近的顶点。
     * 若该顶点在平面负侧，整个 AABB 都在平面外 → 裁剪掉。
     *
     * 加了 8% 的 padding：快速缩放时 tile 不再因刚好在 frustum 边界
     * 而反复进出，消除边界闪烁。padding 按 AABB 半边长比例计算，
     * 对近处小 tile 和远处大 tile 都适配。
     */
    const float diag = std::sqrt(
        (max_x - min_x) * (max_x - min_x) +
        (max_y - min_y) * (max_y - min_y) +
        (max_z - min_z) * (max_z - min_z));
    const float pad = std::max(5.0f, diag * 0.08f);
    const float pmin_x = min_x - pad;
    const float pmin_y = min_y - pad;
    const float pmin_z = min_z - pad;
    const float pmax_x = max_x + pad;
    const float pmax_y = max_y + pad;
    const float pmax_z = max_z + pad;

    for (const auto& p : frustum) {
        const float px = (p.a >= 0.0f) ? pmax_x : pmin_x;
        const float py = (p.b >= 0.0f) ? pmax_y : pmin_y;
        const float pz = (p.c >= 0.0f) ? pmax_z : pmin_z;
        if (p.a * px + p.b * py + p.c * pz + p.d < 0.0f) {
            return true;  // fully outside this plane
        }
    }
    return false;
}

float TileSelection::tile_projected_pixels(
    const gs3d::camera::Camera& camera,
    const TileSelectionConfig& config,
    const gs3d::core::TileHeaderView& header,
    const gs3d::core::TileRecordView& record
) noexcept {
    // Grid cell center (matches how points were assigned)
    const float cx =
        header.grid_origin_x + (record.tile_x + 0.5f) * header.tile_size_x;
    const float cy =
        header.grid_origin_y + (record.tile_y + 0.5f) * header.tile_size_y;
    const Range1f mapped_z =
        mapped_height_range(config, header, record);
    const float cz = 0.5f * (mapped_z.min + mapped_z.max);

    const float dx = cx - camera.position().x;
    const float dy = cy - camera.position().y;
    const float dz = cz - camera.position().z;
    const float dist = std::sqrt(dx*dx + dy*dy + dz*dz);

    if (dist <= 0.0f) {
        return std::numeric_limits<float>::max();
    }

    // Conservative tile size: larger of the two grid dimensions
    const float tile_size = std::max(header.tile_size_x, header.tile_size_y);

    // Orthographic: uniform pixel scale across the viewport
    if (camera.projection_mode() == gs3d::camera::ProjectionMode::Orthographic) {
        const float ortho_h = camera.ortho_height();
        if (ortho_h <= 0.0f) {
            return 0.0f;
        }
        return tile_size / ortho_h *
               static_cast<float>(camera.viewport_height());
    }

    // Perspective: Potree SSE formula
    const float fov_y_rad = camera.fov_y_degrees() * kPi / 180.0f;
    const float tan_half  = std::tan(fov_y_rad * 0.5f);

    if (tan_half <= 0.0f) {
        return 0.0f;
    }

    return tile_size / (dist * tan_half) *
           static_cast<float>(camera.viewport_height());
}

} // namespace gs3d::render
