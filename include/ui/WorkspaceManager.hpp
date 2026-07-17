#pragma once

#include "app/AppState.hpp"

#include <cstdint>
#include <vector>

namespace gs3d::ui {

[[nodiscard]] bool workspace_contains_view(
    const gs3d::app::WorkspaceWindowState& workspace,
    int viewport_index
);

[[nodiscard]] bool view_is_owned_by_workspace(
    const gs3d::app::AppState& state,
    int viewport_index
);

[[nodiscard]] int active_view_for_indices(
    const gs3d::app::AppState& state,
    const std::vector<int>& indices,
    int fallback
);

[[nodiscard]] std::vector<int> main_workspace_viewports(
    const gs3d::app::AppState& state
);

[[nodiscard]] std::uint32_t visible_view_signature(
    const gs3d::app::AppState& state
);

[[nodiscard]] bool has_hidden_view(const gs3d::app::AppState& state);

int show_first_hidden_view(
    gs3d::app::AppState& state,
    bool force_undock = false
);

bool add_view_to_workspace(
    gs3d::app::AppState& state,
    gs3d::app::WorkspaceWindowState& workspace
);

bool create_workspace_window(gs3d::app::AppState& state);

bool pop_out_view_window(
    gs3d::app::AppState& state,
    int viewport_index
);

void restore_default_workspace(gs3d::app::AppState& state);

void prune_workspace_windows(gs3d::app::AppState& state);

} // namespace gs3d::ui
