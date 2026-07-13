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
#include <span>
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
 * 单 tile 磁盘加载结果：tile ID + 已加载的点数据（不携带缓存状态）。
 * 后台 lambda 填充，主线程通过 commit_streaming_tile_load_result 验证后
 * 提交到 point_cache。
 */
struct LoadedTilePoints {
    std::uint64_t tile_id = 0;
    SharedTilePoints points;
};

/*
 * 异步磁盘读取（Potree/Cesium 模式）：
 * 后台线程只负责磁盘 I/O，不再直接写入 point_cache。
 * 主线程每帧非阻塞检查 future，通过 commit_streaming_tile_load_result
 * 验证每个 tile 是否仍属于当前 GPU 工作集后再提交到缓存。
 */
struct TileLoadResult {
    std::vector<std::uint64_t>          tile_ids;
    // Per-tile disk-load results (populated by the background lambda).
    std::vector<LoadedTilePoints>       loaded_tiles;
    // Working-set revision captured at dispatch time.
    std::uint64_t                       request_revision = 0;
    gs3d::data::Gs3dTileQueryBox        actual_bbox;
    double                              read_seconds = 0.0;
    std::size_t                         cache_hit_tiles = 0;
    std::size_t                         cache_miss_tiles = 0;
    std::size_t                         candidate_tiles = 0;
};

/*
 * commit_streaming_tile_load_result 的返回统计。
 */
struct TileLoadCommitStats {
    std::size_t accepted = 0;
    std::size_t stale_retained = 0;
    std::size_t stale_discarded = 0;
    std::size_t already_cached = 0;
    std::size_t invalid = 0;
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
    // IDs in the currently-running background batch.
    std::vector<std::uint64_t> loading_ids;
    // Missing IDs from the current semantic cache request that have not yet
    // been dispatched. Keeping them here lets disk I/O and GPU upload overlap.
    std::vector<std::uint64_t> pending_load_ids;
    // Desired set for which hit/miss accounting has already been performed.
    // Internal read batches must not count the same misses repeatedly.
    std::vector<std::uint64_t> cache_requested_tile_ids;
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
    // Incremented every time gpu_required_tile_ids changes (members,
    // order, or K).  Captured by async load tasks; used for diagnostics.
    std::uint64_t gpu_required_revision = 0;
    // Prevent the completed-upload report (and benchmark sample) from being
    // emitted every frame after a working set becomes fully resident.
    std::optional<std::uint64_t> completed_gpu_required_revision;

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
 * Build the Stage-3 GPU working set from sorted visible candidates.
 *
 *  sorted_candidates — tile IDs in priority order (projected_pixels DESC,
 *                       center_distance_sq ASC, tile_id ASC).
 *  working_set_limit — legacy cache capacity hint. It does not truncate the
 *                      active viewport: visible tiles must all be resident.
 *
 * Returns every visible candidate in priority order.
 */
[[nodiscard]]
inline std::vector<std::uint64_t> build_gpu_required_tile_ids(
    const std::vector<std::uint64_t>& sorted_candidates,
    std::uint32_t working_set_limit
) {
    (void)working_set_limit;
    return sorted_candidates;
}

/*
 * Commit disk-loaded tile points to the CPU cache after main-thread
 * validation.  Only tiles that still belong to the current GPU working
 * set (current_required_tile_ids) and are not already cached are
 * inserted as hot entries. Stale tiles are retained at the cold end of the
 * CPU LRU only when spare capacity exists; they never evict current hot data.
 * Already-cached tiles are left untouched.
 *
 * Single-writer invariant: Stage 3 calls this exclusively from the
 * main thread.  No other path calls point_cache.put() while Stage 3
 * is active, so the find() → put() check-then-insert is safe without
 * additional synchronisation.
 */
[[nodiscard]]
TileLoadCommitStats commit_streaming_tile_load_result(
    TilePointCache& cache,
    std::span<const LoadedTilePoints> loaded_tiles,
    std::span<const std::uint64_t> current_required_tile_ids
);

} // namespace gs3d::app
