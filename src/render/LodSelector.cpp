#include "render/LodSelector.hpp"

#include <algorithm>

namespace gs3d::render {

LodSelector::LodSelector(
    LodSelectorConfig config
)
    : config_(config)
{
}

void LodSelector::set_config(
    const LodSelectorConfig& config
) noexcept {
    config_ = config;
}

void LodSelector::reset() noexcept {
    interacting_ = false;
    idle_seconds_ = 0.0;
    adaptive_level_ = -1;
    good_frame_streak_ = 0;
}

namespace {
// ~0.5s of good frames at 60fps before climbing one level of detail.
// Climbing is deliberately much slower than backing off (which happens
// on the very next over-budget frame) — see LodSelector.hpp.
constexpr int kGoodStreakToClimb = 30;
} // namespace

void LodSelector::update(
    bool interacting,
    double delta_seconds
) noexcept {
    interacting_ = interacting;

    if (interacting_) {
        idle_seconds_ = 0.0;
        return;
    }

    idle_seconds_ += std::max(delta_seconds, 0.0);
}

std::size_t LodSelector::select_level(
    std::size_t level_count
) noexcept {
    if (level_count == 0) {
        return 0;
    }

    if (level_count == 1) {
        return 0;
    }

    /*
     * 约定：
     *   level 0 是最高细节
     *   level_count - 1 是最低细节
     */
    const std::size_t highest = 0;
    const std::size_t lowest = level_count - 1;
    const std::size_t medium = level_count / 2;

    if (interacting_) {
        if (config_.adaptive_interacting_level) {
            last_level_count_ = level_count;
            if (adaptive_level_ < 0 ||
                adaptive_level_ >= static_cast<long>(level_count)) {
                // First use this session (or the level ladder shrank):
                // start conservative at the lowest detail level.
                adaptive_level_ = static_cast<long>(lowest);
            }
            return static_cast<std::size_t>(adaptive_level_);
        }

        if (config_.use_lowest_while_interacting) {
            return lowest;
        }
    }

    if (idle_seconds_ >= config_.high_delay_seconds) {
        return highest;
    }

    if (idle_seconds_ >= config_.medium_delay_seconds) {
        return medium;
    }

    return lowest;
}

void LodSelector::report_frame_time(
    std::size_t level_used,
    double frame_time_ms
) noexcept {
    if (!interacting_ || !config_.adaptive_interacting_level) {
        return;
    }

    if (adaptive_level_ < 0 ||
        static_cast<std::size_t>(adaptive_level_) != level_used) {
        // Stale feedback for a level we're no longer tracking — ignore.
        return;
    }

    if (frame_time_ms > config_.frame_time_budget_ms) {
        good_frame_streak_ = 0;
        if (adaptive_level_ + 1 < static_cast<long>(last_level_count_)) {
            ++adaptive_level_; // step toward lower detail
        }
        return;
    }

    ++good_frame_streak_;
    if (good_frame_streak_ >= kGoodStreakToClimb) {
        good_frame_streak_ = 0;
        if (adaptive_level_ > 0) {
            --adaptive_level_; // step toward higher detail
        }
    }
}

bool LodSelector::interacting() const noexcept {
    return interacting_;
}

double LodSelector::idle_seconds() const noexcept {
    return idle_seconds_;
}

} // namespace gs3d::render