#pragma once

#include <cstddef>

namespace gs3d::render {

struct LodSelectorConfig {
    /*
     * 交互停止后，超过该时间进入 medium LOD。
     */
    double medium_delay_seconds = 0.20;

    /*
     * 交互停止后，超过该时间进入 high LOD。
     */
    double high_delay_seconds = 0.80;

    /*
     * 如果为 true，交互中始终使用最低细节 LOD。
     */
    bool use_lowest_while_interacting = true;
};

class LodSelector {
public:
    LodSelector() = default;

    explicit LodSelector(LodSelectorConfig config);

    void set_config(const LodSelectorConfig& config) noexcept;

    void reset() noexcept;

    void update(
        bool interacting,
        double delta_seconds
    ) noexcept;

    [[nodiscard]]
    std::size_t select_level(
        std::size_t level_count
    ) const noexcept;

    [[nodiscard]]
    bool interacting() const noexcept;

    [[nodiscard]]
    double idle_seconds() const noexcept;

private:
    LodSelectorConfig config_{};

    bool interacting_ = false;
    double idle_seconds_ = 0.0;
};

} // namespace gs3d::render