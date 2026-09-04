#pragma once

#include "app/AppState.hpp"

namespace gs3d::ui {

// 嵌入式内容：由布局模块放入任意已 Begin 的 ImGui 区域。
void draw_dataset_panel_content(
    gs3d::app::AppState& state,
    gs3d::app::DatasetSummaryState* dataset = nullptr
);

void draw_dataset_panel(
    gs3d::app::AppState& state,
    const char* window_name = nullptr,
    bool* open = nullptr,
    gs3d::app::DatasetSummaryState* dataset = nullptr
);

} // namespace gs3d::ui
