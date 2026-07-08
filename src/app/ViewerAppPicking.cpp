#include "app/ViewerApp.hpp"
#include "app/ViewerAppRunState.hpp"

#include "app/AppState.hpp"
#include "app/UiActions.hpp"
#include "render/ViewportManager.hpp"

#include <algorithm>
#include <cmath>

namespace gs3d::app {

[[nodiscard]]
std::uint32_t compute_hover_pick_radius_px(float point_size) noexcept {
    const float clamped_point_size =
        std::clamp(point_size, 1.0f, 10.0f);
    const int sprite_half_extent =
        static_cast<int>(std::ceil(clamped_point_size * 0.5f));
    return static_cast<std::uint32_t>(
        std::clamp(
            sprite_half_extent + 1,
            static_cast<int>(kDefaultGpuPickRadiusPx),
            static_cast<int>(kMaxGpuPickRadiusPx)
        )
    );
}

void ViewerApp::prepare_gpu_pick_requests(
    ViewerAppPickState& pick,
    const gs3d::render::ViewportManager& viewport_manager,
    gs3d::app::AppState& app_state,
    const gs3d::app::UiActions& gui_cmds,
    float point_size,
    std::size_t& benchmark_pick_issue_index,
    const std::vector<BenchmarkPickScriptQuery>& benchmark_pick_queries
) {
    const bool benchmark_pick_enabled =
        config_.benchmark_mode &&
        !config_.benchmark_pick_script_path.empty();

    for (auto& request : pick.requests) {
        request = {};
    }
    // Clear hover data only for views that did not render this
    // frame — not based on ImGui hover state (which can be wrong
    // when ghost viewports consume the hover hit-test).
    for (int i = 0; i < viewport_manager.active_count(); ++i) {
        if (!app_state.render_views[static_cast<std::size_t>(i)]
                 .render_requested) {
            pick.latest_hover_points[static_cast<std::size_t>(i)]
                .reset();
        }
    }
    for (const auto& frame : gui_cmds.viewport_frames) {
        if (frame.index < 0 ||
            frame.index >= static_cast<int>(pick.requests.size())) {
            continue;
        }

        auto& request =
            pick.requests[static_cast<std::size_t>(frame.index)];
        request.viewport_index = frame.index;
        request.viewport_width = frame.width;
        request.viewport_height = frame.height;

        if (frame.box_select_completed && frame.mouse_on_image) {
            request.kind = GpuPickRequestKind::BoxSelectAnchor;
            request.mouse_x =
                0.5f * (frame.box_select_min_x + frame.box_select_max_x);
            request.mouse_y =
                0.5f * (frame.box_select_min_y + frame.box_select_max_y);
            request.pick_radius_px =
                compute_hover_pick_radius_px(point_size);
            request.box_select_min_x = frame.box_select_min_x;
            request.box_select_min_y = frame.box_select_min_y;
            request.box_select_max_x = frame.box_select_max_x;
            request.box_select_max_y = frame.box_select_max_y;
            request.anchor_camera =
                viewport_manager.camera(frame.index);
            continue;
        }

        if (frame.point_double_clicked &&
            frame.mouse_on_image) {
            request.kind = GpuPickRequestKind::SetOrbitPivot;
            request.mouse_x = frame.mouse_local_x;
            request.mouse_y = frame.mouse_local_y;
            request.pick_radius_px =
                compute_hover_pick_radius_px(point_size);
            continue;
        }

        // Measurement pick: middle-click uses the latest hover
        // pick result (zero-latency, same strategy as orbit pivot).
        if (frame.measure_pick_requested &&
            app_state.measurement.measure_mode_active()) {
            const auto idx =
                static_cast<std::size_t>(frame.index);
            if (idx < pick.latest_hover_points.size() &&
                pick.latest_hover_points[idx].has_value()) {
                app_state.measurement.add_point(
                    *pick.latest_hover_points[idx]);
            }
        }

        // Issue hover pick whenever the cursor is on the image,
        // regardless of ImGui hover state.  Ghost viewports can
        // consume the ImGui hit-test even though the cursor is
        // visually over the rendered data; the pick result's
        // has_hit is the ground truth for "cursor on data".
        // Reset hover timeout when a pick can be issued (cursor
        // on image, not dragging).  Increment when we skip.
        {
            const auto idx =
                static_cast<std::size_t>(frame.index);
            if (idx < pick.hover_timeout.size()) {
                if (frame.active || !frame.mouse_on_image) {
                    ++pick.hover_timeout[idx];
                } else {
                    pick.hover_timeout[idx] = 0;
                }
            }
        }

        if (frame.active || !frame.mouse_on_image) {
            continue;
        }

        request.kind = GpuPickRequestKind::Hover;
        request.pick_radius_px =
            compute_hover_pick_radius_px(point_size);
        if (benchmark_pick_enabled &&
            frame.index == 0 &&
            benchmark_pick_issue_index < benchmark_pick_queries.size()) {
            request.benchmark_query_index = static_cast<int>(
                benchmark_pick_queries[benchmark_pick_issue_index]
                    .query_index
            );
            ++benchmark_pick_issue_index;
        }
        request.mouse_x = frame.mouse_local_x;
        request.mouse_y = frame.mouse_local_y;
    }
}

} // namespace gs3d::app
