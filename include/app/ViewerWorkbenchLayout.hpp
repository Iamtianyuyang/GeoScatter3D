#pragma once

#include <optional>

namespace gs3d::app {

struct DesktopWorkArea {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct WindowFrameInsets {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

struct WorkbenchWindowLayout {
    int client_width = 1;
    int client_height = 1;
    std::optional<int> outer_x;
    std::optional<int> outer_y;
};

// Computes the first-launch 16:10 workbench geometry without depending on
// GLFW. An invalid or unavailable work area keeps the default size and skips
// centering instead of deriving an off-screen position.
[[nodiscard]] WorkbenchWindowLayout compute_workbench_window_layout(
    float ui_scale,
    std::optional<DesktopWorkArea> work_area,
    WindowFrameInsets frame_insets
) noexcept;

} // namespace gs3d::app
