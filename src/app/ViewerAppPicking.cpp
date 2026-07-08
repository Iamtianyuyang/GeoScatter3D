#include "app/ViewerApp.hpp"
#include "app/ViewerAppGpuPick.hpp"
#include "app/ViewerAppRunState.hpp"

#include "app/AppState.hpp"
#include "app/UiActions.hpp"
#include "camera/BoxSelect.hpp"
#include "camera/CameraController.hpp"
#include "camera/MouseRay.hpp"
#include "render/PointPipeline.hpp"
#include "render/ViewportManager.hpp"
#include "util/Stopwatch.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <utility>

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

ResolvedPickPoint resolve_pick_point(
    const GpuPickResult& result,
    const std::vector<gs3d::data::Gs3dPoint>& points_by_id,
    const std::vector<std::uint8_t>& valid_by_id
) {
    ResolvedPickPoint out;
    if (result.has_hit &&
        result.point_id < points_by_id.size() &&
        valid_by_id[result.point_id] != 0) {
        out.point = points_by_id[result.point_id];
        out.via_runtime_lookup = true;
    }
    return out;
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

void ViewerApp::consume_ready_pick_frame_slot(
    std::uint32_t frame_slot,
    ViewerAppPickState& pick,
    PickDebugFrameDumper& pick_debug_frame_dumper,
    GpuPickReadback& gpu_pick_readback,
    const ViewerAppPickLookupContext& pick_lookup,
    ViewerAppPickCameraContext& pick_camera,
    ViewerAppBenchmarkPickContext& pick_benchmark,
    const VisibleTilePickResolver& resolve_hover_point_from_visible_tiles
) {
                pick.frame_slot = frame_slot;
                if (config_.pick_debug_dump_enabled) {
                    const auto debug_dump =
                        pick_debug_frame_dumper.collect_ready_frame(frame_slot);
                    if (debug_dump) {
                        write_pick_debug_dump(
                            config_.pick_debug_dump_dir,
                            *debug_dump
                        );
                    }
                }

                gs3d::util::Stopwatch collect_timer;
                const auto pick_results =
                    gpu_pick_readback.collect_ready_frame(frame_slot);
                const double collect_cpu_ms =
                    collect_timer.elapsed_milliseconds();
                for (const auto& result : pick_results) {
                    if (result.request.viewport_index < 0 ||
                        result.request.viewport_index >=
                            static_cast<int>(
                                pick.latest_hover_points.size()
                            )) {
                        continue;
                    }

                    const auto resolved = resolve_pick_point(
                        result, pick_lookup.runtime_points_by_id, pick_lookup.runtime_points_valid_by_id);
                    std::optional<gs3d::data::Gs3dPoint> hit_point = resolved.point;
                    bool lookup_ok = resolved.via_runtime_lookup;

                    const auto view_index =
                        static_cast<std::size_t>(
                            result.request.viewport_index
                        );
                    const char* hover_resolve_path =
                        lookup_ok ? "runtime_lookup" : "miss";
                    if ((result.request.kind ==
                             GpuPickRequestKind::Hover ||
                         result.request.kind ==
                             GpuPickRequestKind::SetOrbitPivot) &&
                        result.has_hit &&
                        !lookup_ok) {
                        if (const auto resolved =
                                resolve_hover_point_from_visible_tiles(
                                    view_index,
                                    result.point_id,
                                    result.request.mouse_x,
                                    result.request.mouse_y
                                )) {
                            hit_point = *resolved;
                            lookup_ok = true;
                            hover_resolve_path = "resident_tile_fallback";
                        } else {
                            hover_resolve_path =
                                "resident_tile_fallback_miss";
                        }
                    }
                    if (result.request.kind ==
                        GpuPickRequestKind::Hover) {
                        // Only overwrite when we actually resolved a
                        // point.  A GPU hit whose runtime lookup failed
                        // must NOT null out valid data from a previous
                        // successful hit — otherwise the tooltip
                        // flickers or disappears when hovering over
                        // points whose IDs are not in the lookup table.
                        if (hit_point.has_value()) {
                            pick.latest_hover_points[view_index] = hit_point;
                            pick.hover_timeout[view_index] = 0;
                            pick.consecutive_no_hit[view_index] = 0;
                        }
                        // has_hit=false means GPU found no point at the
                        // cursor — the mouse is over empty space.
                        // Debounce across a few frames so that moving
                        // between nearby points (where the GPU pick lags
                        // behind the cursor) doesn't flicker the tooltip.
                        if (!result.has_hit &&
                            view_index < pick.consecutive_no_hit.size()) {
                            ++pick.consecutive_no_hit[view_index];
                            if (pick.consecutive_no_hit[view_index] >=
                                    kNoHitClearThreshold &&
                                view_index <
                                    pick.latest_hover_points.size()) {
                                pick.latest_hover_points[view_index].reset();
                            }
                        }
                        if (result.has_hit &&
                            view_index < pick.consecutive_no_hit.size()) {
                            pick.consecutive_no_hit[view_index] = 0;
                        }
                        pick.latest_capture_x[view_index] =
                            result.request.mouse_x;
                        pick.latest_capture_y[view_index] =
                            result.request.mouse_y;
                        if (pick_benchmark.enabled &&
                            result.request.benchmark_query_index >= 0) {
                            const auto query_index =
                                static_cast<std::size_t>(
                                    result.request.benchmark_query_index
                                );
                            BenchmarkPickObservedResult observed;
                            observed.query_index = query_index;
                            observed.has_hit = hit_point.has_value();
                            observed.gpu_has_hit = result.has_hit;
                            observed.point_id = result.point_id;
                            observed.depth = result.depth;
                            observed.issue_cpu_ms =
                                query_index <
                                        pick_benchmark.issue_cpu_ms.size()
                                    ? pick_benchmark.issue_cpu_ms[
                                          query_index
                                      ]
                                    : 0.0;
                            observed.collect_cpu_ms = collect_cpu_ms;
                            if (query_index <
                                pick_benchmark.issue_metadata.size()) {
                                observed.all_tiles_resident =
                                    pick_benchmark.issue_metadata[
                                        query_index
                                    ].all_tiles_resident;
                                observed.resident_tile_ids =
                                    pick_benchmark.issue_metadata[
                                        query_index
                                    ].resident_tile_ids;
                            }
                            if (hit_point) {
                                observed.x = hit_point->x;
                                observed.y = hit_point->y;
                                observed.z = hit_point->z;
                                observed.value = hit_point->value;
                            }
                            pick_benchmark.results.push_back(observed);
                        }
                        continue;
                    }

                    if (result.request.kind ==
                        GpuPickRequestKind::SetOrbitPivot) {
                        if (!hit_point.has_value()) {
                            pick_camera.controllers[view_index].clear_orbit_pivot();
                            pick_camera.selected_focus_points[view_index].reset();
                            std::cout
                                << "[CAMERA] orbit pivot cleared"
                                << " (double-clicked empty space)\n";
                            continue;
                        }

                        float raw = hit_point->z;
                        if (pick_camera.push.height_source ==
                            static_cast<std::uint32_t>(
                                gs3d::app::AttrPhysicalSource::Value)) {
                            raw = hit_point->value;
                        }
                        float mapped_z =
                            pick_camera.push.height_offset + raw * pick_camera.push.height_mult;

                        const gs3d::camera::Vec3 selected_point{
                            hit_point->x,
                            hit_point->y,
                            mapped_z
                        };
                        pick_camera.controllers[view_index].set_orbit_pivot(
                            selected_point
                        );
                        pick_camera.selected_focus_points[view_index] = selected_point;
                        pick_camera.streaming_viewport_index =
                            result.request.viewport_index;
                        std::cout
                            << "[CAMERA] orbit pivot selected at ["
                            << selected_point.x << ", "
                            << selected_point.y << ", "
                            << selected_point.z << "]\n";
                        continue;
                    }

                    if (result.request.kind !=
                        GpuPickRequestKind::BoxSelectAnchor) {
                        continue;
                    }

                    // Use the camera's own viewport so NDC conversion
                    // and the projection matrix use the same dimensions.
                    const gs3d::camera::Viewport mouse_viewport{
                        result.request.anchor_camera.viewport_width(),
                        result.request.anchor_camera.viewport_height()
                    };
                    const float plane_z =
                        hit_point
                            ? hit_point->z
                            : result.request.anchor_camera.target().z;
                    const auto selection_bounds =
                        gs3d::camera::box_select_world_bounds(
                            result.request.box_select_min_x,
                            result.request.box_select_min_y,
                            result.request.box_select_max_x,
                            result.request.box_select_max_y,
                            mouse_viewport,
                            result.request.anchor_camera,
                            pick_camera.bounds,
                            plane_z
                        );
                    if (!selection_bounds) {
                        continue;
                    }

                    // --- diagnostic: box-select full trace ---
                    constexpr bool kBoxFitDiag = false;  // set true to enable
                    const auto& ac = result.request.anchor_camera;
                    const float fb_min_x = result.request.box_select_min_x;
                    const float fb_min_y = result.request.box_select_min_y;
                    const float fb_max_x = result.request.box_select_max_x;
                    const float fb_max_y = result.request.box_select_max_y;

                    if (kBoxFitDiag) {
                        const auto& bc = ac;
                        std::fprintf(stderr,
                            "[BOXFIT] ========================================\n");
                        std::fprintf(stderr,
                            "[BOXFIT] fb_rect=(%.1f,%.1f)-(%.1f,%.1f) "
                            "fb_size=(%.1f,%.1f) fb_center=(%.1f,%.1f)\n",
                            static_cast<double>(fb_min_x),
                            static_cast<double>(fb_min_y),
                            static_cast<double>(fb_max_x),
                            static_cast<double>(fb_max_y),
                            static_cast<double>(fb_max_x - fb_min_x),
                            static_cast<double>(fb_max_y - fb_min_y),
                            static_cast<double>(0.5*(fb_min_x + fb_max_x)),
                            static_cast<double>(0.5*(fb_min_y + fb_max_y)));
                        std::fprintf(stderr,
                            "[BOXFIT] mouse_vp=%ux%u cam_vp=%ux%u "
                            "plane_z=%.2f vp_idx=%d\n",
                            mouse_viewport.width, mouse_viewport.height,
                            ac.viewport_width(), ac.viewport_height(),
                            static_cast<double>(plane_z),
                            result.request.viewport_index);
                        std::fprintf(stderr,
                            "[BOXFIT] cam_BEFORE: pos=(%.2f,%.2f,%.2f) "
                            "tgt=(%.2f,%.2f,%.2f) ortho_h=%.2f aspect=%.4f "
                            "near=%.4f far=%.2f\n",
                            static_cast<double>(bc.position().x),
                            static_cast<double>(bc.position().y),
                            static_cast<double>(bc.position().z),
                            static_cast<double>(bc.target().x),
                            static_cast<double>(bc.target().y),
                            static_cast<double>(bc.target().z),
                            static_cast<double>(bc.ortho_height()),
                            static_cast<double>(bc.aspect_ratio()),
                            static_cast<double>(bc.near_plane()),
                            static_cast<double>(bc.far_plane()));

                        // Print each corner's from_screen result
                        const std::array<std::pair<double,double>, 4> dbg_corners{{
                            {static_cast<double>(fb_min_x), static_cast<double>(fb_min_y)},
                            {static_cast<double>(fb_max_x), static_cast<double>(fb_min_y)},
                            {static_cast<double>(fb_min_x), static_cast<double>(fb_max_y)},
                            {static_cast<double>(fb_max_x), static_cast<double>(fb_max_y)},
                        }};
                        const char* dbg_names[] = {"TL","TR","BL","BR"};
                        for (int ci = 0; ci < 4; ++ci) {
                            auto dbg_ray = gs3d::camera::MouseRay::from_screen(
                                dbg_corners[ci].first, dbg_corners[ci].second,
                                mouse_viewport, ac);
                            auto dbg_hit = gs3d::camera::MouseRay::intersect_plane(
                                dbg_ray, {0,0,ac.target().z}, {0,0,1});
                            std::fprintf(stderr,
                                "[BOXFIT] corner[%s] scr=(%.1f,%.1f) "
                                "ray_org=(%.2f,%.2f,%.2f) ray_dir=(%.4f,%.4f,%.4f) "
                                "hit_z=%.2f %s",
                                dbg_names[ci],
                                dbg_corners[ci].first, dbg_corners[ci].second,
                                static_cast<double>(dbg_ray.origin.x),
                                static_cast<double>(dbg_ray.origin.y),
                                static_cast<double>(dbg_ray.origin.z),
                                static_cast<double>(dbg_ray.direction.x),
                                static_cast<double>(dbg_ray.direction.y),
                                static_cast<double>(dbg_ray.direction.z),
                                static_cast<double>(ac.target().z),
                                dbg_hit ? "hit" : "miss");
                            if (dbg_hit) std::fprintf(stderr,
                                "=(%.2f,%.2f,%.2f)",
                                static_cast<double>(dbg_hit->x),
                                static_cast<double>(dbg_hit->y),
                                static_cast<double>(dbg_hit->z));
                            std::fprintf(stderr, "\n");
                        }
                    }

                    auto& box_camera =
                        pick_camera.viewport_manager.camera(
                            result.request.viewport_index
                        );

                    // Save current state, run fit_screen_rect on the
                    // real camera (so the ray uses the correct viewport
                    // and old projection — see fit_screen_rect fix),
                    // then restore and animate toward the desired state.
                    const auto saved_pos = box_camera.position();
                    const auto saved_target = box_camera.target();
                    const float saved_ortho_h = box_camera.ortho_height();

                    box_camera.fit_screen_rect(
                        fb_min_x, fb_min_y,
                        fb_max_x, fb_max_y,
                        mouse_viewport.width, mouse_viewport.height,
                        1.05f);

                    const auto desired_target = box_camera.target();
                    const float desired_ortho_h = box_camera.ortho_height();
                    const auto desired_pos = box_camera.position();
                    const float desired_near = box_camera.near_plane();
                    const float desired_far = box_camera.far_plane();
                    const auto desired_up = box_camera.up();

                    // Restore and animate.
                    box_camera.look_at(
                        saved_pos, saved_target, box_camera.up());
                    box_camera.set_orthographic(
                        saved_ortho_h,
                        box_camera.near_plane(),
                        box_camera.far_plane());

                    pick_camera.controllers[
                        static_cast<std::size_t>(
                            result.request.viewport_index
                        )
                    ].animate_to(
                        box_camera,
                        desired_target,
                        desired_ortho_h
                    );

                    // --- expected box-zoom (screen-space formula) ---
                    if (kBoxFitDiag) {
                        const float old_ortho_h = ac.ortho_height();
                        const float vp_w = static_cast<float>(mouse_viewport.width);
                        const float vp_h = static_cast<float>(mouse_viewport.height);
                        const float rect_w = fb_max_x - fb_min_x;
                        const float rect_h = fb_max_y - fb_min_y;
                        const float scale_x = rect_w / std::max(vp_w, 1.0f);
                        const float scale_y = rect_h / std::max(vp_h, 1.0f);
                        const float expected_ortho_h =
                            old_ortho_h * std::max(scale_x, scale_y) * 1.05f;

                        const float ctr_x = 0.5f*(fb_min_x + fb_max_x);
                        const float ctr_y = 0.5f*(fb_min_y + fb_max_y);
                        auto ctr_ray = gs3d::camera::MouseRay::from_screen(
                            static_cast<double>(ctr_x),
                            static_cast<double>(ctr_y),
                            mouse_viewport, ac);
                        auto ctr_hit = gs3d::camera::MouseRay::intersect_plane(
                            ctr_ray, {0,0,ac.target().z}, {0,0,1});
                        float exp_tgt_x = ac.target().x;
                        float exp_tgt_y = ac.target().y;
                        if (ctr_hit) {
                            exp_tgt_x = ctr_hit->x;
                            exp_tgt_y = ctr_hit->y;
                        }

                        std::fprintf(stderr,
                            "[BOXFIT] EXPECTED: scale_x=%.4f scale_y=%.4f "
                            "ortho_h=%.2f target=(%.2f,%.2f,%.2f)\n",
                            static_cast<double>(scale_x),
                            static_cast<double>(scale_y),
                            static_cast<double>(expected_ortho_h),
                            static_cast<double>(exp_tgt_x),
                            static_cast<double>(exp_tgt_y),
                            static_cast<double>(ac.target().z));
                    }

                    if (kBoxFitDiag) {
                        std::fprintf(stderr,
                            "[BOXFIT] ACTUAL:   ortho_h=%.2f "
                            "target=(%.2f,%.2f,%.2f) "
                            "pos=(%.2f,%.2f,%.2f) "
                            "near=%.4f far=%.2f up=(%.2f,%.2f,%.2f)\n",
                            static_cast<double>(desired_ortho_h),
                            static_cast<double>(desired_target.x),
                            static_cast<double>(desired_target.y),
                            static_cast<double>(desired_target.z),
                            static_cast<double>(desired_pos.x),
                            static_cast<double>(desired_pos.y),
                            static_cast<double>(desired_pos.z),
                            static_cast<double>(desired_near),
                            static_cast<double>(desired_far),
                            static_cast<double>(desired_up.x),
                            static_cast<double>(desired_up.y),
                            static_cast<double>(desired_up.z));
                    }
                    pick_camera.streaming_viewport_index =
                        result.request.viewport_index;
                    pick_camera.tile_selection_dirty = true;
                }
}

} // namespace gs3d::app
