#include "app/ViewportPresentationState.hpp"

#include "ui/WorkspaceManager.hpp"

#include <algorithm>

namespace gs3d::app {

ViewportPresentationState::ViewportPresentationState(
    const std::size_t viewport_count,
    const gs3d::render::PointPushConstants& initial_push,
    const gs3d::scene::SceneState& initial_scene,
    const float initial_height_exaggeration
)
    : pushes_(viewport_count, initial_push)
    , scenes_(viewport_count, initial_scene)
    , height_exaggerations_(viewport_count, initial_height_exaggeration)
    , previous_visible_(viewport_count, false)
{
}

bool ViewportPresentationState::contains(const int viewport_index) const noexcept
{
    return viewport_index >= 0 &&
           static_cast<std::size_t>(viewport_index) < pushes_.size();
}

std::size_t ViewportPresentationState::viewport_count() const noexcept
{
    return pushes_.size();
}

std::vector<gs3d::render::PointPushConstants>&
ViewportPresentationState::pushes() noexcept
{
    return pushes_;
}

std::vector<gs3d::scene::SceneState>&
ViewportPresentationState::scenes() noexcept
{
    return scenes_;
}

std::vector<float>& ViewportPresentationState::height_exaggerations() noexcept
{
    return height_exaggerations_;
}

void ViewportPresentationState::initialize_visibility(const AppState& app_state)
{
    std::fill(previous_visible_.begin(), previous_visible_.end(), false);
    for (const auto& view : app_state.render_views) {
        if (contains(view.viewport_index)) {
            previous_visible_[static_cast<std::size_t>(view.viewport_index)] =
                view.visible;
        }
    }
}

int ViewportPresentationState::first_visible_main_view(
    const AppState& app_state
) const noexcept
{
    for (const auto& view : app_state.render_views) {
        if (view.visible &&
            !gs3d::ui::view_is_owned_by_workspace(
                app_state,
                view.viewport_index
            )) {
            return view.viewport_index;
        }
    }
    return 0;
}

void ViewportPresentationState::copy_newly_visible_views(
    AppState& app_state,
    const int default_source_view
) {
    for (const auto& view : app_state.render_views) {
        if (!contains(view.viewport_index) || !view.visible ||
            previous_visible_[static_cast<std::size_t>(view.viewport_index)]) {
            continue;
        }

        int source_view = default_source_view;
        for (const auto& workspace : app_state.workspace_windows) {
            if (!gs3d::ui::workspace_contains_view(
                    workspace,
                    view.viewport_index
                )) {
                continue;
            }
            for (const int workspace_view : workspace.viewport_indices) {
                if (workspace_view != view.viewport_index &&
                    contains(workspace_view) &&
                    previous_visible_[static_cast<std::size_t>(workspace_view)]) {
                    source_view = workspace_view;
                    break;
                }
            }
            break;
        }

        if (!contains(source_view)) {
            continue;
        }
        const auto source = static_cast<std::size_t>(source_view);
        const auto destination = static_cast<std::size_t>(view.viewport_index);
        pushes_[destination] = pushes_[source];
        scenes_[destination] = scenes_[source];
        height_exaggerations_[destination] = height_exaggerations_[source];
        if (destination < app_state.render_settings_by_view.size() &&
            source < app_state.render_settings_by_view.size()) {
            app_state.render_settings_by_view[destination] =
                app_state.render_settings_by_view[source];
        }
        if (destination < app_state.navigation_maps.size() &&
            source < app_state.navigation_maps.size()) {
            app_state.navigation_maps[destination].dirty = true;
            app_state.navigation_maps[destination].view_rect_valid = false;
        }
        if (destination < app_state.measurements.size() &&
            source < app_state.measurements.size()) {
            app_state.measurements[destination] = app_state.measurements[source];
        }
        if (destination < app_state.region_stats_by_view.size() &&
            source < app_state.region_stats_by_view.size()) {
            app_state.region_stats_by_view[destination] =
                app_state.region_stats_by_view[source];
        }
    }

    initialize_visibility(app_state);
}

ViewportPresentationRuntimeState
ViewportPresentationState::reconcile_runtime_viewports(
    const AppState& app_state,
    const int viewport_capacity,
    const int streaming_viewport_index,
    std::vector<int>& visible_viewports
) const {
    visible_viewports.clear();

    int active_viewport_count = 1;
    int first_visible_viewport_index = 0;
    bool has_visible_viewport = false;
    for (const auto& view : app_state.render_views) {
        if (view.render_requested) {
            visible_viewports.push_back(view.viewport_index);
        }
        if (!view.visible) {
            continue;
        }
        active_viewport_count = std::max(
            active_viewport_count,
            view.viewport_index + 1
        );
        if (!has_visible_viewport) {
            first_visible_viewport_index = view.viewport_index;
            has_visible_viewport = true;
        }
    }

    const int safe_viewport_capacity = std::max(1, viewport_capacity);
    active_viewport_count = std::clamp(
        active_viewport_count,
        1,
        safe_viewport_capacity
    );
    const bool streaming_viewport_is_visible =
        streaming_viewport_index >= 0 &&
        streaming_viewport_index <
            static_cast<int>(app_state.render_views.size()) &&
        app_state.render_views[
            static_cast<std::size_t>(streaming_viewport_index)
        ].visible;

    return {
        .active_viewport_count = active_viewport_count,
        .streaming_viewport_index = streaming_viewport_is_visible
            ? streaming_viewport_index
            : (has_visible_viewport ? first_visible_viewport_index : 0),
        .streaming_viewport_changed = !streaming_viewport_is_visible
    };
}

} // namespace gs3d::app
