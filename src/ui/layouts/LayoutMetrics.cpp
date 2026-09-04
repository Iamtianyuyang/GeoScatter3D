#include "ui/layouts/LayoutMetrics.hpp"

namespace gs3d::ui {

const LayoutGeometry& default_layout_geometry() noexcept {
    static const LayoutGeometry kDefault;
    return kDefault;
}

void apply_layout_geometry(const LayoutGeometry& geom, float ui_scale) {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = geom.window_rounding * ui_scale;
    style.ChildRounding = geom.child_rounding * ui_scale;
    style.FrameRounding = geom.frame_rounding * ui_scale;
    style.PopupRounding = geom.popup_rounding * ui_scale;
    style.ScrollbarRounding = geom.scrollbar_rounding * ui_scale;
    style.GrabRounding = geom.grab_rounding * ui_scale;
    style.TabRounding = geom.tab_rounding * ui_scale;

    style.WindowBorderSize = geom.window_border_size;
    style.ChildBorderSize = geom.child_border_size;
    style.PopupBorderSize = geom.popup_border_size;
    style.FrameBorderSize = geom.frame_border_size;
    style.TabBorderSize = geom.tab_border_size;
}

} // namespace gs3d::ui
