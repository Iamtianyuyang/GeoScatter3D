#pragma once

#include "data/Gs3dFormat.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace gs3d::app {

enum class DistanceDisplayMode : std::uint8_t {
    ThreeD  = 0,
    Planar  = 1,
    Both    = 2
};

struct MeasurementLine {
    gs3d::data::Gs3dPoint point_a{};
    gs3d::data::Gs3dPoint point_b{};
    std::uint32_t color = 0;
    bool fixed = false;

    [[nodiscard]]
    float distance_3d() const noexcept;

    [[nodiscard]]
    float distance_planar() const noexcept;

    [[nodiscard]]
    std::string distance_label(DistanceDisplayMode mode) const;
};

class MeasurementManager {
public:
    MeasurementManager() = default;

    [[nodiscard]]
    bool measure_mode_active() const noexcept { return measure_mode_active_; }

    void toggle_measure_mode() noexcept { measure_mode_active_ = !measure_mode_active_; }
    void set_measure_mode(bool active) noexcept { measure_mode_active_ = active; }

    [[nodiscard]]
    DistanceDisplayMode display_mode() const noexcept { return display_mode_; }

    void set_display_mode(DistanceDisplayMode mode) noexcept { display_mode_ = mode; }

    // Add a world-space point. If there's no pending first point, stores it.
    // If there's a pending point, pairs them into a line and clears pending.
    // Returns true when a new line was completed.
    bool add_point(const gs3d::data::Gs3dPoint& point);

    // Clear the pending first point (e.g. on mode exit).
    void clear_pending() noexcept { pending_first_point_.reset(); }

    [[nodiscard]]
    bool has_pending() const noexcept { return pending_first_point_.has_value(); }

    [[nodiscard]]
    const std::optional<gs3d::data::Gs3dPoint>& pending_point() const noexcept {
        return pending_first_point_;
    }

    [[nodiscard]]
    const std::vector<MeasurementLine>& lines() const noexcept { return lines_; }

    [[nodiscard]]
    std::vector<MeasurementLine>& lines() noexcept { return lines_; }

    [[nodiscard]]
    std::size_t line_count() const noexcept { return lines_.size(); }

    void remove_line(std::size_t index) noexcept;
    void remove_all_unfixed() noexcept;
    void toggle_fixed(std::size_t index) noexcept;
    void set_line_color(std::size_t index, std::uint32_t color) noexcept;

    // Preset color palette for auto-assignment.
    static const std::vector<std::uint32_t>& color_palette() noexcept;

    // Called after any mutation (add/remove/toggle/color change).
    // Set by the application layer to trigger auto-save.
    std::function<void()> on_changed;

private:
    bool measure_mode_active_ = false;
    DistanceDisplayMode display_mode_ = DistanceDisplayMode::Planar;
    std::optional<gs3d::data::Gs3dPoint> pending_first_point_{};
    std::vector<MeasurementLine> lines_{};
    int next_color_index_ = 0;
};

} // namespace gs3d::app
