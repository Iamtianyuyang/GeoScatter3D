#include "app/ViewerApp.hpp"
#include "app/ViewerAppInternal.hpp"

#include "app/UiActions.hpp"
#include "app/ViewportResizeScheduler.hpp"
#include "camera/MouseRay.hpp"
#include "render/AxisGrid.hpp"

#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace gs3d::app {

/*
 * QGIS Print Layout 风格的地图坐标框：
 *   底面矩形框 + 四边刻度标注 + 四根角柱 + 高程刻度。
 *
 * 所有几何在这里算好（世界坐标 -> 视口本地像素），UiRoot 只管用 ImGui
 * draw list 画线/画字，不需要碰相机或投影矩阵。
 */
/*
 * Orientation gizmo 三轴方向：用 MouseRay::to_screen 把相机 target 点和
 * target+axis*step 点分别投影到屏幕，差值即为世界轴在屏幕上的方向。
 * 存进 RenderViewState::gizmo_*_axis，UiRoot 只管照着画线。
 * 每帧调用，所以旋转主视图时 gizmo 同步旋转。
 */
void compute_gizmo_axes(
    gs3d::app::RenderViewState& view,
    const gs3d::camera::Camera& camera
) {
    view.gizmo_axes_valid = false;

    const gs3d::camera::Viewport viewport{
        camera.viewport_width(),
        camera.viewport_height()
    };

    const auto target = camera.target();
    const float step = 1.0f;
    const gs3d::camera::Vec3 axes[] = {
        {step, 0.0f, 0.0f},
        {0.0f, step, 0.0f},
        {0.0f, 0.0f, step},
    };

    const auto center =
        gs3d::camera::MouseRay::to_screen(target, viewport, camera);
    if (!center) return;

    // 「target→相机」单位向量：轴与它的点积即为朝向观察者的分量，
    // 供 gizmo 做前后排序/背面变暗（dx/dy 在轴指向相机时会退化，
    // 无法承载这一信息）。
    const auto position = camera.position();
    gs3d::camera::Vec3 to_eye{
        position.x - target.x,
        position.y - target.y,
        position.z - target.z
    };
    const float to_eye_len = std::sqrt(
        to_eye.x * to_eye.x + to_eye.y * to_eye.y + to_eye.z * to_eye.z);
    if (to_eye_len > 1.0e-6f) {
        to_eye.x /= to_eye_len;
        to_eye.y /= to_eye_len;
        to_eye.z /= to_eye_len;
    }

    gs3d::app::RenderViewState::GizmoAxisEnd ends[3];

    for (int i = 0; i < 3; ++i) {
        const auto tip = gs3d::camera::MouseRay::to_screen(
            {target.x + axes[i].x,
             target.y + axes[i].y,
             target.z + axes[i].z},
            viewport, camera);
        if (!tip) return;

        ends[i].dx = tip->x - center->x;
        ends[i].dy = tip->y - center->y;
        ends[i].depth =
            (axes[i].x * to_eye.x +
             axes[i].y * to_eye.y +
             axes[i].z * to_eye.z) / step;
    }

    view.gizmo_x_axis   = ends[0];
    view.gizmo_y_axis   = ends[1];
    view.gizmo_z_axis   = ends[2];
    view.gizmo_axes_valid = true;
}

void compute_axis_overlay(
    gs3d::app::RenderViewState& view,
    const gs3d::camera::CameraBounds& bounds,
    const gs3d::camera::Camera& camera,
    double origin_x,
    double origin_y,
    double origin_z,
    float  z_label_mult,
    float  z_label_offset,
    bool   z_axis_add_origin_z
) {
    view.axis_lines.clear();
    view.axis_tick_labels.clear();

    if (!view.show_world_axis) {
        return;
    }

    const gs3d::camera::Viewport viewport{
        camera.viewport_width(),
        camera.viewport_height()
    };

    const float min_x = bounds.min.x;
    const float min_y = bounds.min.y;
    const float min_z = bounds.min.z;
    const float max_x = bounds.max.x;
    const float max_y = bounds.max.y;
    const float max_z = bounds.max.z;

    // ---- helper: project to screen, skip if behind camera ----
    const auto sv = [&](float x, float y, float z)
        -> std::optional<gs3d::camera::ScreenPoint>
    {
        return gs3d::camera::MouseRay::to_screen(
            {x, y, z}, viewport, camera
        );
    };

    // ---- helper: push a screen-space line segment ----
    const auto add_line =
        [&](const std::optional<gs3d::camera::ScreenPoint>& a,
            const std::optional<gs3d::camera::ScreenPoint>& b) {
            if (a && b) {
                view.axis_lines.push_back({a->x, a->y, b->x, b->y});
            }
        };

    // ---- 1. 底面矩形框（四条边，参考面 = z_label_offset，对应属性值 0）----
    const float ref_z = z_label_offset;
    add_line(sv(min_x, min_y, ref_z), sv(max_x, min_y, ref_z)); // 底边
    add_line(sv(max_x, min_y, ref_z), sv(max_x, max_y, ref_z)); // 右边
    add_line(sv(max_x, max_y, ref_z), sv(min_x, max_y, ref_z)); // 顶边
    add_line(sv(min_x, max_y, ref_z), sv(min_x, min_y, ref_z)); // 左边

    // ---- 2. 四根角柱（覆盖数据范围 + 参考面）----
    const float pillar_base = std::min(min_z, ref_z);
    const float pillar_top  = std::max(max_z, ref_z);
    add_line(sv(min_x, min_y, pillar_base), sv(min_x, min_y, pillar_top));
    add_line(sv(max_x, min_y, pillar_base), sv(max_x, min_y, pillar_top));
    add_line(sv(max_x, max_y, pillar_base), sv(max_x, max_y, pillar_top));
    add_line(sv(min_x, max_y, pillar_base), sv(min_x, max_y, pillar_top));

    // ---- 3. X 轴刻度（底边 + 顶边，参考面）----
    {
        const auto x_ticks =
            gs3d::render::compute_axis_ticks(min_x, max_x, 5);
        for (const float tick : x_ticks) {
            const auto bottom = sv(tick, min_y, ref_z);
            const auto top    = sv(tick, max_y, ref_z);
            // small tick mark (8 px outward from the frame)
            constexpr float kMarkPx = 8.0f;
            if (bottom) {
                // Tick along bottom edge, label below frame (y + kMarkPx)
                view.axis_lines.push_back(
                    {bottom->x, bottom->y, bottom->x, bottom->y + kMarkPx}
                );
                char label[32];
                std::snprintf(label, sizeof(label), "%.0f",
                    static_cast<double>(tick) + origin_x);
                view.axis_tick_labels.push_back(
                    {bottom->x, bottom->y + kMarkPx + 2.0f, label}
                );
            }
            if (top) {
                view.axis_lines.push_back(
                    {top->x, top->y, top->x, top->y - kMarkPx}
                );
                char label[32];
                std::snprintf(label, sizeof(label), "%.0f",
                    static_cast<double>(tick) + origin_x);
                view.axis_tick_labels.push_back(
                    {top->x, top->y - kMarkPx - 14.0f, label}
                );
            }
        }
    }

    // ---- 4. Y 轴刻度（左边 + 右边，参考面）----
    {
        const auto y_ticks =
            gs3d::render::compute_axis_ticks(min_y, max_y, 5);
        for (const float tick : y_ticks) {
            const auto left  = sv(min_x, tick, ref_z);
            const auto right = sv(max_x, tick, ref_z);
            constexpr float kMarkPx = 8.0f;
            if (left) {
                view.axis_lines.push_back(
                    {left->x, left->y, left->x - kMarkPx, left->y}
                );
                char label[32];
                std::snprintf(label, sizeof(label), "%.0f",
                    static_cast<double>(tick) + origin_y);
                view.axis_tick_labels.push_back(
                    {left->x - kMarkPx - 3.0f, left->y, label}
                );
            }
            if (right) {
                view.axis_lines.push_back(
                    {right->x, right->y, right->x + kMarkPx, right->y}
                );
                char label[32];
                std::snprintf(label, sizeof(label), "%.0f",
                    static_cast<double>(tick) + origin_y);
                view.axis_tick_labels.push_back(
                    {right->x + kMarkPx + 2.0f, right->y, label}
                );
            }
        }
    }

    // ---- 5. Z / 高程刻度（左下角柱）----
    // 在标签空间（属性原始值范围）确定刻度，再映射回世界 Z 画几何。
    // nice_step 进位激进（>2.0→5.0），对小范围可能只产 1 个 tick，
    // 逐步提高目标数兜底，确保至少 2 个刻度。
    {
        const float label_min =
            (min_z - z_label_offset) / z_label_mult;
        const float label_max =
            (max_z - z_label_offset) / z_label_mult;
        std::vector<float> label_ticks;
        for (int target = 4; target <= 12 && label_ticks.size() < 2; target += 2) {
            label_ticks =
                gs3d::render::compute_axis_ticks(label_min, label_max, target);
        }

        for (const float label_val : label_ticks) {
            const float world_z =
                label_val * z_label_mult + z_label_offset;
            const auto p = sv(min_x, min_y, world_z);
            if (!p) continue;
            constexpr float kMarkPx = 8.0f;
            // Z 刻度向右伸出，避免与 Y 刻度（向左）在左下角重叠。
            view.axis_lines.push_back(
                {p->x, p->y, p->x + kMarkPx, p->y}
            );
            char label[32];
            double display_val = static_cast<double>(label_val);
            if (z_axis_add_origin_z) {
                display_val += origin_z;
            }
            std::snprintf(label, sizeof(label), "%.6g", display_val);
            view.axis_tick_labels.push_back(
                {p->x + kMarkPx + 2.0f, p->y, label}
            );
        }
    }
}

/*
 * 地图式坐标轴可见范围：从相机参数估算视口内 X/Y 平面的可见坐标范围。
 * 对 2.5D 数据是合理近似，极斜视角下有轻微偏差但不影响刻度使用。
 */
void compute_map_axis_overlay(
    gs3d::app::RenderViewState& view,
    const gs3d::camera::Camera& camera,
    double origin_x,
    double origin_y
) {
    const gs3d::camera::Viewport vp{
        camera.viewport_width(),
        camera.viewport_height()
    };

    // Ground plane: Z = 0, normal = (0, 0, 1)
    const gs3d::camera::Vec3 ground_point{0.0f, 0.0f, 0.0f};
    const gs3d::camera::Vec3 ground_normal{0.0f, 0.0f, 1.0f};

    // 4 corners of the viewport in screen space (y=0 is top in GLFW/ImGui)
    const float scr_w = static_cast<float>(vp.width);
    const float scr_h = static_cast<float>(vp.height);
    const float corners[4][2] = {
        {0.0f,     0.0f},       // top-left
        {scr_w,    0.0f},       // top-right
        {0.0f,     scr_h},      // bottom-left
        {scr_w,    scr_h}       // bottom-right
    };

    float xs[4], ys[4];
    int valid = 0;

    for (int i = 0; i < 4; ++i) {
        const auto ray = gs3d::camera::MouseRay::from_screen(
            static_cast<double>(corners[i][0]),
            static_cast<double>(corners[i][1]),
            vp,
            camera
        );

        const auto hit = gs3d::camera::MouseRay::intersect_plane(
            ray, ground_point, ground_normal
        );

        if (hit) {
            xs[valid] = hit->x;
            ys[valid] = hit->y;
            ++valid;
        }

    }

    if (valid == 0) {
        /*
         * No ray hits Z=0 (e.g. camera is below the ground plane or
         * looking upwards). Fall back to the visible-world estimate
         * centred on the camera target.
         */
        float world_w, world_h;
        if (camera.projection_mode() ==
            gs3d::camera::ProjectionMode::Orthographic) {
            world_h = camera.ortho_height();
            world_w = world_h * camera.aspect_ratio();
        } else {
            const float distance = camera.distance();
            const float fov_rad  =
                camera.fov_y_degrees() * (3.14159265f / 180.0f);
            world_h = 2.0f * distance * std::tan(fov_rad * 0.5f);
            world_w = world_h * camera.aspect_ratio();
        }
        const float cx = camera.target().x;
        const float cy = camera.target().y;
        view.map_axis_x_min = cx - world_w * 0.5f;
        view.map_axis_x_max = cx + world_w * 0.5f;
        view.map_axis_y_min = cy - world_h * 0.5f;
        view.map_axis_y_max = cy + world_h * 0.5f;
    } else {
        float x_min = xs[0], x_max = xs[0];
        float y_min = ys[0], y_max = ys[0];
        for (int i = 1; i < valid; ++i) {
            if (xs[i] < x_min) x_min = xs[i];
            if (xs[i] > x_max) x_max = xs[i];
            if (ys[i] < y_min) y_min = ys[i];
            if (ys[i] > y_max) y_max = ys[i];
        }
        view.map_axis_x_min = x_min;
        view.map_axis_x_max = x_max;
        view.map_axis_y_min = y_min;
        view.map_axis_y_max = y_max;
    }

    view.map_axis_origin_x = origin_x;
    view.map_axis_origin_y = origin_y;

}

void ViewerApp::observe_viewport_resize_requests(
    const UiActions& gui_cmds,
    ViewportResizeScheduler& scheduler,
    double now_seconds
) {
    for (const auto& frame : gui_cmds.viewport_frames) {
        scheduler.observe(
            frame.index,
            frame.width,
            frame.height,
            now_seconds
        );
    }
}

} // namespace gs3d::app
