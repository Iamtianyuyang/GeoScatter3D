#pragma once

#include "app/AppState.hpp"
#include "app/UiActions.hpp"

namespace gs3d::ui {

/*
 * 方案 B · 悬浮胶囊 Dock 布局 (Floating Dock)
 * 全屏沉浸视口 + 底部居中悬浮胶囊工具栏 + 浮动卡片面板
 */
void draw_floating_dock_overlay(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    float ui_scale
);

} // namespace gs3d::ui
