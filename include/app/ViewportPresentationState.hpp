#pragma once

#include "app/AppState.hpp"
#include "render/PointPipeline.hpp"
#include "scene/SceneState.hpp"

#include <cstddef>
#include <vector>

namespace gs3d::app {

struct ViewportPresentationRuntimeState {
    int active_viewport_count = 1;
    int streaming_viewport_index = 0;
    bool streaming_viewport_changed = false;
};

/*
 * Owns render-facing state that must stay aligned for every allocated
 * viewport.  The AppState mirrors UI-facing pieces of this state; when a
 * hidden view becomes visible, this class copies a coherent source view once
 * instead of letting parallel arrays drift independently in ViewerApp::run.
 */
class ViewportPresentationState {
public:
    ViewportPresentationState(
        std::size_t viewport_count,
        const gs3d::render::PointPushConstants& initial_push,
        const gs3d::scene::SceneState& initial_scene,
        float initial_height_exaggeration
    );

    [[nodiscard]] bool contains(int viewport_index) const noexcept;
    [[nodiscard]] std::size_t viewport_count() const noexcept;

    std::vector<gs3d::render::PointPushConstants>& pushes() noexcept;
    std::vector<gs3d::scene::SceneState>& scenes() noexcept;
    std::vector<float>& height_exaggerations() noexcept;

    void initialize_visibility(const AppState& app_state);
    [[nodiscard]] int first_visible_main_view(
        const AppState& app_state
    ) const noexcept;
    void copy_newly_visible_views(
        AppState& app_state,
        int default_source_view
    );
    [[nodiscard]] ViewportPresentationRuntimeState
    reconcile_runtime_viewports(
        const AppState& app_state,
        int viewport_capacity,
        int streaming_viewport_index,
        std::vector<int>& visible_viewports
    ) const;

private:
    std::vector<gs3d::render::PointPushConstants> pushes_;
    std::vector<gs3d::scene::SceneState> scenes_;
    std::vector<float> height_exaggerations_;
    std::vector<bool> previous_visible_;
};

} // namespace gs3d::app
