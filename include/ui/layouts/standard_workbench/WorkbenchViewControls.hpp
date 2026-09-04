#pragma once

#include "app/AppState.hpp"
#include "app/UiActions.hpp"

namespace gs3d::ui {

// =========================================================================================
// 方案 A · 视口顶部快捷控制条 (View Controls Toolbar)
// =========================================================================================
// 位于每个 3D 点云渲染视口正上方，负责渲染：
// 1. [复位视角]：重置当前视口相机至全景鸟瞰位 (快捷键 R)
// 2. [地图轴]：开关底部/左侧刻度标尺与地理坐标标注
// 3. [十字线]：开关视口中心十字基准线
// 4. [更多选项]：弹出菜单（相机联动、世界坐标轴、拾取准星样式、新建视图等）
void draw_workbench_view_controls(
    gs3d::app::RenderViewState& view,
    gs3d::app::UiActions& actions,
    float ui_scale
);

void draw_workbench_view_controls(
    gs3d::app::AppState& state,
    gs3d::app::RenderViewState& view,
    gs3d::app::UiActions& actions,
    float ui_scale
);

} // namespace gs3d::ui
