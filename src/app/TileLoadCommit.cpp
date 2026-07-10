#include "app/ViewerAppTileStreaming.hpp"

#include "app/TilePointCache.hpp"

#include <unordered_set>

namespace gs3d::app {

TileLoadCommitStats commit_streaming_tile_load_result(
    TilePointCache& cache,
    std::span<const LoadedTilePoints> loaded_tiles,
    std::span<const std::uint64_t> current_required_tile_ids
) {
    TileLoadCommitStats stats;

    // Build fast-lookup set from current required working set.
    const std::unordered_set<std::uint64_t> required_set(
        current_required_tile_ids.begin(),
        current_required_tile_ids.end());

    for (const auto& entry : loaded_tiles) {
        if (!entry.points ||
            entry.points->points.empty() ||
            entry.points->point_ids.empty() ||
            entry.points->points.size() !=
                entry.points->point_ids.size()) {
            ++stats.invalid;
            continue;
        }

        if (!required_set.contains(entry.tile_id)) {
            // Tile no longer belongs to the current GPU working set.
            ++stats.stale_discarded;
            continue;
        }

        // Single-writer invariant: Stage 3 is the only path that calls
        // point_cache.put().  The find() -> put() check-then-insert is
        // safe without a mutex because no other thread writes the cache
        // while Stage 3 is active.
        if (cache.find(entry.tile_id)) {
            ++stats.already_cached;
            continue;
        }

        // ponytail: make a mutable copy because put() takes
        // shared_ptr<TilePoints> (non-const).
        auto mutable_points =
            std::make_shared<TilePoints>(
                *entry.points);
        cache.put(entry.tile_id, std::move(mutable_points));
        ++stats.accepted;
    }

    return stats;
}

} // namespace gs3d::app
