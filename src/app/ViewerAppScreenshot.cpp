#include "app/ViewerApp.hpp"
#include "app/ViewerAppRunState.hpp"

#include "app/AppState.hpp"
#include "app/UiActions.hpp"
#include "imgui.h"
#include "render/VulkanSwapchain.hpp"

namespace gs3d::app {

void ViewerApp::apply_screenshot_command(
    const UiActions& gui_cmds,
    ViewerAppScreenshotContext& ctx
) {
    if (!gui_cmds.screenshot_requested) {
        return;
    }
    // Map viewport 0's canvas_rect (ImGui screen coords) →
    // swapchain physical pixels.
    for (const auto& view : ctx.app_state.render_views) {
        if (view.viewport_index != 0) continue;
        if (view.canvas_rect_max_x <= view.canvas_rect_min_x ||
            view.canvas_rect_max_y <= view.canvas_rect_min_y) break;
        const auto& io = ImGui::GetIO();
        const ImVec2 vp_pos = ImGui::GetMainViewport()->Pos;
        const float sx = io.DisplayFramebufferScale.x;
        const float sy = io.DisplayFramebufferScale.y;
        int x = static_cast<int>((view.canvas_rect_min_x - vp_pos.x) * sx);
        int y = static_cast<int>((view.canvas_rect_min_y - vp_pos.y) * sy);
        int w = static_cast<int>((view.canvas_rect_max_x - view.canvas_rect_min_x) * sx);
        int h = static_cast<int>((view.canvas_rect_max_y - view.canvas_rect_min_y) * sy);
        // Clamp to swapchain extent
        const auto& sc_ext = ctx.swapchain.extent();
        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (x + w > static_cast<int>(sc_ext.width))  w = static_cast<int>(sc_ext.width)  - x;
        if (y + h > static_cast<int>(sc_ext.height)) h = static_cast<int>(sc_ext.height) - y;
        if (w > 0 && h > 0) {
            ctx.screenshot_offset = {static_cast<std::uint32_t>(x),
                                     static_cast<std::uint32_t>(y)};
            ctx.screenshot_extent = {static_cast<std::uint32_t>(w),
                                     static_cast<std::uint32_t>(h)};
            ctx.screenshot_pending = true;
        }
        break;
    }
}

} // namespace gs3d::app
