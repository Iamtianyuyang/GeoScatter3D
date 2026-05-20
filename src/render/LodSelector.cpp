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
}

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
) const noexcept {
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

    if (interacting_ && config_.use_lowest_while_interacting) {
        return lowest;
    }

    if (idle_seconds_ >= config_.high_delay_seconds) {
        return highest;
    }

    if (idle_seconds_ >= config_.medium_delay_seconds) {
        return medium;
    }

    return lowest;
}

bool LodSelector::interacting() const noexcept {
    return interacting_;
}

double LodSelector::idle_seconds() const noexcept {
    return idle_seconds_;
}

} // namespace gs3d::render