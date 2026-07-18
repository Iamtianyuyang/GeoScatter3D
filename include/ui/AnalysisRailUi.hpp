#pragma once

#include "app/AppState.hpp"
#include "app/UiActions.hpp"
#include "ui/Theme.hpp"

namespace gs3d::ui {

struct AnalysisRailFrameResult {
    bool theme_change_requested = false;
    ThemeId requested_theme = ThemeId::kDeepGraphite;
};

// 方案 C「暗色分析舱」整帧入口。固定结构为 54px 图标轨、272px
// 互斥抽屉、50px 工作流顶栏、252px 右侧分析卡片和 24px 状态栏。
[[nodiscard]]
AnalysisRailFrameResult draw_analysis_rail_layout(
    gs3d::app::AppState& state,
    gs3d::app::UiActions& actions,
    float ui_scale
);

} // namespace gs3d::ui
