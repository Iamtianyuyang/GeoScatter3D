#pragma once

#include "app/TilePointCache.hpp"
#include "app/ViewerAppInternal.hpp"
#include "core/PointData.hpp"
#include "data/Gs3dTileReader.hpp"
#include "util/Stopwatch.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <future>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gs3d::core { struct TileIndexView; }
namespace gs3d::render { class PointCloudTileGpu; }
namespace gs3d::render { class TileSelection; }
namespace gs3d::render { struct TileSelectionResult; }
namespace gs3d::render { class ViewportManager; }
namespace gs3d::render { class VulkanContext; }
namespace gs3d::render { class VulkanRenderer; }

namespace gs3d::app {

/*
 * 异步磁盘读取（Potree/Cesium 模式）：
 * 后台线程读取 tile 数据，主线程每帧非阻塞检查 future 是否完成。
 * GPU upload 仍在主线程，调用前用 in-flight fence 代替 vkDeviceWaitIdle。
 */
struct TileLoadResult {
    std::vector<std::uint64_t>          tile_ids;
    std::vector<std::pair<
        std::uint64_t,
        SharedTilePoints
    >>                                  tiles;
    gs3d::data::Gs3dTileQueryBox        actual_bbox;
    double                              read_seconds = 0.0;
    std::size_t                         cache_hit_tiles = 0;
    std::size_t                         cache_miss_tiles = 0;
    std::size_t                         candidate_tiles = 0;
};

/*
 * 瓦片流式全部可变状态：每视口可见瓦片集与 clip 包围盒、CPU 缓存、
 * 异步读取 future、全量预加载进度。生命周期与 run() 相同；必须在
 * tile_reader / tile_point_ids_by_tile 之后声明——后台 future 引用
 * 它们，析构时按声明逆序先 join future 再销毁被引用对象。
 */
struct ViewerAppTileStreamState {
    explicit ViewerAppTileStreamState(std::uint64_t cpu_cache_max_bytes)
        : point_cache(cpu_cache_max_bytes)
    {
    }

    /*
     * Tracks the query box of the tile buffer currently on the GPU.
     * Used to clip LOD draws so LOD points don't overlap full-res tiles.
     * Reset when the tile buffer is cleared.
     */
    std::array<
        std::vector<std::uint64_t>,
        kMaxViewportCount
    > viewport_tile_ids{};
    std::array<
        std::optional<gs3d::data::Gs3dTileQueryBox>,
        kMaxViewportCount
    > viewport_tile_query_boxes{};

    TilePointCache point_cache;
    std::future<TileLoadResult> load_future;
    // IDs dispatched to background thread (may differ from current selection)
    std::vector<std::uint64_t> loading_ids;
    std::vector<std::uint64_t> debounced_tile_ids;
    std::chrono::steady_clock::time_point selection_changed_at =
        std::chrono::steady_clock::now();
    gs3d::util::Stopwatch async_cycle_timer;

    /*
     * Stage 3 bounded GPU working set: first K candidates from sorted
     * tile_result.tile_ids, where K = tile_gpu_cache_max_tiles (0 = all).
     * Empty in Stage 1 (preload) and Stage 2 (fast path).
     * Rebuilt when the tile selection changes or the budget limit changes.
     */
    std::vector<std::uint64_t> gpu_required_tile_ids;
    std::uint32_t last_working_set_limit = 0;

    /*
     * 全量预加载状态(tile_preload_all):后台一次性读取全部瓦片,主循环
     * 用大预算渐进上传到 GPU 常驻;全部驻留后 tiles_fully_resident=true,
     * 之后走"每帧选择可见子集、零加载"的快路径,不再碰流式状态机。
     * 仅在所有瓦片总字节 <= tile_preload_max_bytes 时启用,否则保持 false
     * 走原有按需流式。
     */
    bool preload_enabled = false;
    std::future<std::vector<std::pair<
        std::uint64_t, SharedTilePoints>>> preload_future;
    std::vector<std::pair<std::uint64_t, SharedTilePoints>>
        preload_tiles;
    bool preload_dispatched = false;
    bool preload_failed = false;
    bool tiles_fully_resident = false;
    int preload_stall_frames = 0;
    gs3d::util::Stopwatch preload_timer;
};

/*
 * Per-frame inputs for ViewerApp::update_tile_streaming(). References
 * point at run() locals; the context itself is rebuilt every frame.
 */
struct ViewerAppTileStreamFrameContext {
    gs3d::render::VulkanContext& context;
    gs3d::render::VulkanRenderer& renderer;
    gs3d::render::ViewportManager& viewport_manager;
    // nullptr when tile mode is disabled (every stage no-ops).
    const gs3d::data::Gs3dTileReader* tile_reader = nullptr;
    const std::unordered_map<
        std::uint64_t,
        std::vector<std::uint32_t>
    >& tile_point_ids_by_tile;
    gs3d::render::PointCloudTileGpu* tile_gpu_cloud = nullptr;
    gs3d::render::TileSelection& tile_selection;
    gs3d::render::TileSelectionResult& tile_result;
    const gs3d::core::TileIndexView& tile_index_view;
    std::vector<gs3d::data::Gs3dPoint>& runtime_points_by_id;
    std::vector<std::uint8_t>& runtime_points_valid_by_id;
    int streaming_viewport_index = 0;
    bool camera_changed = false;
    bool interacting = false;
    std::chrono::steady_clock::time_point current_time{};
    bool& tile_selection_dirty;

    // Benchmark stage accumulators (no-ops when benchmark_mode is off).
    gs3d::util::Stopwatch& lod_tile_timer;
    double& lod_tile_select_ms_frame;
    double& cpu_cull_ms_frame;
    double& upload_record_ms_frame;
    std::vector<double>& reload_seconds;
};

/*
 * 悬停 pick 解析用：当前视口可见瓦片的点数据视图（CPU 缓存优先，
 * 全量预加载完成后回退到常驻预加载集）。
 */
[[nodiscard]]
std::vector<gs3d::core::PointDataView> collect_visible_hover_tile_views(
    const ViewerAppTileStreamState& tiles,
    std::size_t view_index
);

/*
 * Build the Stage-3 bounded GPU working set from sorted candidates.
 *
 *  sorted_candidates — tile IDs in priority order (projected_pixels DESC,
 *                       center_distance_sq ASC, tile_id ASC).
 *  working_set_limit — K = tile_gpu_cache_max_tiles (0 = all candidates).
 *
 * Returns the first K candidates (or all when K == 0).
 */
[[nodiscard]]
inline std::vector<std::uint64_t> build_gpu_required_tile_ids(
    const std::vector<std::uint64_t>& sorted_candidates,
    std::uint32_t working_set_limit
) {
    if (sorted_candidates.empty()) {
        return {};
    }

    const auto K =
        (working_set_limit == 0)
            ? sorted_candidates.size()
            : std::min(
                  static_cast<std::size_t>(working_set_limit),
                  sorted_candidates.size());
    return {
        sorted_candidates.begin(),
        sorted_candidates.begin() + K
    };
}

} // namespace gs3d::app
