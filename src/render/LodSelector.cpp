#include "render/LodSelector.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

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
    cooldown_remaining_ = 0;
}

namespace {
// ~0.75s of good frames at 60fps before climbing one level of detail.
// Climbing is deliberately much slower than backing off (which happens
// on the very next over-budget frame) — see LodSelector.hpp.
// Raised from 30 after observing flicker during rapid zoom; the longer
// streak gives frame-time variance time to settle.
constexpr int kGoodStreakToClimb = 45;

// 降级后 N 帧内禁止升级，防止 "刚降完立刻又想升" 的脉冲闪烁。
// 快速缩放时帧时间波动大，没有这个冷却会导致每几帧就跳一次细节级别。
constexpr int kDropCooldownFrames = 20;  // ~330ms at 60fps

// 环境变量 GS3D_LOD_DEBUG=1 开启 LOD 切换日志（每个切换事件打一条）
bool lod_debug_enabled() {
    static bool checked = false;
    static bool enabled = false;
    if (!checked) {
        const char* env = std::getenv("GS3D_LOD_DEBUG");
        enabled = (env != nullptr && env[0] == '1');
        checked = true;
    }
    return enabled;
}
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

    /*
     * 自适应模式过渡期：刚停止交互时 idle_seconds_ ≈ 0，如果直接 fall
     * through 到下面 return lowest，会从自适应已爬升到的级别（如 level 0）
     * 瞬间跳到 lowest，然后随 idle_seconds_ 增长再逐步升回去——在快速
     * 滚轮缩放中 interacting 标志帧间抖动时，这个跳变就是显眼的闪烁。
     *
     * 解法：自适应模式下用 adaptive_level_ 作最低基准，直到 idle 时间
     * 足够让常规延迟路径接手。这样就平滑连接了"交互刚结束"和"空闲恢复"。
     */
    if (config_.adaptive_interacting_level &&
        adaptive_level_ >= 0 &&
        adaptive_level_ < static_cast<long>(level_count)) {
        const std::size_t base =
            static_cast<std::size_t>(adaptive_level_);

        if (idle_seconds_ >= config_.high_delay_seconds) {
            return highest;
        }

        if (idle_seconds_ >= config_.medium_delay_seconds) {
            // medium 可能比 adaptive_level_ 更粗或更细；取两者中更细的
            // (索引更小的 = 更精细)，不要因为过渡期而降级。
            return std::min(base, medium);
        }

        // 刚停交互：保持 adaptive level，不跌到 lowest
        return base;
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
        return;
    }

    // 冷却递减（每帧减 1）
    if (cooldown_remaining_ > 0) {
        --cooldown_remaining_;
    }

    if (frame_time_ms > config_.frame_time_budget_ms) {
        good_frame_streak_ = 0;
        cooldown_remaining_ = kDropCooldownFrames;
        if (adaptive_level_ + 1 < static_cast<long>(last_level_count_)) {
            ++adaptive_level_; // step toward lower detail
            if (lod_debug_enabled()) {
                std::fprintf(stderr,
                    "[LODDBG] DROP  to level %ld (ft=%.2f ms, cooldown=%d)\n",
                    adaptive_level_, frame_time_ms, kDropCooldownFrames);
            }
        }
        return;
    }

    // 在冷却期内不计数好帧——防止降级后立刻反弹
    if (cooldown_remaining_ > 0) {
        return;
    }

    ++good_frame_streak_;
    if (good_frame_streak_ >= kGoodStreakToClimb) {
        good_frame_streak_ = 0;
        if (adaptive_level_ > 0) {
            --adaptive_level_; // step toward higher detail
            if (lod_debug_enabled()) {
                std::fprintf(stderr,
                    "[LODDBG] CLIMB to level %ld (streak=%d, ft=%.2f ms)\n",
                    adaptive_level_, kGoodStreakToClimb, frame_time_ms);
            }
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