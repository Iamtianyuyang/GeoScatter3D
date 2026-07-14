#include "app/ViewerApp.hpp"
#include "app/ViewerAppRunState.hpp"

#include "app/AppState.hpp"
#include "camera/MouseRay.hpp"
#include "data/Gs3dDataset.hpp"
#include "render/PointPipeline.hpp"
#include "render/ViewportManager.hpp"

#include <algorithm>
#include <cmath>

namespace gs3d::app {

namespace {

const MeasurementManager& measurement_for_render_view(
    const AppState& app_state,
    int viewport_index
) {
    return measurement_for_view(app_state, viewport_index);
}

} // namespace

void ViewerApp::fill_render_views(
    gs3d::app::AppState& app_state,
    const ViewerAppRenderViewContext& ctx,
    const ViewerPickState& pick,
    const std::vector<std::optional<gs3d::camera::Vec3>>& selected_focus_points
) {
    for (int i = 0; i < ctx.n_viewports; ++i) {
                const auto& camera = ctx.viewport_manager.camera(i);
                auto& view =
                    app_state.render_views[static_cast<std::size_t>(i)];
                view.viewport_index = i;
                view.descriptor =
                    ctx.viewport_manager.framebuffer(i).imgui_descriptor();
                view.show_live_image = view.descriptor != VK_NULL_HANDLE;
                view.image_width = camera.viewport_width();
                view.image_height = camera.viewport_height();
                view.points_visible = ctx.visible_points;
                view.points_total = ctx.dataset.point_count();
                view.frame_time_ms = app_state.performance.frame_time_ms;
                view.camera_mode = "轨道";
                view.position = format_vec3_text(camera.position());
                view.fov = camera.fov_y_degrees();
                const auto& view_push =
                    static_cast<std::size_t>(i) < ctx.viewport_pushes.size()
                        ? ctx.viewport_pushes[static_cast<std::size_t>(i)]
                        : ctx.viewport_pushes.front();
                const auto& measurement =
                    measurement_for_render_view(app_state, i);
                view.measure_mode_active =
                    measurement.measure_mode_active();

                const float vp_h =
                    static_cast<float>(camera.viewport_height());
                float scale_world = 500.0f;
                if (vp_h > 1.0f) {
                    float pixel_world = 0.0f;
                    if (camera.projection_mode() ==
                        gs3d::camera::ProjectionMode::Orthographic) {
                        pixel_world = camera.ortho_height() / vp_h;
                    } else {
                        const float d = camera.distance();
                        const float fov_rad =
                            camera.fov_y_degrees() * (3.14159265f / 180.0f);
                        pixel_world = 2.0f * d * std::tan(fov_rad * 0.5f) / vp_h;
                    }
                    if (pixel_world > 0.0f) {
                        scale_world = nice_scale_distance(pixel_world * 96.0f);
                    }
                }
                view.scale = format_scale_distance(scale_world);

                // Z 轴范围 + 刻度标签同步当前高度属性 & 夸张系数。
                // 几何：包围盒 Z 用 world-space 范围（含 exag）。
                // 标签：逆映射回属性原始值。
                //   - Z source: 参考面 = 世界 Z=0 (= -origin_z * exag 渲染坐标)
                //   - Value source: 参考面 = 属性值 0 (= height_offset 渲染坐标)
                auto axis_bounds = ctx.bounds;
                const float z_label_mult   = view_push.height_mult;
                float       z_label_offset = view_push.height_offset;
                if (view_push.height_source == static_cast<std::uint32_t>(gs3d::app::AttrPhysicalSource::Z)) {
                    axis_bounds.min.z = view_push.height_offset + ctx.dataset.bbox_min_z() * view_push.height_mult;
                    axis_bounds.max.z = view_push.height_offset + ctx.dataset.bbox_max_z() * view_push.height_mult;
                } else {
                    axis_bounds.min.z = view_push.height_offset + ctx.dataset.value_min() * view_push.height_mult;
                    axis_bounds.max.z = view_push.height_offset + ctx.dataset.value_max() * view_push.height_mult;
                }

                compute_axis_overlay(
                    view,
                    axis_bounds,
                    camera,
                    ctx.dataset.origin_x(),
                    ctx.dataset.origin_y(),
                    ctx.dataset.origin_z(),
                    z_label_mult,
                    z_label_offset,
                    view_push.height_source ==
                        static_cast<std::uint32_t>(
                            gs3d::app::AttrPhysicalSource::Z)
                );

                compute_map_axis_overlay(
                    view,
                    camera,
                    ctx.dataset.origin_x(),
                    ctx.dataset.origin_y()
                );

                compute_gizmo_axes(view, camera);

                const auto point_to_render_position =
                    [&](const gs3d::data::Gs3dPoint& point)
                        -> gs3d::camera::Vec3
                    {
                        float raw_height = point.z;
                        if (view_push.height_source ==
                            static_cast<std::uint32_t>(
                                gs3d::app::AttrPhysicalSource::Value)) {
                            raw_height = point.value;
                        }
                        return {
                            point.x,
                            point.y,
                            view_push.height_offset +
                                raw_height * view_push.height_mult
                        };
                    };

                const auto& hover_point =
                    pick.latest_hover_points[static_cast<std::size_t>(i)];

                // Timeout only increments when no pick request is issued
                // (cursor outside image).  While the cursor is on the
                // image and picks are in flight, existing data stays live.
                constexpr int kHoverTimeoutFrames = 30;
                auto& ht = pick.hover_timeout[static_cast<std::size_t>(i)];
                view.hover_tooltip_visible =
                    hover_point.has_value() && ht <= kHoverTimeoutFrames;
                view.hover_x = 0.0f;
                view.hover_y = 0.0f;
                view.hover_fold = 0.0f;
                view.hover_elevation = 0.0f;
                view.hover_primary_value_label = ctx.primary_value_name;
                view.hover_z_label = ctx.z_field_name;
                view.hover_screen_x = -1.0f;
                view.hover_screen_y = -1.0f;
                if (hover_point) {
                    view.hover_x =
                        static_cast<float>(
                            static_cast<double>(hover_point->x) +
                            ctx.dataset.origin_x());
                    view.hover_y =
                        static_cast<float>(
                            static_cast<double>(hover_point->y) +
                            ctx.dataset.origin_y());
                    view.hover_fold = hover_point->value;
                    view.hover_elevation =
                        static_cast<float>(
                            static_cast<double>(hover_point->z) +
                            ctx.dataset.origin_z());

                    // Z 映射：与 vertex shader 的 height = offset + raw * mult
                    // 完全一致，对所有 height_source 统一应用，否则高度缩放后
                    // 准星投影会与渲染点错位。
                    // Marker screen position via to_screen projection.
                    const auto screen_pt =
                        gs3d::camera::MouseRay::to_screen(
                            point_to_render_position(*hover_point),
                            {camera.viewport_width(),
                             camera.viewport_height()},
                            camera);
                    if (screen_pt) {
                        view.hover_screen_x = screen_pt->x;
                        view.hover_screen_y = screen_pt->y;
                    }
                }

                const auto view_index = static_cast<std::size_t>(i);
                view.selected_point_visible = false;
                view.selected_screen_x = -1.0f;
                view.selected_screen_y = -1.0f;
                if (view_index < selected_focus_points.size() &&
                    selected_focus_points[view_index].has_value()) {
                    const auto selected_screen =
                        gs3d::camera::MouseRay::to_screen(
                            *selected_focus_points[view_index],
                            {camera.viewport_width(),
                             camera.viewport_height()},
                            camera
                        );
                    if (selected_screen.has_value()) {
                        view.selected_point_visible = true;
                        view.selected_screen_x = selected_screen->x;
                        view.selected_screen_y = selected_screen->y;
                    }
                }

                // ── 测量线投影：每条全局测量线的两端点 → 本视口屏幕坐标 ──
                {
                    const auto& lines = measurement.lines();
                    view.measurement_overlays.clear();
                    view.measurement_overlays.reserve(lines.size());
                    for (const auto& line : lines) {
                        RenderViewState::MeasurementLineOverlay overlay;
                        overlay.color = line.color;
                        overlay.label = line.distance_label(
                            measurement.display_mode());

                        const auto sa = gs3d::camera::MouseRay::to_screen(
                            point_to_render_position(line.point_a),
                            {camera.viewport_width(),
                             camera.viewport_height()},
                            camera);
                        const auto sb = gs3d::camera::MouseRay::to_screen(
                            point_to_render_position(line.point_b),
                            {camera.viewport_width(),
                             camera.viewport_height()},
                            camera);

                        if (sa && sb) {
                            overlay.a_screen_x = sa->x;
                            overlay.a_screen_y = sa->y;
                            overlay.b_screen_x = sb->x;
                            overlay.b_screen_y = sb->y;
                            overlay.visible = true;
                        }
                        view.measurement_overlays.push_back(overlay);
                    }
                }

                // ── 待定测量点投影（选了第一个点，等第二个点）──
                view.pending_point_visible = false;
                view.pending_point_screen_x = -1.0f;
                view.pending_point_screen_y = -1.0f;
                if (measurement.has_pending()) {
                    const auto& pending = *measurement.pending_point();
                    const auto sp = gs3d::camera::MouseRay::to_screen(
                        point_to_render_position(pending),
                        {camera.viewport_width(),
                         camera.viewport_height()},
                        camera);
                    if (sp) {
                        view.pending_point_visible = true;
                        view.pending_point_screen_x = sp->x;
                        view.pending_point_screen_y = sp->y;
                    }
                }
    }

}

} // namespace gs3d::app
