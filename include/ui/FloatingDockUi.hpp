#pragma once

#include "app/AppState.hpp"
#include "app/UiActions.hpp"
#include "ui/Theme.hpp"

namespace gs3d::ui {

// draw_floating_dock_layout 的帧结果。主题切换必须延迟到本帧所有
// 临时样式出栈后再应用（同 UiRoot 菜单栏的处理），由调用方执行。
struct FloatingDockFrameResult {
    bool theme_change_requested = false;
    ThemeId requested_theme = ThemeId::kCarbonBlue;
};

/*
 * 方案 B「悬浮 Dock（Telegram 风）」布局的整帧绘制入口：
 *   - 全屏沉浸视口（活动视图铺满窗口，无菜单栏/停靠面板/状态栏）
 *   - 左上数据文件 chip + 性能 HUD chip，右上视角工具 pill
 *   - 右侧 FAB（截图 + 新建测量）
 *   - 底部居中胶囊 Dock（视图/测量/图层/属性/我的）
 *   - Dock 项点击弹出玻璃卡片，Esc / 点击视口收起
 *
 * 在 UiRoot::draw 中当 state.ui_layout_mode == kFloatingDock 时调用，
 * 替代工作台的 DockSpace 布局。
 */
[[nodiscard]]
FloatingDockFrameResult draw_floating_dock_layout(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    float ui_scale
);

// 主窗口顶部的非阻塞状态提示，用于截图选址、编码和保存结果。
void draw_screenshot_notice(
    gs3d::app::AppState& state,
    float ui_scale
);

/*
 * 瓦片全量预加载门禁：预加载进行中时绘制全屏加载页（数据集名 +
 * 进度条 + 明细）、禁用视口渲染并拦截交互，返回 true——调用方本帧
 * 不再绘制其余 UI。预加载完成或回退按需流式后返回 false，界面照常。
 */
[[nodiscard]]
bool draw_preload_gate_if_active(
    gs3d::app::AppState& state,
    float ui_scale
);

/*
 * 独立视图窗口复用沉浸布局右上角的视角工具 pill。位置以画布右上角
 * 为基准，并绑定到画布所在的 ImGui 平台窗口，避免成为额外的系统窗口。
 */
void draw_detached_view_camera_pill(
    gs3d::app::RenderViewState& view,
    gs3d::app::UiActions& actions,
    float canvas_top,
    float canvas_right,
    unsigned int platform_viewport_id,
    float ui_scale
);

} // namespace gs3d::ui
