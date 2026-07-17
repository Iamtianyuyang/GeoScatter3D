#pragma once

#include "app/AppState.hpp"
#include "app/UiActions.hpp"

namespace gs3d::ui {

struct ViewportInputRouting {
    bool hovered = false;
    bool active = false;
};

[[nodiscard]]
inline ViewportInputRouting resolve_viewport_input_routing(
    const bool item_hovered,
    const bool item_active,
    const bool platform_window_focused
) noexcept {
    return {
        .hovered = platform_window_focused && item_hovered,
        .active = platform_window_focused && item_active,
    };
}

struct ViewportCanvasOptions {
    int workspace_id = 0;
    // 左上角「N 点 | x ms」信息 badge。悬浮 Dock 布局用独立的性能
    // HUD chip 替代它，避免与文件状态 chip 重叠。
    bool show_info_badge = true;
};

/*
 * 视口画布主体：InvisibleButton 交互层 + 点云图像 + 地图轴/十字准线/
 * 测量线 overlay，并向 actions.viewport_frames 追加本帧相机与拾取命令。
 *
 * 前置条件：调用时处于一个已 Begin 的 ImGui 窗口内，画布占满剩余
 * 内容区域。工作台布局的视图窗口和悬浮 Dock 布局的全屏沉浸视口共用
 * 这一份实现（定义在 UiRoot.cpp，与坐标轴绘制助手同翻译单元）。
 */
void draw_viewport_canvas(
    gs3d::app::RenderViewState& view,
    gs3d::app::UiActions& actions,
    const ViewportCanvasOptions& options = {}
);

} // namespace gs3d::ui
