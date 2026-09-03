#pragma once

#include "app/AppState.hpp"
#include "app/UiActions.hpp"
#include "ui/Theme.hpp"

namespace gs3d::ui {

struct AppChromeResult {
    bool theme_change_requested = false;
    ThemeId requested_theme = ThemeId::kCarbonBlue;
    bool layout_change_requested = false;
    std::string requested_layout_id;
    bool restore_default_workspace_requested = false;
};

void handle_chrome_shortcuts(gs3d::app::AppState& state, gs3d::app::UiActions& actions);
void draw_top_bar(gs3d::app::AppState& state, gs3d::app::UiActions& actions, float ui_scale, AppChromeResult& result);
void draw_status_bar(const gs3d::app::AppState& state, float ui_scale);
void draw_shortcut_overlay(gs3d::app::AppState& state, float ui_scale);
void draw_panel_command_palette(gs3d::app::AppState& state, float ui_scale);
void persist_ui_preferences(const gs3d::app::AppState& state, ThemeId theme);

} // namespace gs3d::ui
