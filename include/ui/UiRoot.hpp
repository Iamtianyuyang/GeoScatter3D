#pragma once

#include "app/AppState.hpp"
#include "app/UiActions.hpp"

namespace gs3d::ui {

class UiRoot {
public:
    UiRoot() = default;

    [[nodiscard]]
    gs3d::app::UiActions draw(gs3d::app::AppState& state);

private:
    void build_default_layout(const gs3d::app::AppState& state);

    bool dock_layout_initialized_ = false;
    std::uint32_t dock_layout_signature_ = 0;
};

} // namespace gs3d::ui
