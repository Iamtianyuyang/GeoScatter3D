#include "app/ViewerApp.hpp"
#include "util/Log.hpp"
#include "app/ViewerAppTileStreaming.hpp"

#include "app/ViewerAppInternal.hpp"
#include "data/PointDataAdapters.hpp"
#include "render/PointCloudTileGpu.hpp"
#include "render/TileSelection.hpp"
#include "render/ViewportManager.hpp"
#include "render/VulkanContext.hpp"
#include "render/VulkanRenderer.hpp"
#include "util/Stopwatch.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

namespace gs3d::app {

namespace {

constexpr double kTileSelectionDebounceSeconds = 0.075;
constexpr std::uint64_t kTileReadBatchMaxBytes =
    64ull * 1024ull * 1024ull;

[[nodiscard]]
std::vector<std::uint64_t> take_tile_read_batch(
    const gs3d::data::Gs3dTileReader& reader,
    std::vector<std::uint64_t>& pending_ids
) {
    std::vector<std::uint64_t> batch;
    std::uint64_t batch_bytes = 0;
    std::size_t count = 0;

    while (count < pending_ids.size()) {
        const auto tile_id = pending_ids[count];
        const auto tile_bytes =
            reader.record(tile_id).point_data_bytes;
        if (!batch.empty() &&
            (batch_bytes >= kTileReadBatchMaxBytes ||
             tile_bytes > kTileReadBatchMaxBytes - batch_bytes)) {
            break;
        }

        batch.push_back(tile_id);
        ++count;
        if (tile_bytes >= kTileReadBatchMaxBytes - batch_bytes) {
            batch_bytes = kTileReadBatchMaxBytes;
        } else {
            batch_bytes += tile_bytes;
        }
    }

    pending_ids.erase(
        pending_ids.begin(),
        pending_ids.begin() + static_cast<std::ptrdiff_t>(count));
    return batch;
}

[[nodiscard]]
std::shared_ptr<TilePoints> load_tile_points_with_ids(
    const gs3d::data::Gs3dTileReader& tile_reader,
    const std::unordered_map<
        std::uint64_t,
        std::vector<std::uint32_t>
    >& tile_point_ids_by_tile,
    std::uint64_t tile_id
) {
    auto loaded = std::make_shared<TilePoints>();

    if (tile_reader.has_embedded_point_ids()) {
        /*
         * v2 tile: points and point_ids are embedded
         * (20-byte interleaved Gs3dPointWithId on disk).
         * read_tile_points_with_ids splits them into
         * 16-byte Gs3dPoint + uint32_t vectors.
         */
        auto block =
            tile_reader.read_tile_points_with_ids(tile_id);
        loaded->points = std::move(block.points);
        loaded->point_ids = std::move(block.point_ids);
    } else {
        /*
         * v1 tile: points-only on disk (16-byte Gs3dPoint).
         * Point IDs come from the runtime-built
         * tile_point_ids_by_tile map.
         */
        loaded->points =
            tile_reader.read_tile_points(tile_id);
        const auto found =
            tile_point_ids_by_tile.find(tile_id);
        if (found == tile_point_ids_by_tile.end()) {
            throw std::runtime_error(
                "ViewerApp: missing runtime tile point ids"
            );
        }
        loaded->point_ids = found->second;
    }

    if (loaded->points.size() != loaded->point_ids.size()) {
        throw std::runtime_error(
            "ViewerApp: tile point/id size mismatch"
        );
    }
    return loaded;
}

[[nodiscard]]
std::vector<std::pair<std::uint64_t, gs3d::core::PointDataView>>
make_cached_tile_views(
    const std::vector<std::pair<std::uint64_t, SharedTilePoints>>& tiles
) {
    std::vector<std::pair<
        std::uint64_t,
        gs3d::core::PointDataView
    >> views;
    views.reserve(tiles.size());
    for (const auto& [tile_id, points] : tiles) {
        if (!points) {
            continue;
        }
        views.emplace_back(
            tile_id,
            gs3d::data::make_point_data_view(
                points->points,
                points->point_ids.data()
            )
        );
    }
    return views;
}

// 从瓦片 id 列表算并集包围盒(供 clip 用)。
[[nodiscard]]
gs3d::data::Gs3dTileQueryBox compute_tiles_bbox(
    const gs3d::data::Gs3dTileReader& tile_reader,
    const std::vector<std::uint64_t>& ids
) {
    gs3d::data::Gs3dTileQueryBox box;
    box.min_x = box.min_y = box.min_z =
        std::numeric_limits<float>::max();
    box.max_x = box.max_y = box.max_z =
        -std::numeric_limits<float>::max();
    for (const auto tile_id : ids) {
        const auto& r = tile_reader.record(tile_id);
        box.min_x = std::min(box.min_x, r.bbox_min_x);
        box.min_y = std::min(box.min_y, r.bbox_min_y);
        box.min_z = std::min(box.min_z, r.bbox_min_z);
        box.max_x = std::max(box.max_x, r.bbox_max_x);
        box.max_y = std::max(box.max_y, r.bbox_max_y);
        box.max_z = std::max(box.max_z, r.bbox_max_z);
    }
    return box;
}

void log_tile_upload(
    bool verbose,
    const gs3d::render::PointCloudTileGpuStats& stats,
    const gs3d::render::PointCloudTileGpuSyncResult& sync,
    double total_seconds
) {
    if (!verbose) {
        return;
    }

    gs3d::util::log::info() << "[TILE] upload complete (all desired tiles resident).\n";
    gs3d::util::log::info() << "tile_count = "
              << stats.tile_count << '\n';
    gs3d::util::log::info() << "point_count = "
              << stats.point_count << '\n';
    gs3d::util::log::info() << "gpu_buffer_bytes = "
              << stats.gpu_buffer_bytes << '\n';
    gs3d::util::log::info() << "resident_tile_count = "
              << stats.resident_tile_count << '\n';
    gs3d::util::log::info() << "uploaded_tile_count = "
              << sync.uploaded_tile_count << '\n';
    gs3d::util::log::info() << "uploaded_point_count = "
              << sync.uploaded_point_count << '\n';
    gs3d::util::log::info() << "uploaded_bytes = "
              << sync.uploaded_bytes << '\n';
    gs3d::util::log::info() << "[TIME] tile.async_total_seconds = "
              << total_seconds << '\n';
}

} // namespace


std::vector<gs3d::core::PointDataView> collect_visible_hover_tile_views(
    const ViewerAppTileStreamState& tiles,
    std::size_t view_index
) {
    std::vector<gs3d::core::PointDataView> views;
    if (view_index >= tiles.viewport_tile_ids.size()) {
        return views;
    }

    const auto& tile_ids = tiles.viewport_tile_ids[view_index];
    views.reserve(tile_ids.size());
    for (const auto tile_id : tile_ids) {
        SharedTilePoints points = tiles.point_cache.find(tile_id);
        if (!points && tiles.tiles_fully_resident) {
            const auto found = std::find_if(
                tiles.preload_tiles.begin(),
                tiles.preload_tiles.end(),
                [tile_id](
                    const std::pair<
                        std::uint64_t,
                        SharedTilePoints
                    >& entry
                ) {
                    return entry.first == tile_id;
                }
            );
            if (found != tiles.preload_tiles.end()) {
                points = found->second;
            }
        }

        if (!points || points->points.empty() ||
            points->point_ids.empty()) {
            continue;
        }

        views.push_back(
            gs3d::data::make_point_data_view(
                points->points,
                points->point_ids.data()
            )
        );
    }

    return views;
}

TileStreamingSystem::TileStreamingSystem(
    const ViewerTileConfig& config,
    bool benchmark_enabled,
    const std::optional<gs3d::data::Gs3dTileReader>& tile_reader
)
    : state_(config.cpu_cache_max_bytes)
{
    state_.preload_enabled =
        config.enabled &&
        config.preload_all &&
        !benchmark_enabled &&
        tile_reader.has_value() &&
        !tile_reader->records().empty() &&
        tile_reader->stats().total_point_bytes <=
            config.preload_max_bytes;
}

ViewerAppTileStreamState& TileStreamingSystem::state() noexcept
{
    return state_;
}

const ViewerAppTileStreamState& TileStreamingSystem::state() const noexcept
{
    return state_;
}

// gui_cmds.clear_cache_requested: cancel a still-running preload first so
// clear() isn't immediately defeated by the background thread re-inserting
// tiles, then release both CPU and GPU tile caches.
void TileStreamingSystem::clear_cache(
    gs3d::render::VulkanRenderer& renderer,
    gs3d::render::PointCloudTileGpu* tile_gpu_cloud
) {
    auto& tiles = state_;
    if (tiles.preload_enabled && !tiles.tiles_fully_resident &&
        !tiles.preload_failed) {
        tiles.preload_failed = true;
        if (tiles.preload_future.valid()) {
            tiles.preload_future.wait();
        }
        gs3d::util::log::info() << "[TILE] preload cancelled for cache clear.\n";
    }
    // A running Stage-3 read cannot be cancelled safely. Drain and discard it
    // before clearing so its late result cannot immediately refill the cache.
    if (tiles.load_future.valid()) {
        tiles.load_future.wait();
        try {
            (void)tiles.load_future.get();
        } catch (const std::exception& e) {
            gs3d::util::log::error()
                << "[TILE] pending load discarded after error: "
                << e.what() << '\n';
        }
        tiles.load_future = {};
    }
    tiles.loading_ids.clear();
    tiles.pending_load_ids.clear();
    tiles.cache_requested_tile_ids.clear();
    tiles.completed_gpu_required_revision.reset();
    tiles.point_cache.clear();
    if (tile_gpu_cloud != nullptr) {
        renderer.wait_for_in_flight_fences();
        tile_gpu_cloud->clear();
    }
    gs3d::util::log::info() << "[TILE] tile caches cleared.\n";
}

/*
 * 每帧瓦片状态机，三个阶段：
 *   1. 全量预加载:后台一次性读取全部瓦片,主循环用大预算逐帧上传到
 *      GPU 常驻。全部驻留后切到快路径,不再走流式。
 *   2. 快路径:全部瓦片已常驻 GPU。瓦片选择(纯 CPU frustum/屏幕尺寸,
 *      无 I/O)每帧都跑,包括交互期间,把可见子集直接设为绘制集——
 *      无异步读、无上传节流、无去抖等待。绘制仍只画可见子集,开销有界。
 *   3. 按需流式(预加载未启用或失败时):停手后去抖选择、异步磁盘读、
 *      逐帧预算上传、增量更新可见集。
 */
void TileStreamingSystem::update(
    const ViewerAppTileStreamFrameContext& ctx,
    const ViewerTileConfig& config,
    bool benchmark_enabled
) {
    auto& tiles = state_;
    const auto flush_lod_tile_stage = [&ctx]() {
        ctx.lod_tile_select_ms_frame +=
            ctx.lod_tile_timer.elapsed_milliseconds();
        ctx.lod_tile_timer.reset();
    };

    // ── 阶段 1:全量预加载 ─────────────────────────────────────────
    if (tiles.preload_enabled && !tiles.tiles_fully_resident &&
        !tiles.preload_failed && ctx.tile_gpu_cloud) {
        if (!tiles.preload_dispatched) {
            tiles.preload_dispatched = true;
            tiles.preload_timer.reset();
            tiles.preload_future = std::async(
                std::launch::async,
                [&reader = *ctx.tile_reader,
                 &cache = tiles.point_cache,
                 &ids_by_tile = ctx.tile_point_ids_by_tile]()
                    -> std::vector<std::pair<
                        std::uint64_t, SharedTilePoints>> {
                    std::vector<std::pair<
                        std::uint64_t, SharedTilePoints>> all;
                    all.reserve(reader.records().size());
                    for (const auto& rec : reader.records()) {
                        // Use find() (shared lock) for the check to
                        // avoid serializing against main-thread
                        // find()/stats() calls. Only put() (exclusive)
                        // on cache miss.
                        SharedTilePoints pts =
                            cache.find(rec.tile_id);
                        if (!pts) {
                            auto loaded =
                                load_tile_points_with_ids(
                                    reader,
                                    ids_by_tile,
                                    rec.tile_id
                                );
                            cache.put(rec.tile_id, loaded);
                            pts = loaded;
                        }
                        all.emplace_back(rec.tile_id, pts);
                    }
                    return all;
                });
        }

        if (tiles.preload_tiles.empty() &&
            tiles.preload_future.valid() &&
            tiles.preload_future.wait_for(std::chrono::seconds(0)) ==
                std::future_status::ready) {
            try {
                tiles.preload_tiles = tiles.preload_future.get();
                register_runtime_tile_point_lookup(
                    tiles.preload_tiles,
                    ctx.runtime_points_by_id,
                    ctx.runtime_points_valid_by_id
                );
            } catch (const std::exception& e) {
                gs3d::util::log::error()
                    << "[TILE] preload failed: " << e.what()
                    << " — falling back to streaming mode.\n";
                tiles.preload_failed = true;
            }
        }

        if (!tiles.preload_tiles.empty()) {
            ctx.renderer.wait_for_in_flight_fences();
            const auto preload_views =
                make_cached_tile_views(tiles.preload_tiles);
            const auto sync =
                ctx.tile_gpu_cloud->sync_from_cached_tiles(
                    ctx.context,
                    ctx.renderer.command_pool(),
                    ctx.context.graphics_queue(),
                    preload_views,
                    {},  // preload: no bounded working set
                    config.preload_upload_budget_bytes
                );
            if (config.verbose && sync.uploaded_bytes > 0) {
                gs3d::util::log::info()
                    << "[TILE] preload upload: bytes="
                    << sync.uploaded_bytes
                    << ", resident="
                    << sync.resident_tile_count << "/"
                    << tiles.preload_tiles.size()
                    << ", complete="
                    << (sync.complete ? "true" : "false")
                    << '\n';
            }
            if (sync.complete) {
                tiles.tiles_fully_resident = true;
                ctx.tile_selection_dirty = true;
                gs3d::util::log::info()
                    << "[TILE] preload complete: "
                    << sync.resident_tile_count
                    << " tiles resident on GPU ("
                    << sync.resident_gpu_buffer_bytes
                    << " bytes) in "
                    << tiles.preload_timer.elapsed_seconds()
                    << "s — interactive streaming disabled.\n";
            } else if (sync.uploaded_bytes == 0) {
                ++tiles.preload_stall_frames;
                if (tiles.preload_stall_frames >= 3) {
                    gs3d::util::log::error()
                        << "[TILE] preload stalled ("
                        << sync.resident_tile_count << "/"
                        << tiles.preload_tiles.size()
                        << " tiles uploaded, budget="
                        << config.preload_upload_budget_bytes
                        << " bytes/frame)"
                        << " — falling back to streaming.\n";
                    tiles.preload_failed = true;
                }
            } else {
                tiles.preload_stall_frames = 0;
            }
        }
    }

    // ── 阶段 2:快路径(全部瓦片常驻) ──────────────────────────────
    if (tiles.tiles_fully_resident) {
        if (ctx.camera_changed || ctx.tile_selection_dirty) {
            flush_lod_tile_stage();
            {
                gs3d::util::Stopwatch cpu_cull_timer;
                ctx.tile_result = ctx.tile_selection.update(
                    ctx.viewport_manager.camera(ctx.streaming_viewport_index),
                    ctx.tile_index_view
                );
                ctx.cpu_cull_ms_frame +=
                    cpu_cull_timer.elapsed_milliseconds();
            }
            ctx.lod_tile_timer.reset();
            ctx.tile_selection_dirty = false;
            const auto view_index =
                static_cast<std::size_t>(ctx.streaming_viewport_index);
            if (ctx.tile_result.enabled &&
                !ctx.tile_result.tile_ids.empty()) {
                tiles.viewport_tile_ids[view_index] =
                    ctx.tile_result.tile_ids;
                tiles.viewport_tile_query_boxes[view_index] =
                    compute_tiles_bbox(
                        *ctx.tile_reader,
                        ctx.tile_result.tile_ids
                    );
            } else {
                tiles.viewport_tile_ids[view_index].clear();
                tiles.viewport_tile_query_boxes[view_index].reset();
            }
        }
        return;
    }

    // ── 阶段 3:按需流式 ──────────────────────────────────────────
    if (!config.enabled ||
        ctx.tile_reader == nullptr ||
        !ctx.tile_gpu_cloud ||
        (tiles.preload_enabled && !tiles.preload_failed)) {
        return;
    }

    if (!ctx.interacting && ctx.tile_selection_dirty) {
        flush_lod_tile_stage();
        {
            gs3d::util::Stopwatch cpu_cull_timer;
            ctx.tile_result = ctx.tile_selection.update(
                ctx.viewport_manager.camera(ctx.streaming_viewport_index),
                ctx.tile_index_view
            );
            ctx.cpu_cull_ms_frame +=
                cpu_cull_timer.elapsed_milliseconds();
        }
        ctx.lod_tile_timer.reset();
        ctx.tile_selection_dirty = false;
        if (ctx.tile_result.changed) {
            tiles.selection_changed_at = ctx.current_time;
            /*
             * Anti-flicker (docs/benchmark/flicker-audit.md
             * Task 5/6): do NOT clear viewport_tile_ids here.
             * The previously-displayed tiles are still resident
             * on the GPU (evict_to_budget only drops tiles once
             * the *new* selection has finished uploading) and
             * stay valid to keep drawing. Clearing them the
             * instant the selection changes — before the async
             * read+upload for the new selection completes —
             * caused a visible "detail drops to LOD, then pops
             * back" cycle on every camera-settle event, lasting
             * as long as the reload (0.2-0.9s on the 33M-point
             * baseline). The swap to the new tile set happens
             * incrementally below as each tile becomes resident,
             * not atomically at upload completion.
             */
        }
    }

    /*
     * (Re)build the Stage 3 GPU working set from sorted visible candidates.
     * Every visible tile is required. tile_gpu_cache_max_tiles remains a
     * residency budget for tiles outside the active viewport.
     *
     * The working set is recomputed every frame so that priority-order
     * changes (which TileSelection::same_tile_ids ignores — it uses a
     * set-based comparison) and runtime budget edits are picked up
     * without special-case flags.
     */
    {
        const auto resident_tile_budget =
            config.gpu_cache_max_tiles;

        // Keep the GPU eviction budget synchronised with the cache limit.
        // Current visible tiles are pinned by sync_from_cached_tiles, so the
        // budget can be exceeded temporarily when the viewport needs it.
        if (resident_tile_budget != tiles.last_resident_tile_budget) {
            tiles.last_resident_tile_budget = resident_tile_budget;
            ctx.tile_gpu_cloud->set_resident_tile_budget(
                resident_tile_budget);
        }

        if (ctx.tile_result.enabled &&
            !ctx.tile_result.tile_ids.empty()) {
            auto next_required = build_gpu_required_tile_ids(
                ctx.tile_result.tile_ids,
                resident_tile_budget);

            if (next_required != tiles.gpu_required_tile_ids) {
                tiles.gpu_required_tile_ids =
                    std::move(next_required);
                ++tiles.gpu_required_revision;
                if (tiles.debounced_tile_ids !=
                    tiles.gpu_required_tile_ids) {
                    tiles.debounced_tile_ids =
                        tiles.gpu_required_tile_ids;
                    tiles.selection_changed_at =
                        ctx.current_time;
                }
            }
        } else {
            tiles.gpu_required_tile_ids.clear();
        }
    }

    if (tiles.load_future.valid() &&
        tiles.load_future.wait_for(std::chrono::seconds(0))
            == std::future_status::ready) {
        auto loaded = tiles.load_future.get();

        // Commit disk-loaded tiles to CPU cache after validating
        // each tile against the current GPU working set.
        gs3d::util::Stopwatch commit_timer;
        const auto commit_stats =
            commit_streaming_tile_load_result(
                tiles.point_cache,
                loaded.loaded_tiles,
                tiles.gpu_required_tile_ids);
        const double commit_seconds =
            commit_timer.elapsed_seconds();

        tiles.load_future = {};
        tiles.loading_ids.clear();
        // Accepted tiles are now in CPU cache; the per-frame
        // upload loop below picks them up incrementally.
        if (config.verbose) {
            gs3d::util::log::info()
                << "[TILE] async load completed: "
                << "requested=" << loaded.loaded_tiles.size()
                << ", accepted=" << commit_stats.accepted
                << ", stale_retained="
                << commit_stats.stale_retained
                << ", stale_discarded="
                << commit_stats.stale_discarded
                << ", already_cached="
                << commit_stats.already_cached
                << ", invalid=" << commit_stats.invalid
                << ", read_seconds="
                << loaded.read_seconds
                << ", commit_seconds="
                << commit_seconds
                << ", batches_remaining="
                << tiles.pending_load_ids.size()
                << ", request_revision="
                << loaded.request_revision
                << ", current_revision="
                << tiles.gpu_required_revision
                << '\n';
        }
    }

    if (!ctx.tile_selection_dirty && !ctx.tile_result.enabled) {
        tiles.debounced_tile_ids.clear();
        tiles.gpu_required_tile_ids.clear();
        tiles.pending_load_ids.clear();
        tiles.cache_requested_tile_ids.clear();
        tiles.completed_gpu_required_revision.reset();
        const auto view_index =
            static_cast<std::size_t>(
                ctx.streaming_viewport_index
            );
        tiles.viewport_tile_ids[view_index].clear();
        tiles.viewport_tile_query_boxes[view_index].reset();
    } else if (ctx.tile_result.enabled && !ctx.interacting) {
        /*
         * Incremental upload:
         * 1. Collect desired tiles that are in CPU cache
         * 2. Upload whatever fits in the per-frame GPU budget
         * 3. Update viewport_tile_ids incrementally —
         *    tiles appear as soon as they become resident
         * 4. Pin all desired resident tiles against LRU eviction
         */
        flush_lod_tile_stage();

        const auto view_index =
            static_cast<std::size_t>(ctx.streaming_viewport_index);

        // Collect GPU-required tiles already in CPU cache
        std::vector<std::pair<
            std::uint64_t, SharedTilePoints>> cached_desired;
        cached_desired.reserve(tiles.gpu_required_tile_ids.size());
        for (const auto tile_id : tiles.gpu_required_tile_ids) {
            auto pts = tiles.point_cache.find(tile_id);
            if (pts) {
                cached_desired.emplace_back(tile_id, pts);
            }
        }

        if (!cached_desired.empty()) {
            ctx.renderer.wait_for_in_flight_fences();
            gs3d::util::Stopwatch upload_timer;
            const auto upload_views =
                make_cached_tile_views(cached_desired);
            const auto sync =
                ctx.tile_gpu_cloud->sync_from_cached_tiles(
                    ctx.context,
                    ctx.renderer.command_pool(),
                    ctx.context.graphics_queue(),
                    upload_views,
                    tiles.gpu_required_tile_ids,
                    config.gpu_upload_budget_bytes
                );
            ctx.upload_record_ms_frame +=
                upload_timer.elapsed_milliseconds();
            ctx.lod_tile_timer.reset();

            const bool all_desired_resident =
                std::all_of(
                    tiles.gpu_required_tile_ids.begin(),
                    tiles.gpu_required_tile_ids.end(),
                    [&ctx](std::uint64_t tile_id) {
                        return ctx.tile_gpu_cloud
                            ->has_resident_tile(tile_id);
                    }) &&
                tiles.pending_load_ids.empty() &&
                !tiles.load_future.valid();

            if (config.verbose &&
                sync.uploaded_bytes > 0) {
                gs3d::util::log::info()
                    << "[TILE] upload slice: bytes="
                    << sync.uploaded_bytes
                    << ", resident="
                    << sync.resident_tile_count
                    << ", desired="
                    << tiles.gpu_required_tile_ids.size()
                    << ", complete="
                    << (all_desired_resident ? "true" : "false")
                    << '\n';
            }

            if (all_desired_resident &&
                tiles.completed_gpu_required_revision !=
                    tiles.gpu_required_revision) {
                const double reload_total_seconds =
                    tiles.async_cycle_timer.elapsed_seconds();
                log_tile_upload(
                    config.verbose,
                    ctx.tile_gpu_cloud->stats(),
                    sync,
                    reload_total_seconds
                );
                if (benchmark_enabled) {
                    ctx.reload_samples.push_back({
                        .seconds = reload_total_seconds,
                        .selected_tile_count = ctx.tile_result.tile_ids.size(),
                        .required_tile_count =
                            tiles.gpu_required_tile_ids.size(),
                        .resident_tile_count = static_cast<std::size_t>(
                            sync.resident_tile_count)
                    });
                }
                tiles.completed_gpu_required_revision =
                    tiles.gpu_required_revision;
            }
        }

        // Tick all required tiles so they remain fresh in the GPU
        // LRU ordering (the pin-only loop in sync_from_cached_tiles
        // already protects them against eviction this frame;
        // touch_tile provides a secondary tick update).
        for (const auto tile_id : tiles.gpu_required_tile_ids) {
            ctx.tile_gpu_cloud->touch_tile(tile_id);
        }

        // Incrementally update viewport: show whatever is
        // resident right now (strict subset of required working set).
        auto& vp_ids = tiles.viewport_tile_ids[view_index];
        vp_ids.clear();
        for (const auto tile_id : tiles.gpu_required_tile_ids) {
            if (ctx.tile_gpu_cloud->has_resident_tile(tile_id)) {
                vp_ids.push_back(tile_id);
            }
        }

        // Incrementally update clip bbox from currently
        // resident tiles.  Clip itself is gated on
        // all_desired_resident (see render section) so a
        // partial bbox is never used for clipping.
        if (!vp_ids.empty()) {
            tiles.viewport_tile_query_boxes[view_index] =
                compute_tiles_bbox(*ctx.tile_reader, vp_ids);
        } else {
            tiles.viewport_tile_query_boxes[view_index].reset();
        }
    }

    /*
     * Dispatch async disk reads for desired tiles that
     * are not yet in the CPU cache.  The per-frame upload
     * loop above picks them up as soon as they arrive.
     */
    const bool load_in_progress =
        tiles.load_future.valid();
    const double selection_stable_seconds =
        std::chrono::duration<double>(
            ctx.current_time - tiles.selection_changed_at
        ).count();
    const bool selection_stable =
        selection_stable_seconds >=
        kTileSelectionDebounceSeconds;

    const bool can_dispatch =
        selection_stable &&
        !load_in_progress &&
        !tiles.debounced_tile_ids.empty();

    if (can_dispatch) {
        std::size_t cache_hit_tiles = 0;
        std::size_t cache_miss_tiles = 0;
        const bool new_cache_request =
            tiles.cache_requested_tile_ids !=
            tiles.debounced_tile_ids;

        if (new_cache_request) {
            tiles.cache_requested_tile_ids =
                tiles.debounced_tile_ids;
            tiles.pending_load_ids.clear();
            tiles.pending_load_ids.reserve(
                tiles.debounced_tile_ids.size());
            tiles.async_cycle_timer.reset();

            for (const auto tile_id :
                 tiles.debounced_tile_ids) {
                // Account once per semantic working-set request. Internal
                // disk batches must not inflate hit/miss statistics.
                if (tiles.point_cache.get(tile_id)) {
                    ++cache_hit_tiles;
                } else {
                    ++cache_miss_tiles;
                    // A CPU miss needs disk I/O only if the tile is not
                    // already resident on the GPU from a previous view.
                    if (!ctx.tile_gpu_cloud
                             ->has_resident_tile(tile_id)) {
                        tiles.pending_load_ids.push_back(tile_id);
                    }
                }
            }

            if (tiles.pending_load_ids.empty() &&
                config.verbose) {
                gs3d::util::log::info()
                    << "[TILE] no disk reads needed for "
                    << tiles.debounced_tile_ids.size()
                    << " desired tiles (CPU-cached or GPU-resident)"
                    << " (candidates="
                    << ctx.tile_result.total_candidate_tiles
                    << "), uploading incrementally.\n";
            }
        } else if (tiles.pending_load_ids.empty()) {
            // A bounded CPU cache may evict a freshly-loaded tile before the
            // GPU upload reaches it. Reconcile without counting a second
            // semantic cache miss so the pipeline cannot stall permanently.
            for (const auto tile_id :
                 tiles.debounced_tile_ids) {
                if (!tiles.point_cache.find(tile_id) &&
                    !ctx.tile_gpu_cloud
                         ->has_resident_tile(tile_id)) {
                    tiles.pending_load_ids.push_back(tile_id);
                }
            }
        }

        if (!tiles.pending_load_ids.empty()) {
            auto batch_ids = take_tile_read_batch(
                *ctx.tile_reader,
                tiles.pending_load_ids);
            tiles.loading_ids = batch_ids;

            const auto ids = tiles.debounced_tile_ids;
            const auto candidate_tile_count =
                static_cast<std::size_t>(
                    ctx.tile_result.total_candidate_tiles
                );
            const auto request_revision =
                tiles.gpu_required_revision;
            tiles.load_future = std::async(
                std::launch::async,
                [&reader = *ctx.tile_reader,
                 &ids_by_tile = ctx.tile_point_ids_by_tile,
                 ids,
                 batch_ids = std::move(batch_ids),
                 cache_hit_tiles,
                 cache_miss_tiles,
                 candidate_tile_count,
                 request_revision]()
                    -> TileLoadResult {
                    gs3d::util::Stopwatch read_timer;
                    TileLoadResult loaded;
                    loaded.tile_ids = ids;
                    loaded.cache_hit_tiles =
                        cache_hit_tiles;
                    loaded.cache_miss_tiles =
                        cache_miss_tiles;
                    loaded.candidate_tiles =
                        candidate_tile_count;
                    loaded.request_revision =
                        request_revision;

                    loaded.loaded_tiles.reserve(
                        batch_ids.size());
                    for (const auto tile_id : batch_ids) {
                        auto pts =
                            load_tile_points_with_ids(
                                reader,
                                ids_by_tile,
                                tile_id);
                        loaded.loaded_tiles.push_back(
                            {tile_id, std::move(pts)});
                    }
                    loaded.read_seconds =
                        read_timer.elapsed_seconds();
                    return loaded;
                });

            if (config.verbose) {
                gs3d::util::log::info()
                    << "[TILE] async load dispatched: batch="
                    << tiles.loading_ids.size()
                    << ", remaining="
                    << tiles.pending_load_ids.size()
                    << ", desired=" << ids.size()
                    << " (candidates="
                    << candidate_tile_count
                    << ", cache_hit="
                    << cache_hit_tiles
                    << ", cache_miss="
                    << cache_miss_tiles
                    << ", revision="
                    << request_revision
                    << ").\n";
            }
        }
    }
}

} // namespace gs3d::app
