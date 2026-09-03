#include "app/ViewerWorkbenchLayout.hpp"

#include <algorithm>
#include <cmath>

namespace gs3d::app {

WorkbenchWindowLayout compute_workbench_window_layout(
    const float ui_scale,
    const std::optional<DesktopWorkArea> work_area,
    const WindowFrameInsets frame_insets
) noexcept {
    constexpr int kBaseOuterWidth = 1440;
    constexpr int kMinOuterWidth = 1100;
    constexpr float kAspect = 16.0f / 10.0f;

    const float scale = std::isfinite(ui_scale) && ui_scale > 0.0f
        ? ui_scale
        : 1.0f;
    int outer_width = std::max(
        kMinOuterWidth,
        static_cast<int>(std::lround(
            static_cast<float>(kBaseOuterWidth) * scale
        ))
    );

    if (work_area.has_value() && work_area->width > 0 &&
        work_area->height > 0) {
        const int width_limit = static_cast<int>(std::floor(
            static_cast<float>(work_area->width) * 0.85f
        ));
        const int height_limit = static_cast<int>(std::floor(
            static_cast<float>(work_area->height) * 0.85f
        ));
        const int width_from_height = static_cast<int>(std::floor(
            static_cast<float>(height_limit) * kAspect
        ));
        outer_width = std::clamp(
            outer_width,
            kMinOuterWidth,
            std::max(kMinOuterWidth, std::min(width_limit, width_from_height))
        );
    }

    const int outer_height = static_cast<int>(std::lround(
        static_cast<float>(outer_width) / kAspect
    ));
    WorkbenchWindowLayout layout{
        .client_width = std::max(
            1,
            outer_width - frame_insets.left - frame_insets.right
        ),
        .client_height = std::max(
            1,
            outer_height - frame_insets.top - frame_insets.bottom
        ),
    };
    if (work_area.has_value() && work_area->width > 0 &&
        work_area->height > 0) {
        layout.outer_x = work_area->x +
            (work_area->width - outer_width) / 2;
        layout.outer_y = work_area->y +
            (work_area->height - outer_height) / 2;
    }
    return layout;
}

} // namespace gs3d::app
