#pragma once

#include "app/ViewerAppInternal.hpp"
#include "data/Gs3dFormat.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace gs3d::app {

struct ViewerAppPickState {
    std::vector<GpuPickRequest> requests;
    std::vector<std::optional<gs3d::data::Gs3dPoint>> latest_hover_points;
    std::vector<float> latest_capture_x;
    std::vector<float> latest_capture_y;
    std::vector<int> hover_timeout;
    std::vector<int> consecutive_no_hit;
    std::uint32_t frame_slot = 0;
};

} // namespace gs3d::app
