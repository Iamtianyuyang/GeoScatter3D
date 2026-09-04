#pragma once

#include "app/AppState.hpp"

namespace gs3d::ui {

// 嵌入式导航图内容，不创建独立窗口。
void draw_navigation_map_content(
    gs3d::app::AppState& state,
    gs3d::app::NavigationMapState* navigation_map = nullptr
);

void draw_navigation_map(
    gs3d::app::AppState& state,
    const char* window_name = nullptr,
    bool* open = nullptr,
    gs3d::app::NavigationMapState* navigation_map = nullptr
);

} // namespace gs3d::ui
