#include "ui/layouts/LayoutMetrics.hpp"

namespace gs3d::ui {

const LayoutGeometry& default_layout_geometry() noexcept {
    static const LayoutGeometry kDefault;
    return kDefault;
}

void apply_layout_geometry(const LayoutGeometry& /*geom*/, float /*ui_scale*/) {
}

} // namespace gs3d::ui
