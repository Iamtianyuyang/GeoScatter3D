#include "app/ViewerFrameMetricsCollector.hpp"

#include "app/ViewerAppTileStreaming.hpp"
#include "data/Gs3dTileReader.hpp"
#include "render/PointCloudGpu.hpp"
#include "render/PointCloudLodGpu.hpp"
#include "render/PointCloudTileGpu.hpp"
#include "render/TileSelection.hpp"

#include <algorithm>
#include <limits>

namespace gs3d::app {

ViewerFrameMetricsSnapshot ViewerFrameMetricsCollector::collect(
    const ViewerFrameMetricsContext& context
) const {
    std::uint32_t loaded_tiles = 0;
    std::size_t pending_tile_count = 0;
    if (context.tile_enabled && context.tile_gpu_cloud != nullptr &&
        context.tile_result.enabled) {
        const auto& required_tile_ids =
            context.tile_stream.gpu_required_tile_ids.empty()
                ? context.tile_result.tile_ids
                : context.tile_stream.gpu_required_tile_ids;
        for (const auto tile_id : required_tile_ids) {
            if (!context.tile_gpu_cloud->has_resident_tile(tile_id)) {
                ++pending_tile_count;
            }
        }
    }
    if (context.tile_stream.load_future.valid()) {
        pending_tile_count = std::max(
            pending_tile_count,
            context.tile_stream.loading_ids.size()
        );
    }
    const auto pending_tiles = static_cast<std::uint32_t>(
        std::min<std::size_t>(
            pending_tile_count,
            std::numeric_limits<std::uint32_t>::max()
        )
    );

    std::uint64_t gpu_buffer_bytes = 0;
    std::uint64_t gpu_resident_points = 0;
    if (context.tile_enabled && context.tile_gpu_cloud != nullptr) {
        const auto& tile_stats = context.tile_gpu_cloud->stats();
        loaded_tiles = static_cast<std::uint32_t>(
            tile_stats.resident_tile_count
        );
        gpu_buffer_bytes += tile_stats.gpu_buffer_bytes;
        gpu_resident_points += tile_stats.point_count;
    }
    if (context.full_gpu_cloud != nullptr) {
        gpu_buffer_bytes += static_cast<std::uint64_t>(
            context.full_gpu_cloud->vertex_buffer_size()
        );
        gpu_resident_points += context.full_gpu_cloud->point_count();
    }
    if (context.lod_gpu_cloud != nullptr) {
        for (std::size_t index = 0;
             index < context.lod_gpu_cloud->level_count();
             ++index) {
            gpu_buffer_bytes += static_cast<std::uint64_t>(
                context.lod_gpu_cloud->gpu_cloud(index).vertex_buffer_size()
            );
            gpu_resident_points +=
                context.lod_gpu_cloud->level(index).gpu_point_count;
        }
    }

    std::uint64_t visible_points = gpu_resident_points;
    if (context.tile_result.enabled && context.tile_reader != nullptr) {
        visible_points = 0;
        for (const auto tile_id : context.tile_result.tile_ids) {
            visible_points += context.tile_reader->record(tile_id).point_count;
        }
    }

    // 全量预加载进度（加载门禁 UI）。active 期间界面显示加载页。
    TilePreloadProgressState preload;
    const auto& tile_stream = context.tile_stream;
    preload.active =
        tile_stream.preload_enabled &&
        !tile_stream.tiles_fully_resident &&
        !tile_stream.preload_failed;
    preload.reading =
        preload.active &&
        tile_stream.preload_uploads.empty() &&
        tile_stream.preload_tiles.empty();
    preload.read_tiles = tile_stream.preload_read_tiles.load(
        std::memory_order_relaxed);
    if (context.tile_reader != nullptr) {
        const auto reader_stats = context.tile_reader->stats();
        preload.total_tiles = reader_stats.tile_count;
        preload.total_bytes = reader_stats.total_point_bytes;
    }
    if (context.tile_gpu_cloud != nullptr) {
        const auto& tile_stats = context.tile_gpu_cloud->stats();
        preload.resident_tiles = tile_stats.resident_tile_count;
        preload.resident_bytes = tile_stats.gpu_buffer_bytes;
    }

    const auto cache_stats = context.tile_stream.point_cache.stats();
    return {
        .tile_cache = {
            .resident_bytes = cache_stats.resident_bytes,
            .max_bytes = cache_stats.max_bytes,
            .hits = cache_stats.hits,
            .misses = cache_stats.misses
        },
        .frame_state = {
            .dataset_point_count = context.dataset_point_count,
            .gpu_resident_points = gpu_resident_points,
            .visible_points = visible_points,
            .gpu_buffer_bytes = gpu_buffer_bytes,
            .loaded_tiles = loaded_tiles,
            .pending_tiles = pending_tiles,
            .fps = context.fps,
            .frame_time_ms = context.delta_seconds > 0.0
                ? static_cast<float>(context.delta_seconds * 1000.0)
                : 0.0f,
            .camera_position = context.camera_position,
            .tile_preload = preload
        }
    };
}

} // namespace gs3d::app
