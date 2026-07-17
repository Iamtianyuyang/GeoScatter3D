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

} // namespace gs3d::ui
