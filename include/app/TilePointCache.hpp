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

using TilePoints = std::vector<gs3d::data::Gs3dPoint>;
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
        std::shared_ptr<TilePoints> points
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
