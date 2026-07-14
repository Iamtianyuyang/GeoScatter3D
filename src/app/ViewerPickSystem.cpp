#include "app/ViewerPickSystem.hpp"

#include "app/AppState.hpp"
#include "app/UiActions.hpp"
#include "app/ViewerBenchmarkController.hpp"
#include "camera/BoxSelect.hpp"
#include "camera/CameraController.hpp"
#include "render/PointPipeline.hpp"
#include "render/VulkanRenderer.hpp"
#include "render/ViewportManager.hpp"
#include "util/Log.hpp"
#include "util/Stopwatch.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace gs3d::app {

namespace {

float point_size_for_viewport(
    const std::vector<float>& viewport_point_sizes,
    int viewport_index
) noexcept {
    if (viewport_index >= 0 &&
        viewport_index < static_cast<int>(viewport_point_sizes.size())) {
        return viewport_point_sizes[static_cast<std::size_t>(viewport_index)];
    }
    return 1.0f;
}

} // namespace

[[nodiscard]]
std::uint32_t compute_hover_pick_radius_px(float point_size) noexcept {
    const float clamped_point_size = std::clamp(point_size, 1.0f, 10.0f);
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

ResolvedPickPoint resolve_pick_point(
    const GpuPickResult& result,
    const std::vector<gs3d::data::Gs3dPoint>& points_by_id,
    const std::vector<std::uint8_t>& valid_by_id
) {
    ResolvedPickPoint out;
    if (result.has_hit && result.point_id < points_by_id.size() &&
        valid_by_id[result.point_id] != 0) {
        out.point = points_by_id[result.point_id];
        out.via_runtime_lookup = true;
    }
    return out;
}

ViewerPickSystem::ViewerPickSystem(
    const gs3d::render::VulkanContext& context,
    std::uint32_t frames_in_flight,
    std::size_t viewport_count,
    bool debug_dump_enabled,
    std::filesystem::path debug_dump_dir
)
    : gpu_readback_(context, frames_in_flight, viewport_count),
      debug_frame_dumper_(context, frames_in_flight),
      debug_dump_enabled_(debug_dump_enabled),
      debug_dump_dir_(std::move(debug_dump_dir)),
      pending_hover_miss_dump_(viewport_count, false)
{
    state_.latest_hover_points.resize(viewport_count);
    state_.latest_capture_x.resize(viewport_count, 0.0f);
    state_.latest_capture_y.resize(viewport_count, 0.0f);
    state_.hover_timeout.resize(viewport_count, 0);
    state_.consecutive_no_hit.resize(viewport_count, 0);
    state_.requests.resize(viewport_count);
}

ViewerPickState& ViewerPickSystem::state() noexcept { return state_; }
const ViewerPickState& ViewerPickSystem::state() const noexcept { return state_; }
GpuPickReadback& ViewerPickSystem::gpu_readback() noexcept { return gpu_readback_; }
PickDebugFrameDumper& ViewerPickSystem::debug_frame_dumper() noexcept {
    return debug_frame_dumper_;
}
std::vector<bool>& ViewerPickSystem::pending_hover_miss_dump() noexcept {
    return pending_hover_miss_dump_;
}
std::uint64_t& ViewerPickSystem::debug_dump_count() noexcept {
    return debug_dump_count_;
}
bool& ViewerPickSystem::debug_dump_completed() noexcept {
    return debug_dump_completed_;
}

void ViewerPickSystem::prepare_requests(
    const gs3d::render::ViewportManager& viewport_manager,
    AppState& app_state,
    const UiActions& actions,
    const std::vector<float>& viewport_point_sizes,
    ViewerBenchmarkController& benchmark_controller
) {
    for (auto& request : state_.requests) {
        request = {};
    }
    for (int index = 0; index < viewport_manager.active_count(); ++index) {
        if (!app_state.render_views[static_cast<std::size_t>(index)]
                 .render_requested) {
            state_.latest_hover_points[static_cast<std::size_t>(index)].reset();
        }
    }
    for (const auto& frame : actions.viewport_frames) {
        if (frame.index < 0 ||
            frame.index >= static_cast<int>(state_.requests.size())) {
            continue;
        }

        auto& request = state_.requests[static_cast<std::size_t>(frame.index)];
        request.viewport_index = frame.index;
        request.viewport_width = frame.width;
        request.viewport_height = frame.height;

        if (frame.box_select_completed && frame.mouse_on_image) {
            request.kind = GpuPickRequestKind::BoxSelectAnchor;
            request.mouse_x =
                0.5f * (frame.box_select_min_x + frame.box_select_max_x);
            request.mouse_y =
                0.5f * (frame.box_select_min_y + frame.box_select_max_y);
            request.pick_radius_px = compute_hover_pick_radius_px(
                point_size_for_viewport(viewport_point_sizes, frame.index)
            );
            request.box_select_min_x = frame.box_select_min_x;
            request.box_select_min_y = frame.box_select_min_y;
            request.box_select_max_x = frame.box_select_max_x;
            request.box_select_max_y = frame.box_select_max_y;
            request.anchor_camera = viewport_manager.camera(frame.index);
            continue;
        }

        if (frame.point_double_clicked && frame.mouse_on_image) {
            request.kind = GpuPickRequestKind::SetOrbitPivot;
            request.mouse_x = frame.mouse_local_x;
            request.mouse_y = frame.mouse_local_y;
            request.pick_radius_px = compute_hover_pick_radius_px(
                point_size_for_viewport(viewport_point_sizes, frame.index)
            );
            continue;
        }

        auto& measurement = measurement_for_view(app_state, frame.index);
        if (frame.measure_pick_requested && measurement.measure_mode_active()) {
            const auto index = static_cast<std::size_t>(frame.index);
            if (index < state_.latest_hover_points.size() &&
                state_.latest_hover_points[index].has_value()) {
                measurement.add_point(*state_.latest_hover_points[index]);
            }
        }

        const auto index = static_cast<std::size_t>(frame.index);
        if (index < state_.hover_timeout.size()) {
            if (frame.active || !frame.mouse_on_image) {
                ++state_.hover_timeout[index];
            } else {
                state_.hover_timeout[index] = 0;
            }
        }
        if (frame.active || !frame.mouse_on_image) {
            continue;
        }

        request.kind = GpuPickRequestKind::Hover;
        request.pick_radius_px = compute_hover_pick_radius_px(
            point_size_for_viewport(viewport_point_sizes, frame.index)
        );
        if (const auto query_index =
                benchmark_controller.take_active_query_for_viewport(
                    frame.index
                )) {
            request.benchmark_query_index = static_cast<int>(*query_index);
        }
        request.mouse_x = frame.mouse_local_x;
        request.mouse_y = frame.mouse_local_y;
    }
}

void ViewerPickSystem::consume_ready_frame(
    std::uint32_t frame_slot,
    const ViewerPickLookupContext& lookup,
    ViewerPickCameraContext& camera,
    ViewerBenchmarkController& benchmark_controller,
    const VisibleTilePickResolver& resolve_hover_point_from_visible_tiles
) {
    state_.frame_slot = frame_slot;
    if (debug_dump_enabled_) {
        const auto debug_dump = debug_frame_dumper_.collect_ready_frame(frame_slot);
        if (debug_dump) {
            write_pick_debug_dump(debug_dump_dir_, *debug_dump);
        }
    }

    gs3d::util::Stopwatch collect_timer;
    const auto pick_results = gpu_readback_.collect_ready_frame(frame_slot);
    const double collect_cpu_ms = collect_timer.elapsed_milliseconds();
    const auto& issue_cpu_ms = benchmark_controller.issue_cpu_ms();
    const auto& issue_metadata = benchmark_controller.issue_metadata();
    auto& results = benchmark_controller.results();
    for (const auto& result : pick_results) {
        if (result.request.viewport_index < 0 ||
            result.request.viewport_index >=
                static_cast<int>(state_.latest_hover_points.size())) {
            continue;
        }

        const auto resolved = resolve_pick_point(
            result,
            lookup.runtime_points_by_id,
            lookup.runtime_points_valid_by_id
        );
        std::optional<gs3d::data::Gs3dPoint> hit_point = resolved.point;

        const auto view_index =
            static_cast<std::size_t>(result.request.viewport_index);
        if ((result.request.kind == GpuPickRequestKind::Hover ||
             result.request.kind == GpuPickRequestKind::SetOrbitPivot) &&
            result.has_hit && !resolved.via_runtime_lookup) {
            if (const auto fallback = resolve_hover_point_from_visible_tiles(
                    view_index,
                    result.point_id,
                    result.request.mouse_x,
                    result.request.mouse_y
                )) {
                hit_point = *fallback;
            }
        }
        if (result.request.kind == GpuPickRequestKind::Hover) {
            if (hit_point.has_value()) {
                state_.latest_hover_points[view_index] = hit_point;
                state_.hover_timeout[view_index] = 0;
                state_.consecutive_no_hit[view_index] = 0;
            }
            if (!result.has_hit &&
                view_index < state_.consecutive_no_hit.size()) {
                ++state_.consecutive_no_hit[view_index];
                if (state_.consecutive_no_hit[view_index] >=
                        kNoHitClearThreshold &&
                    view_index < state_.latest_hover_points.size()) {
                    state_.latest_hover_points[view_index].reset();
                }
            }
            if (result.has_hit &&
                view_index < state_.consecutive_no_hit.size()) {
                state_.consecutive_no_hit[view_index] = 0;
            }
            state_.latest_capture_x[view_index] = result.request.mouse_x;
            state_.latest_capture_y[view_index] = result.request.mouse_y;
            if (benchmark_controller.pick_enabled() &&
                result.request.benchmark_query_index >= 0) {
                const auto query_index = static_cast<std::size_t>(
                    result.request.benchmark_query_index
                );
                BenchmarkPickObservedResult observed;
                observed.query_index = query_index;
                observed.has_hit = hit_point.has_value();
                observed.gpu_has_hit = result.has_hit;
                observed.point_id = result.point_id;
                observed.depth = result.depth;
                observed.issue_cpu_ms =
                    query_index < issue_cpu_ms.size()
                        ? issue_cpu_ms[query_index]
                        : 0.0;
                observed.collect_cpu_ms = collect_cpu_ms;
                if (query_index < issue_metadata.size()) {
                    observed.all_tiles_resident =
                        issue_metadata[query_index].all_tiles_resident;
                    observed.resident_tile_ids =
                        issue_metadata[query_index].resident_tile_ids;
                }
                if (hit_point) {
                    observed.x = hit_point->x;
                    observed.y = hit_point->y;
                    observed.z = hit_point->z;
                    observed.value = hit_point->value;
                }
                results.push_back(observed);
            }
            continue;
        }

        if (result.request.kind == GpuPickRequestKind::SetOrbitPivot) {
            if (!hit_point.has_value()) {
                camera.controllers[view_index].clear_orbit_pivot();
                camera.selected_focus_points[view_index].reset();
                gs3d::util::log::info()
                    << "[CAMERA] orbit pivot cleared"
                    << " (double-clicked empty space)\n";
                continue;
            }

            const auto& push =
                view_index < camera.viewport_pushes.size()
                    ? camera.viewport_pushes[view_index]
                    : camera.viewport_pushes.front();
            float raw = hit_point->z;
            if (push.height_source == static_cast<std::uint32_t>(
                    gs3d::app::AttrPhysicalSource::Value
                )) {
                raw = hit_point->value;
            }
            const float mapped_z = push.height_offset + raw * push.height_mult;
            const gs3d::camera::Vec3 selected_point{
                hit_point->x,
                hit_point->y,
                mapped_z
            };
            camera.controllers[view_index].set_orbit_pivot(selected_point);
            camera.selected_focus_points[view_index] = selected_point;
            camera.streaming_viewport_index = result.request.viewport_index;
            gs3d::util::log::info()
                << "[CAMERA] orbit pivot selected at ["
                << selected_point.x << ", "
                << selected_point.y << ", "
                << selected_point.z << "]\n";
            continue;
        }

        if (result.request.kind != GpuPickRequestKind::BoxSelectAnchor) {
            continue;
        }

        const gs3d::camera::Viewport mouse_viewport{
            result.request.anchor_camera.viewport_width(),
            result.request.anchor_camera.viewport_height()
        };
        const float plane_z =
            hit_point
                ? hit_point->z
                : result.request.anchor_camera.target().z;
        const auto selection_bounds = gs3d::camera::box_select_world_bounds(
            result.request.box_select_min_x,
            result.request.box_select_min_y,
            result.request.box_select_max_x,
            result.request.box_select_max_y,
            mouse_viewport,
            result.request.anchor_camera,
            camera.bounds,
            plane_z
        );
        if (!selection_bounds) {
            continue;
        }

        auto& box_camera =
            camera.viewport_manager.camera(result.request.viewport_index);
        const auto saved_position = box_camera.position();
        const auto saved_target = box_camera.target();
        const float saved_ortho_height = box_camera.ortho_height();

        box_camera.fit_screen_rect(
            result.request.box_select_min_x,
            result.request.box_select_min_y,
            result.request.box_select_max_x,
            result.request.box_select_max_y,
            mouse_viewport.width,
            mouse_viewport.height,
            1.05f
        );

        const auto desired_target = box_camera.target();
        const float desired_ortho_height = box_camera.ortho_height();
        box_camera.look_at(saved_position, saved_target, box_camera.up());
        box_camera.set_orthographic(
            saved_ortho_height,
            box_camera.near_plane(),
            box_camera.far_plane()
        );

        camera.controllers[static_cast<std::size_t>(
            result.request.viewport_index
        )].animate_to(
            box_camera,
            desired_target,
            desired_ortho_height
        );
        camera.streaming_viewport_index = result.request.viewport_index;
        camera.tile_selection_dirty = true;
    }
}

void ViewerPickSystem::consume_ready_frame_slot(
    const std::uint32_t frame_slot,
    ViewerPickFrameContext& context
) {
    const ViewerPickLookupContext lookup{
        context.runtime_points_by_id,
        context.runtime_points_valid_by_id
    };
    ViewerPickCameraContext camera{
        context.controllers,
        context.selected_focus_points,
        context.viewport_manager,
        context.bounds,
        context.viewport_pushes,
        context.streaming_viewport_index,
        context.tile_selection_dirty
    };
    consume_ready_frame(
        frame_slot,
        lookup,
        camera,
        context.benchmark_controller,
        context.resolve_hover_point_from_visible_tiles
    );
}

void ViewerPickSystem::consume_ready_frames(
    const gs3d::render::VulkanRenderer& renderer,
    ViewerPickFrameContext& context
) {
    for (std::uint32_t frame_slot = 0;
         frame_slot < renderer.frames_in_flight();
         ++frame_slot) {
        if (renderer.is_frame_slot_ready(frame_slot)) {
            consume_ready_frame_slot(frame_slot, context);
        }
    }
}

} // namespace gs3d::app
