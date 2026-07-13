#include "ui/WorkspaceManager.hpp"

#include <algorithm>

namespace gs3d::ui {

bool workspace_contains_view(
    const gs3d::app::WorkspaceWindowState& workspace,
    const int viewport_index
) {
    return std::find(
        workspace.viewport_indices.begin(),
        workspace.viewport_indices.end(),
        viewport_index
    ) != workspace.viewport_indices.end();
}

bool view_is_owned_by_workspace(
    const gs3d::app::AppState& state,
    const int viewport_index
) {
    return std::any_of(
        state.workspace_windows.begin(),
        state.workspace_windows.end(),
        [viewport_index](const auto& workspace) {
            return workspace.visible &&
                   workspace_contains_view(workspace, viewport_index);
        }
    );
}

int active_view_for_indices(
    const gs3d::app::AppState& state,
    const std::vector<int>& indices,
    const int fallback
) {
    if (std::find(
            indices.begin(),
            indices.end(),
            state.active_viewport_index
        ) != indices.end()) {
        return state.active_viewport_index;
    }
    return indices.empty() ? fallback : indices.front();
}

std::vector<int> main_workspace_viewports(
    const gs3d::app::AppState& state
) {
    std::vector<int> indices;
    for (const auto& view : state.render_views) {
        if (view.visible &&
            !view_is_owned_by_workspace(state, view.viewport_index)) {
            indices.push_back(view.viewport_index);
        }
    }
    return indices;
}

std::uint32_t visible_view_signature(const gs3d::app::AppState& state)
{
    std::uint32_t signature = 0;
    for (const auto& view : state.render_views) {
        if (view.visible &&
            !view_is_owned_by_workspace(state, view.viewport_index) &&
            view.viewport_index >= 0 && view.viewport_index < 24) {
            signature |= 1u << static_cast<std::uint32_t>(view.viewport_index);
        }
    }
    return signature;
}

bool has_hidden_view(const gs3d::app::AppState& state)
{
    return std::any_of(
        state.render_views.begin(),
        state.render_views.end(),
        [](const auto& view) { return !view.visible; }
    );
}

int show_first_hidden_view(
    gs3d::app::AppState& state,
    const bool force_undock
) {
    const auto hidden = std::find_if(
        state.render_views.begin(),
        state.render_views.end(),
        [](const auto& view) { return !view.visible; }
    );
    if (hidden == state.render_views.end()) {
        return -1;
    }
    hidden->visible = true;
    hidden->force_undock_next_frame = force_undock;
    hidden->render_requested = false;
    return hidden->viewport_index;
}

namespace {

int next_workspace_id(const gs3d::app::AppState& state)
{
    int next_id = 1;
    for (const auto& workspace : state.workspace_windows) {
        next_id = std::max(next_id, workspace.id + 1);
    }
    return next_id;
}

} // namespace

bool add_view_to_workspace(
    gs3d::app::AppState& state,
    gs3d::app::WorkspaceWindowState& workspace
) {
    const int view_index = show_first_hidden_view(state);
    if (view_index < 0) {
        return false;
    }
    auto& view = state.render_views[static_cast<std::size_t>(view_index)];
    view.detached = false;
    view.force_undock_next_frame = false;
    workspace.viewport_indices.push_back(view_index);
    workspace.dock_layout_initialized = false;
    return true;
}

bool create_workspace_window(gs3d::app::AppState& state)
{
    gs3d::app::WorkspaceWindowState workspace;
    workspace.id = next_workspace_id(state);
    workspace.components.dataset = state.dataset;
    workspace.components.render_settings = state.render_settings;
    workspace.components.navigation_map = state.navigation_map;
    workspace.components.navigation_map.view_rect_valid = false;
    if (!add_view_to_workspace(state, workspace)) {
        return false;
    }
    workspace.visible = true;
    workspace.dock_layout_initialized = false;
    state.workspace_windows.push_back(std::move(workspace));
    return true;
}

void prune_workspace_windows(gs3d::app::AppState& state)
{
    for (auto& workspace : state.workspace_windows) {
        if (!workspace.visible) {
            for (const int view_index : workspace.viewport_indices) {
                if (view_index >= 0 &&
                    view_index < static_cast<int>(state.render_views.size())) {
                    auto& view = state.render_views[
                        static_cast<std::size_t>(view_index)
                    ];
                    view.visible = false;
                    view.detached = false;
                    view.force_undock_next_frame = false;
                    view.render_requested = false;
                }
            }
            workspace.viewport_indices.clear();
            continue;
        }

        workspace.viewport_indices.erase(
            std::remove_if(
                workspace.viewport_indices.begin(),
                workspace.viewport_indices.end(),
                [&](const int view_index) {
                    return view_index < 0 ||
                           view_index >= static_cast<int>(state.render_views.size()) ||
                           !state.render_views[
                                static_cast<std::size_t>(view_index)
                            ].visible;
                }
            ),
            workspace.viewport_indices.end()
        );
        if (workspace.viewport_indices.empty()) {
            workspace.visible = false;
        }
    }

    state.workspace_windows.erase(
        std::remove_if(
            state.workspace_windows.begin(),
            state.workspace_windows.end(),
            [](const auto& workspace) {
                return !workspace.visible || workspace.viewport_indices.empty();
            }
        ),
        state.workspace_windows.end()
    );
}

} // namespace gs3d::ui
