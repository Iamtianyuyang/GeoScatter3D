#include "app/MeasurementManager.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace gs3d::app {

// ── MeasurementLine ──────────────────────────────────────────────────────────

float MeasurementLine::distance_3d() const noexcept {
    const float dx = point_b.x - point_a.x;
    const float dy = point_b.y - point_a.y;
    const float dz = point_b.z - point_a.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float MeasurementLine::distance_planar() const noexcept {
    const float dx = point_b.x - point_a.x;
    const float dy = point_b.y - point_a.y;
    return std::sqrt(dx * dx + dy * dy);
}

std::string MeasurementLine::distance_label(DistanceDisplayMode mode) const {
    char buf[64];
    switch (mode) {
        case DistanceDisplayMode::ThreeD:
            std::snprintf(buf, sizeof(buf), "%.2f", distance_3d());
            break;
        case DistanceDisplayMode::Planar:
            std::snprintf(buf, sizeof(buf), "%.2f (XY)", distance_planar());
            break;
        case DistanceDisplayMode::Both: {
            std::snprintf(buf, sizeof(buf), "3D: %.2f  XY: %.2f",
                          distance_3d(), distance_planar());
            break;
        }
    }
    return buf;
}

// ── MeasurementManager ───────────────────────────────────────────────────────

bool MeasurementManager::add_point(const gs3d::data::Gs3dPoint& point) {
    if (!pending_first_point_.has_value()) {
        pending_first_point_ = point;
        return false;
    }

    const auto& palette = color_palette();
    const std::uint32_t color = palette[
        static_cast<std::size_t>(next_color_index_) % palette.size()];
    ++next_color_index_;

    lines_.push_back(MeasurementLine{
        *pending_first_point_,
        point,
        color,
        false
    });
    pending_first_point_.reset();
    if (on_changed) { on_changed(); }
    return true;
}

void MeasurementManager::remove_line(std::size_t index) noexcept {
    if (index < lines_.size()) {
        lines_.erase(lines_.begin() + static_cast<std::ptrdiff_t>(index));
        if (on_changed) { on_changed(); }
    }
}

void MeasurementManager::remove_all_unfixed() noexcept {
    const auto before = lines_.size();
    lines_.erase(
        std::remove_if(lines_.begin(), lines_.end(),
                       [](const MeasurementLine& line) { return !line.fixed; }),
        lines_.end());
    if (lines_.size() != before && on_changed) { on_changed(); }
}

void MeasurementManager::toggle_fixed(std::size_t index) noexcept {
    if (index < lines_.size()) {
        lines_[index].fixed = !lines_[index].fixed;
        if (on_changed) { on_changed(); }
    }
}

void MeasurementManager::set_line_color(std::size_t index, std::uint32_t color) noexcept {
    if (index < lines_.size()) {
        lines_[index].color = color;
        if (on_changed) { on_changed(); }
    }
}

const std::vector<std::uint32_t>& MeasurementManager::color_palette() noexcept {
    // High-visibility palette against dark viewport backgrounds.
    static const std::vector<std::uint32_t> palette = {
        0xFF3CD6FF, // cyan
        0xFFFF963C, // orange
        0xFF5CF53C, // green
        0xFFFF3CEC, // magenta
        0xFFF5E73C, // yellow
        0xFF3C8CFF, // blue
        0xFFFF5C5C, // red
        0xFF3CFFB8, // mint
    };
    return palette;
}

} // namespace gs3d::app
