#pragma once

#include <cstddef>
#include <vector>

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

    /*
     * 在 use_lowest_while_interacting 之上做帧时间自适应：交互时不固定用
     * 最低档，而是按 report_frame_time() 测得的帧时间在档位间爬升/回退
     * （AIMD 风格——超预算立刻退一档，达标若干帧才升一档）。开启后这个
     * 设置优先于 use_lowest_while_interacting。
     */
    bool adaptive_interacting_level = false;

    /*
     * report_frame_time() 判断"超预算"的阈值，应小于 60fps 对应的 16.6ms，
     * 留出余量（默认 14ms）。
     */
    double frame_time_budget_ms = 14.0;
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
    ) noexcept;

    /*
     * 每帧渲染完成后调用一次（仅在 adaptive_interacting_level 时有意义，
     * 否则是 no-op）：report 这一帧实际用的档位和测得的帧时间，驱动下一帧
     * 的爬升/回退决策。`level_used` 必须是上一次 select_level() 的返回值，
     * 否则会被当作过期反馈忽略。
     */
    void report_frame_time(
        std::size_t level_used,
        double frame_time_ms
    ) noexcept;

    [[nodiscard]]
    bool interacting() const noexcept;

    [[nodiscard]]
    double idle_seconds() const noexcept;

    /*
     * Potree 式空间选层：选出 voxel_size ≤ world_per_pixel 的最粗层。
     *
     *   world_per_pixel = ortho_height / viewport_height (正交投影)
     *   voxel_sizes[i]  = 第 i 层的 voxel_size (i=0 最精细)
     *
     * 滞回 (hysteresis)：切到更粗层时阈值放宽为 voxel_size × hysteresis，
     * 防止 world_per_pixel 在边界附近微抖导致两层间反复横跳。
     * hysteresis 默认 1.2（20% 余量）。传 1.0 则无滞回。
     *
     * previous_level = level_count 表示"无前值"（首帧或刚重置不出滞回）。
     */
    [[nodiscard]]
    static std::size_t select_level_by_spacing(
        float world_per_pixel,
        const std::vector<float>& voxel_sizes,
        std::size_t previous_level,
        float hysteresis = 1.2f
    ) noexcept;

private:
    LodSelectorConfig config_{};

    bool interacting_ = false;
    double idle_seconds_ = 0.0;

    // -1 = no adaptive level chosen yet for the current/next interacting
    // session; initialized to `lowest` the first time select_level() needs it.
    long adaptive_level_ = -1;
    std::size_t last_level_count_ = 0;
    int good_frame_streak_ = 0;

    /*
     * 降级冷却：每次 report_frame_time 触发降级后，接下来的
     * kDropCooldownFrames 帧内禁止升级。防止快速缩放时帧时间波动
     * 导致的 "降-升-降" 脉冲闪烁。
     */
    int cooldown_remaining_ = 0;
};

} // namespace gs3d::render