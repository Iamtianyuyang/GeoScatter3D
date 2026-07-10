#pragma once

#include "data/Gs3dFormat.hpp"

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace gs3d::app {

struct TilePoints {
    std::vector<gs3d::data::Gs3dPoint> points{};
    std::vector<std::uint32_t> point_ids{};

    [[nodiscard]]
    std::size_t size() const noexcept {
        return points.size();
    }

    [[nodiscard]]
    bool empty() const noexcept {
        return points.empty();
    }
};

using SharedTilePoints = std::shared_ptr<const TilePoints>;

struct TilePointCacheStats {
    std::uint64_t max_bytes = 0;
    std::uint64_t resident_bytes = 0;
    std::size_t tile_count = 0;
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
    std::uint64_t evictions = 0;
};

class TilePointCache {
public:
    explicit TilePointCache(std::uint64_t max_bytes);

    [[nodiscard]]
    SharedTilePoints get(std::uint64_t tile_id);

    // Read-only lookup (no LRU update). Safe to call concurrently with other
    // readers. Use when the caller only needs the data, not LRU recency.
    [[nodiscard]]
    SharedTilePoints find(std::uint64_t tile_id) const;

    void put(
        std::uint64_t tile_id,
        SharedTilePoints points
    );

    // Admit speculative/stale data only when unused capacity is available.
    // The entry is inserted at the cold end of the LRU and never evicts an
    // existing hot tile. Returns true when the tile is cached (or already
    // present), false when it is invalid or there is insufficient space.
    [[nodiscard]]
    bool put_if_space(
        std::uint64_t tile_id,
        SharedTilePoints points
    );

    void clear() noexcept;

    [[nodiscard]]
    TilePointCacheStats stats() const noexcept;

private:
    struct Entry {
        SharedTilePoints points;
        std::uint64_t bytes = 0;
        std::list<std::uint64_t>::iterator lru_position;
    };

    void erase_entry(
        std::unordered_map<std::uint64_t, Entry>::iterator entry
    ) noexcept;

    const std::uint64_t max_bytes_;
    std::uint64_t resident_bytes_ = 0;
    std::uint64_t hits_ = 0;
    std::uint64_t misses_ = 0;
    std::uint64_t evictions_ = 0;
    std::list<std::uint64_t> lru_;
    std::unordered_map<std::uint64_t, Entry> entries_;
    mutable std::shared_mutex mutex_;
};

} // namespace gs3d::app
