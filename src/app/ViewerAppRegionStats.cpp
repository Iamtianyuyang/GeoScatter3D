#include "app/ViewerApp.hpp"
#include "app/ViewerAppRunState.hpp"

#include "app/AppState.hpp"
#include "app/UiActions.hpp"
#include "camera/BoxSelect.hpp"
#include "camera/Camera.hpp"
#include "camera/MouseRay.hpp"
#include "data/Gs3dDataset.hpp"
#include "data/Gs3dReader.hpp"
#include "render/ViewportManager.hpp"

#include <filesystem>
#include <future>
#include <limits>

namespace gs3d::app {

void ViewerApp::handle_region_stats_commands(
    const UiActions& gui_cmds,
    AppState& app_state,
    const RegionStatsCommandContext& ctx
) {
    // ── 区域统计：测量模式下 Shift+左键框选（异步计算）──
    // 屏幕空间判断：逐点 world→screen 投影，检查是否落在框选矩形内。
    // 斜视/俯视均正确，不依赖平面反投影近似。
    //
    // 计算在后台线程执行，主线程立即返回继续渲染。
    // generation counter 实现取消：新框选使旧任务的 gen 失配，旧任务
    // 每 64K 点检查一次并提前退出，结果被丢弃。

    // ── Submit ──
    for (const auto& frame : gui_cmds.viewport_frames) {
        if (!frame.stats_select_completed || !frame.mouse_on_image) {
            continue;
        }
        if (frame.index < 0 ||
            frame.index >= ctx.viewport_manager.viewport_count()) {
            continue;
        }

        // Cancel any in-flight computation by bumping the generation.
        const auto gen = ++region_stats_gen_;

        const auto& cam = ctx.viewport_manager.camera(frame.index);
        const gs3d::camera::Mat4 vp =
            cam.view_projection_matrix();
        const float vp_w =
            static_cast<float>(cam.viewport_width());
        const float vp_h =
            static_cast<float>(cam.viewport_height());

        const float sx_min = frame.stats_select_min_x;
        const float sx_max = frame.stats_select_max_x;
        const float sy_min = frame.stats_select_min_y;
        const float sy_max = frame.stats_select_max_y;

        // Snapshot point-data access: raw pointer when in-memory
        // (dataset lives for the entire run() scope, safe to
        // reference from the short-lived worker), or copy the path
        // and let the worker read from disk.
        const bool has_points = ctx.dataset.has_point_data();
        const gs3d::data::Gs3dPoint* points_data =
            has_points ? ctx.dataset.points().data() : nullptr;
        const std::uint64_t point_count =
            has_points ? ctx.dataset.point_count() : 0;
        const std::filesystem::path gs3d_path =
            has_points ? std::filesystem::path{} : config_.gs3d_path;

        app_state.region_stats = RegionStatsResult{};
        app_state.region_stats.computing = true;
        app_state.region_stats.primary_label = ctx.primary_value_name;
        app_state.region_stats.secondary_label = ctx.z_field_name;

        // Compute world-space XY bounds of the selection rectangle
        // so the panel can display the approximate coordinate range.
        // box_select_world_bounds returns local-space coords (matching
        // the camera space); add origin to get absolute coords that
        // match the hover tooltip / map axis display.
        double world_x_min = 0.0;
        double world_x_max = 0.0;
        double world_y_min = 0.0;
        double world_y_max = 0.0;
        {
            const gs3d::camera::Viewport stats_viewport{
                static_cast<std::uint32_t>(vp_w),
                static_cast<std::uint32_t>(vp_h)
            };
            const float plane_z = cam.target().z;
            const auto selection_bounds =
                gs3d::camera::box_select_world_bounds(
                    sx_min, sy_min, sx_max, sy_max,
                    stats_viewport, cam, ctx.bounds, plane_z);
            if (selection_bounds) {
                const double ox = ctx.dataset.origin_x();
                const double oy = ctx.dataset.origin_y();
                world_x_min = static_cast<double>(selection_bounds->min.x) + ox;
                world_x_max = static_cast<double>(selection_bounds->max.x) + ox;
                world_y_min = static_cast<double>(selection_bounds->min.y) + oy;
                world_y_max = static_cast<double>(selection_bounds->max.y) + oy;
            }
        }

        region_stats_future_ = std::async(
            std::launch::async,
            [gen,
             vp, vp_w, vp_h,
             sx_min, sx_max, sy_min, sy_max,
             has_points, points_data, point_count,
             gs3d_path,
             world_x_min, world_x_max, world_y_min, world_y_max,
             primary_label = ctx.primary_value_name,
             secondary_label = ctx.z_field_name,
             &gen_counter = region_stats_gen_]() -> RegionStatsResult
            {
                double fold_sum = 0.0;
                double elev_sum = 0.0;
                float fold_min =
                    std::numeric_limits<float>::max();
                float fold_max =
                    std::numeric_limits<float>::lowest();
                float elev_min =
                    std::numeric_limits<float>::max();
                float elev_max =
                    std::numeric_limits<float>::lowest();
                std::uint64_t count = 0;

                const auto process =
                    [&](const gs3d::data::Gs3dPoint& p) {
                        const auto sp =
                            gs3d::camera::MouseRay::world_to_screen(
                                vp, p.x, p.y, p.z, vp_w, vp_h);
                        if (!sp) return;
                        if (sp->x < sx_min || sp->x > sx_max ||
                            sp->y < sy_min || sp->y > sy_max) {
                            return;
                        }
                        ++count;
                        const float f = p.value;
                        const float e = p.z;
                        fold_sum += static_cast<double>(f);
                        elev_sum += static_cast<double>(e);
                        if (f < fold_min) fold_min = f;
                        if (f > fold_max) fold_max = f;
                        if (e < elev_min) elev_min = e;
                        if (e > elev_max) elev_max = e;
                    };

                constexpr std::uint64_t kCancelCheckInterval =
                    65536;

                if (has_points) {
                    for (std::uint64_t i = 0; i < point_count;
                         ++i) {
                        if ((i & (kCancelCheckInterval - 1)) == 0) {
                            if (gen_counter.load(
                                    std::memory_order_relaxed) !=
                                gen) {
                                return RegionStatsResult{};
                            }
                        }
                        process(points_data[i]);
                    }
                } else {
                    auto read_result =
                        gs3d::data::Gs3dReader::read_all(
                            gs3d_path);
                    std::uint64_t i = 0;
                    for (const auto& p : read_result.points) {
                        if ((i & (kCancelCheckInterval - 1)) == 0) {
                            if (gen_counter.load(
                                    std::memory_order_relaxed) !=
                                gen) {
                                return RegionStatsResult{};
                            }
                        }
                        process(p);
                        ++i;
                    }
                }

                RegionStatsResult out;
                out.valid = true;
                out.point_count = count;
                out.world_x_min = world_x_min;
                out.world_x_max = world_x_max;
                out.world_y_min = world_y_min;
                out.world_y_max = world_y_max;
                out.primary_label = primary_label;
                out.secondary_label = secondary_label;
                if (count > 0) {
                    const double inv =
                        1.0 / static_cast<double>(count);
                    out.fold_min = fold_min;
                    out.fold_max = fold_max;
                    out.fold_avg =
                        static_cast<float>(fold_sum * inv);
                    out.elev_min = elev_min;
                    out.elev_max = elev_max;
                    out.elev_avg =
                        static_cast<float>(elev_sum * inv);
                }
                return out;
            });

        break; // one launch per frame
    }

    // ── Poll ──
    // Poll completion: when the worker finishes, swap its result
    // into app_state.  If the result is invalid (cancelled),
    // just clear the computing flag so the panel goes back to idle.
    if (region_stats_future_.valid()) {
        if (region_stats_future_.wait_for(
                std::chrono::seconds(0)) ==
            std::future_status::ready) {
            auto result = region_stats_future_.get();
            if (result.valid) {
                app_state.region_stats = std::move(result);
            } else {
                app_state.region_stats.computing = false;
            }
        }
    }
}

} // namespace gs3d::app
