#include "app/TilePointCache.hpp"

#include <limits>
#include <shared_mutex>

namespace gs3d::app {

namespace {

bool tile_bytes_for(
    const SharedTilePoints& points,
    std::uint64_t& bytes
) noexcept {
    if (!points) {
        return false;
    }

    const auto point_count =
        static_cast<std::uint64_t>(points->size());
    if (point_count != points->point_ids.size() ||
        point_count >
            std::numeric_limits<std::uint64_t>::max() /
                (sizeof(gs3d::data::Gs3dPoint) +
                 sizeof(std::uint32_t))) {
        return false;
    }

    bytes =
        point_count *
        (sizeof(gs3d::data::Gs3dPoint) + sizeof(std::uint32_t));
    return true;
}

} // namespace

TilePointCache::TilePointCache(std::uint64_t max_bytes)
    : max_bytes_(max_bytes)
{
}

SharedTilePoints TilePointCache::get(std::uint64_t tile_id)
{
    std::unique_lock lock(mutex_);
    const auto found = entries_.find(tile_id);
    if (found == entries_.end()) {
        ++misses_;
        return {};
    }

    ++hits_;
    lru_.splice(lru_.begin(), lru_, found->second.lru_position);
    return found->second.points;
}

SharedTilePoints TilePointCache::find(std::uint64_t tile_id) const
{
    std::shared_lock lock(mutex_);
    const auto found = entries_.find(tile_id);
    if (found == entries_.end()) {
        return {};
    }
    return found->second.points;
}

void TilePointCache::put(
    std::uint64_t tile_id,
    SharedTilePoints points
) {
    std::uint64_t bytes = 0;
    if (!tile_bytes_for(points, bytes)) {
        return;
    }

    std::unique_lock lock(mutex_);
    if (const auto existing = entries_.find(tile_id);
        existing != entries_.end()) {
        erase_entry(existing);
    }

    if (max_bytes_ == 0 || bytes > max_bytes_) {
        return;
    }

    while (!lru_.empty() &&
           resident_bytes_ > max_bytes_ - bytes) {
        const auto oldest_id = lru_.back();
        const auto oldest = entries_.find(oldest_id);
        if (oldest == entries_.end()) {
            lru_.pop_back();
            continue;
        }
        erase_entry(oldest);
        ++evictions_;
    }

    lru_.push_front(tile_id);
    resident_bytes_ += bytes;
    entries_.emplace(
        tile_id,
        Entry{
            std::move(points),
            bytes,
            lru_.begin()
        }
    );
}

bool TilePointCache::put_if_space(
    std::uint64_t tile_id,
    SharedTilePoints points
) {
    std::uint64_t bytes = 0;
    if (!tile_bytes_for(points, bytes)) {
        return false;
    }

    std::unique_lock lock(mutex_);
    if (entries_.contains(tile_id)) {
        return true;
    }
    if (max_bytes_ == 0 ||
        bytes > max_bytes_ ||
        resident_bytes_ > max_bytes_ - bytes) {
        return false;
    }

    lru_.push_back(tile_id);
    auto lru_position = lru_.end();
    --lru_position;
    resident_bytes_ += bytes;
    entries_.emplace(
        tile_id,
        Entry{
            std::move(points),
            bytes,
            lru_position
        }
    );
    return true;
}

void TilePointCache::clear() noexcept
{
    std::unique_lock lock(mutex_);
    entries_.clear();
    lru_.clear();
    resident_bytes_ = 0;
    hits_ = 0;
    misses_ = 0;
    evictions_ = 0;
}

TilePointCacheStats TilePointCache::stats() const noexcept
{
    std::shared_lock lock(mutex_);
    return {
        max_bytes_,
        resident_bytes_,
        entries_.size(),
        hits_,
        misses_,
        evictions_
    };
}

void TilePointCache::erase_entry(
    std::unordered_map<std::uint64_t, Entry>::iterator entry
) noexcept {
    resident_bytes_ -= entry->second.bytes;
    lru_.erase(entry->second.lru_position);
    entries_.erase(entry);
}

} // namespace gs3d::app
