#pragma once

#include "app/AppState.hpp"
#include "app/UiActions.hpp"

#include <vector>

namespace gs3d::ui {

// 嵌入式渲染设置内容，由具体布局负责窗口或子区域的几何。
void draw_render_settings_content(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    gs3d::app::RenderSettingsState* render_settings = nullptr,
    const std::vector<int>* target_viewports = nullptr
);

void draw_render_settings(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    const char* window_name = nullptr,
    bool* open = nullptr,
    gs3d::app::RenderSettingsState* render_settings = nullptr,
    const std::vector<int>* target_viewports = nullptr
);

} // namespace gs3d::ui
