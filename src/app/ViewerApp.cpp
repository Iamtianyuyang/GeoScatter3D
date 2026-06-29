#include "app/ViewerApp.hpp"

#include "app/AppState.hpp"
#include "app/TilePointCache.hpp"
#include "app/ViewportResizeScheduler.hpp"
#include "gui/ImGuiLayer.hpp"
#include "render/ViewportManager.hpp"
#include "imgui.h"

#include "camera/BoxSelect.hpp"
#include "camera/Camera.hpp"
#include "camera/CameraController.hpp"
#include "camera/CameraHub.hpp"
#include "camera/MouseRay.hpp"
#include "data/Gs3dDataset.hpp"
#include "data/Gs3dLodDataset.hpp"
#include "data/Gs3dLodReader.hpp"
#include "data/Gs3dLodTargets.hpp"
#include "data/PointDataAdapters.hpp"
#include "data/TileDataAdapters.hpp"
#include "data/Gs3dTileReader.hpp"

#include "platform/Window.hpp"
#include "render/AxisGrid.hpp"
#include "render/LodSelector.hpp"
#include "render/NearestPointQuery.hpp"
#include "render/PointCloudGpu.hpp"
#include "render/PointCloudLodGpu.hpp"
#include "render/PointPipeline.hpp"
#include "render/VulkanContext.hpp"
#include "render/VulkanRenderer.hpp"
#include "render/VulkanSwapchain.hpp"
#include "render/PointCloudTileGpu.hpp"
#include "render/TileSelection.hpp"

#include "preprocess/Gs3dLodWriter.hpp"
#include "util/PercentileStats.hpp"
#include "util/Stopwatch.hpp"
#include "scene/SceneState.hpp"

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <optional>
#include <sstream>

namespace gs3d::app {

namespace {

gs3d::camera::Vec3 to_vec3(
    const std::array<float, 3>& value
) {
    return {
        value[0],
        value[1],
        value[2]
    };
}

gs3d::camera::CameraBounds make_camera_bounds(
    const gs3d::data::Gs3dDataset& dataset
) {
    gs3d::camera::CameraBounds bounds;

    bounds.min = {
        dataset.bbox_min_x(),
        dataset.bbox_min_y(),
        dataset.bbox_min_z()
    };

    bounds.max = {
        dataset.bbox_max_x(),
        dataset.bbox_max_y(),
        dataset.bbox_max_z()
    };

    return bounds;
}

gs3d::data::Gs3dLodVoxelMode parse_lod_voxel_mode(
    const std::string& mode
) {
    if (mode == "XY" || mode == "xy") {
        return gs3d::data::Gs3dLodVoxelMode::XY;
    }

    if (mode == "XYZ" || mode == "xyz") {
        return gs3d::data::Gs3dLodVoxelMode::XYZ;
    }

    throw std::runtime_error(
        "ViewerApp: unsupported LOD voxel_mode: " + mode
    );
}

gs3d::render::TileSelectionConfig make_tile_selection_config(
    const ViewerAppConfig& config
) {
    gs3d::render::TileSelectionConfig tile_config;

    tile_config.min_tile_pixel_size =
        config.tile_min_pixel_size;
    tile_config.max_visible_tiles =
        config.tile_max_visible_tiles;

    tile_config.use_full_z_range =
        config.tile_use_full_z_range;

    return tile_config;
}

gs3d::data::Gs3dLodDataset build_runtime_lod_dataset(
    const gs3d::data::Gs3dDataset& dataset,
    const ViewerAppConfig& config
) {
    gs3d::data::Gs3dLodBuildConfig lod_config;
    lod_config.include_full_resolution_level = false;
    lod_config.target_point_counts =
        gs3d::data::resolve_lod_target_point_counts(
            dataset.point_count(),
            config.lod_target_point_ratios,
            config.lod_target_point_counts
        );
    lod_config.voxel_mode =
        parse_lod_voxel_mode(config.lod_voxel_mode);
    lod_config.voxel_scale =
        config.lod_voxel_scale;
    lod_config.verbose =
        config.lod_verbose;

    return gs3d::data::Gs3dLodDataset::build(
        dataset,
        lod_config
    );
}

gs3d::data::Gs3dLodDataset load_or_build_lod_dataset(
    const gs3d::data::Gs3dDataset& dataset,
    const ViewerAppConfig& config
) {
    if (!config.lod_enabled) {
        return {};
    }

    const auto& sidecar_path =
        config.lod_sidecar_path;

    if (config.lod_auto_load_sidecar &&
        !sidecar_path.empty() &&
        std::filesystem::exists(sidecar_path)) {
        try {
            gs3d::data::Gs3dLodReadConfig read_config;
            read_config.validate_against_source = true;
            read_config.verbose = config.lod_verbose;

            const auto read_result =
                gs3d::data::Gs3dLodReader::read(
                    sidecar_path,
                    dataset.header(),
                    read_config
                );

            std::cout << "[OK] LOD sidecar loaded.\n";
            std::cout << "path = "
                      << sidecar_path.string()
                      << '\n';

            return read_result.dataset;

        } catch (const std::exception& e) {
            std::cout << "[WARN] Failed to load LOD sidecar.\n";
            std::cout << "[WARN] path = "
                      << sidecar_path.string()
                      << '\n';
            std::cout << "[WARN] reason = "
                      << e.what()
                      << '\n';
            std::cout << "[WARN] Falling back to runtime LOD build.\n";
        }
    } else if (config.lod_auto_load_sidecar &&
               !sidecar_path.empty()) {
        std::cout << "[LOD] sidecar not found, runtime build required.\n";
        std::cout << "path = "
                  << sidecar_path.string()
                  << '\n';
    }

    auto lod_dataset =
        build_runtime_lod_dataset(
            dataset,
            config
        );

    std::cout << lod_dataset.summary();

    if (config.lod_auto_save_sidecar &&
        !sidecar_path.empty()) {
        try {
            const auto write_stats =
                gs3d::preprocess::Gs3dLodWriter::write(
                    sidecar_path,
                    lod_dataset
                );

            std::cout << "[OK] LOD sidecar written.\n";
            std::cout << "path = "
                      << write_stats.path.string()
                      << '\n';
            std::cout << "file_bytes = "
                      << write_stats.total_file_bytes
                      << '\n';

        } catch (const std::exception& e) {
            std::cout << "[WARN] Failed to write LOD sidecar.\n";
            std::cout << "[WARN] path = "
                      << sidecar_path.string()
                      << '\n';
            std::cout << "[WARN] reason = "
                      << e.what()
                      << '\n';
        }
    }

    return lod_dataset;
}

gs3d::render::PointCloudLodSource build_lod_source(
    const gs3d::data::Gs3dLodDataset& lod_dataset
) {
    gs3d::render::PointCloudLodSource source;
    source.levels.reserve(lod_dataset.level_count());
    for (const auto& level : lod_dataset.levels()) {
        source.levels.push_back({
            level.name,
            gs3d::data::Gs3dLodDataset::voxel_mode_name(level.voxel_mode),
            level.level_index,
            level.source_point_count,
            level.target_point_count,
            level.voxel_size,
            gs3d::data::make_point_data_view(level.points)
        });
    }
    return source;
}

void initialize_camera_from_config(
    gs3d::camera::Camera& camera,
    const ViewerAppConfig& config,
    const gs3d::camera::CameraBounds& bounds
) {
    camera.set_perspective(
        config.camera_fov_y,
        config.camera_near,
        config.camera_far
    );

    if (config.camera_mode == "fit") {
        camera.fit_bounds(bounds);
        return;
    }

    camera.look_at(
        to_vec3(config.camera_position),
        to_vec3(config.camera_target),
        to_vec3(config.camera_up)
    );
}

void fill_push_constants(
    gs3d::render::PointPushConstants& push,
    const gs3d::camera::Camera& camera
) {
    const auto mvp = camera.view_projection_matrix();
    std::copy(mvp.m.begin(), mvp.m.end(), push.mvp);
    // value_min / value_range / attr_index are managed by the
    // attribute-selection system and must not be overwritten here.
    push.clip_mode = 0.0f;
}

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
    double origin_z
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

    // ---- 1. 底面矩形框（四条边）----
    add_line(sv(min_x, min_y, min_z), sv(max_x, min_y, min_z)); // 底边
    add_line(sv(max_x, min_y, min_z), sv(max_x, max_y, min_z)); // 右边
    add_line(sv(max_x, max_y, min_z), sv(min_x, max_y, min_z)); // 顶边
    add_line(sv(min_x, max_y, min_z), sv(min_x, min_y, min_z)); // 左边

    // ---- 2. 四根角柱（底面角点 -> 顶面角点）----
    add_line(sv(min_x, min_y, min_z), sv(min_x, min_y, max_z));
    add_line(sv(max_x, min_y, min_z), sv(max_x, min_y, max_z));
    add_line(sv(max_x, max_y, min_z), sv(max_x, max_y, max_z));
    add_line(sv(min_x, max_y, min_z), sv(min_x, max_y, max_z));

    // ---- 3. X 轴刻度（底边 + 顶边）----
    {
        const auto x_ticks =
            gs3d::render::compute_axis_ticks(min_x, max_x, 5);
        for (const float tick : x_ticks) {
            const auto bottom = sv(tick, min_y, min_z);
            const auto top    = sv(tick, max_y, min_z);
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

    // ---- 4. Y 轴刻度（左边 + 右边）----
    {
        const auto y_ticks =
            gs3d::render::compute_axis_ticks(min_y, max_y, 5);
        for (const float tick : y_ticks) {
            const auto left  = sv(min_x, tick, min_z);
            const auto right = sv(max_x, tick, min_z);
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
    {
        const auto z_ticks =
            gs3d::render::compute_axis_ticks(min_z, max_z, 4);
        for (const float tick : z_ticks) {
            const auto p = sv(min_x, min_y, tick);
            if (!p) continue;
            constexpr float kMarkPx = 8.0f;
            view.axis_lines.push_back(
                {p->x, p->y, p->x - kMarkPx, p->y}
            );
            char label[32];
            std::snprintf(label, sizeof(label), "%.0f",
                static_cast<double>(tick) + origin_z);
            view.axis_tick_labels.push_back(
                {p->x - kMarkPx - 3.0f, p->y, label}
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
    if (!view.show_map_axis) {
        return;
    }

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
         * looking upwards). Fall back to the perpendicular-plane estimate
         * centred on the camera target.
         */
        const float distance   = camera.distance();
        const float fov_rad    = camera.fov_y_degrees() * (3.14159265f / 180.0f);
        const float aspect     = camera.aspect_ratio();
        const float world_h    = 2.0f * distance * std::tan(fov_rad * 0.5f);
        const float world_w    = world_h * aspect;
        const float cx         = camera.target().x;
        const float cy         = camera.target().y;
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

void print_dataset_info(
    const gs3d::data::Gs3dDataset& dataset
) {
    std::cout << "[OK] Dataset loaded.\n";
    std::cout << "point_count = " << dataset.point_count() << '\n';
    std::cout << "loaded_point_bytes = "
              << dataset.point_bytes() << '\n';
    std::cout << "metadata_only = "
              << (dataset.metadata_only() ? "true" : "false")
              << '\n';

    std::cout << "bbox_min = ["
              << dataset.bbox_min_x() << ", "
              << dataset.bbox_min_y() << ", "
              << dataset.bbox_min_z() << "]\n";

    std::cout << "bbox_max = ["
              << dataset.bbox_max_x() << ", "
              << dataset.bbox_max_y() << ", "
              << dataset.bbox_max_z() << "]\n";

    std::cout << "value_range = ["
              << dataset.value_min() << ", "
              << dataset.value_max() << "]\n";
}

void print_controls(
    bool lod_enabled,
    bool tile_enabled
){
    std::cout << "[OK] Entering render loop.\n";
    std::cout << "操作说明：\n";
    std::cout << "  左键拖动：轨道旋转\n";
    std::cout << "  右键/中键拖动：平移\n";
    std::cout << "  滚轮：缩放\n";
    std::cout << "  + / -：调整点大小\n";
    std::cout << "  R：重置视图\n";
    std::cout << "  Tab：切换着色属性\n";
    std::cout << "  Esc：退出\n";
    std::cout << "渲染模式：\n";
    std::cout << "  LOD         : "
              << (lod_enabled ? "启用" : "关闭")
              << '\n';
    std::cout << "  全分辨率瓦片："
              << (tile_enabled ? "启用" : "关闭")
              << '\n';
}

// Round a world-space distance to a human-readable "nice" value:
//   1, 2, 5, 10, 20, 50, 100, 200, 500, 1000 …
// Follows the same algorithm used by Leaflet (BSD-2) and Cesium (Apache 2).
float nice_scale_distance(float raw)
{
    if (raw <= 0.0f) return 1.0f;
    // Guard against denormalized floats: log10(very small) -> pow underflow -> 0.
    constexpr float kMinRaw = 1.0e-30f;
    if (raw < kMinRaw) return kMinRaw;
    const double mag_d = std::pow(10.0, std::floor(std::log10(static_cast<double>(raw))));
    if (mag_d <= 0.0) return 1.0f;
    const float mag = static_cast<float>(mag_d);
    const float n   = raw / mag;
    if (n < 1.5f) return       mag;
    if (n < 3.5f) return 2.0f * mag;
    if (n < 7.5f) return 5.0f * mag;
    return 10.0f * mag;
}

// Assumes the dataset coordinate unit is metres.
std::string format_scale_distance(float d)
{
    char buf[48];
    if (d >= 1000.0f) {
        std::snprintf(buf, sizeof(buf), "%.0f 千米",
            static_cast<double>(d / 1000.0f));
    } else if (d >= 1.0f) {
        std::snprintf(buf, sizeof(buf), "%.0f 米",
            static_cast<double>(d));
    } else if (d >= 0.01f) {
        std::snprintf(buf, sizeof(buf), "%.0f 厘米",
            static_cast<double>(d * 100.0f));
    } else {
        std::snprintf(buf, sizeof(buf), "%.0f 毫米",
            static_cast<double>(d * 1000.0f));
    }
    return buf;
}

std::string format_vec3_text(const gs3d::camera::Vec3& value)
{
    char buf[96];
    std::snprintf(
        buf,
        sizeof(buf),
        "%.1f, %.1f, %.1f",
        static_cast<double>(value.x),
        static_cast<double>(value.y),
        static_cast<double>(value.z)
    );
    return buf;
}

constexpr double kTileSelectionDebounceSeconds = 0.12;
constexpr double kInteractingDebounceSeconds = 0.15;
constexpr int kMaxViewportCount = 4;

} // namespace

ViewerApp::ViewerApp(ViewerAppConfig config)
    : config_(std::move(config))
{
}

int ViewerApp::run() {
    try {
        gs3d::util::Stopwatch startup_timer;

        const bool can_start_from_metadata =
            config_.lod_enabled &&
            !config_.lod_keep_full_buffer &&
            config_.lod_auto_load_sidecar &&
            !config_.lod_sidecar_path.empty() &&
            std::filesystem::exists(config_.lod_sidecar_path);

        gs3d::util::Stopwatch dataset_load_timer;
        auto dataset = can_start_from_metadata
            ? gs3d::data::Gs3dDatasetLoader::load_header_only(
                config_.gs3d_path
            )
            : gs3d::data::Gs3dDatasetLoader::load(
                config_.gs3d_path
            );
        std::cout << "[TIME] viewer.dataset_load_seconds = "
                  << dataset_load_timer.elapsed_seconds()
                  << '\n';

        if (!dataset.is_consistent()) {
            std::cerr << "[FAIL] dataset is inconsistent.\n";
            return 1;
        }

        if (dataset.point_count() == 0) {
            std::cerr << "[FAIL] dataset is empty.\n";
            return 1;
        }

        print_dataset_info(dataset);

        std::optional<gs3d::data::Gs3dTileReader> tile_reader;
        gs3d::core::TileIndexView tile_index_view;

        if (config_.tile_enabled) {
            gs3d::util::Stopwatch tile_reader_timer;
            tile_reader =
                gs3d::data::Gs3dTileReader::open(
                    config_.tile_index_path,
                    config_.tile_data_path,
                    dataset.header()
                );
            std::cout << "[TIME] viewer.tile_reader_open_seconds = "
                      << tile_reader_timer.elapsed_seconds()
                      << '\n';

            if (!tile_reader->valid()) {
                std::cerr << "[FAIL] TileReader is invalid.\n";
                return 1;
            }

            const auto tile_stats =
                tile_reader->stats();

            std::cout << "[OK] TileReader opened.\n";
            std::cout << "tile_count = "
                      << tile_stats.tile_count
                      << '\n';
            std::cout << "tile_total_point_count = "
                      << tile_stats.total_point_count
                      << '\n';
            std::cout << "tile_total_point_bytes = "
                      << tile_stats.total_point_bytes
                      << '\n';

            tile_index_view =
                gs3d::data::make_tile_index_view(*tile_reader);
        }

        gs3d::data::Gs3dLodDataset lod_dataset;

        if (config_.lod_enabled) {
            gs3d::util::Stopwatch lod_timer;
            try {
                lod_dataset =
                    load_or_build_lod_dataset(
                        dataset,
                        config_
                    );
            } catch (const std::exception&) {
                if (!dataset.metadata_only()) {
                    throw;
                }

                std::cout
                    << "[WARN] Metadata-only startup cannot build LOD; "
                    << "loading full GS3D data.\n";
                gs3d::util::Stopwatch fallback_load_timer;
                dataset =
                    gs3d::data::Gs3dDatasetLoader::load(
                        config_.gs3d_path
                    );
                std::cout
                    << "[TIME] viewer.dataset_fallback_load_seconds = "
                    << fallback_load_timer.elapsed_seconds()
                    << '\n';
                lod_dataset =
                    load_or_build_lod_dataset(
                        dataset,
                        config_
                    );
            }
            std::cout << "[TIME] viewer.lod_prepare_seconds = "
                      << lod_timer.elapsed_seconds()
                      << '\n';
        }
        
        gs3d::platform::WindowConfig window_config;
        window_config.width = config_.window_width;
        window_config.height = config_.window_height;
        window_config.title = config_.window_title;
        window_config.resizable = config_.window_resizable;

        gs3d::platform::Window window(window_config);

        gs3d::render::VulkanContextConfig vk_config;
        vk_config.enable_validation_layers =
            config_.enable_validation_layers;
        vk_config.application_name = "GeoScatter3D";

        gs3d::render::VulkanContext context(window, vk_config);
        std::cout << "[TIME] viewer.startup_seconds = "
                  << startup_timer.elapsed_seconds()
                  << '\n';

        std::cout << "[OK] VulkanContext created.\n";
        std::cout << "Physical device: "
                  << context.physical_device_name() << '\n';

        gs3d::render::VulkanSwapchain swapchain(context, window);
        gs3d::render::VulkanRenderer renderer(context, swapchain);

        gs3d::gui::ImGuiLayer imgui_layer;
        imgui_layer.init(
            window.native_handle(),
            context,
            renderer,
            swapchain.image_count(),
            config_.ui_layout_ini_path
        );

        gs3d::render::ClearColor clear_color;
        clear_color.r = config_.clear_color[0];
        clear_color.g = config_.clear_color[1];
        clear_color.b = config_.clear_color[2];
        clear_color.a = config_.clear_color[3];
        renderer.set_clear_color(clear_color);

        std::unique_ptr<gs3d::render::PointCloudGpu> full_gpu_cloud;
        std::unique_ptr<gs3d::render::PointCloudLodGpu> lod_gpu_cloud;
        std::unique_ptr<gs3d::render::PointCloudTileGpu> tile_gpu_cloud;

        if (config_.lod_enabled) {
            const auto lod_source = build_lod_source(lod_dataset);
            lod_gpu_cloud =
                std::make_unique<gs3d::render::PointCloudLodGpu>(
                    context,
                    renderer.command_pool(),
                    context.graphics_queue(),
                    lod_source
                );

            std::cout << "[OK] PointCloudLodGpu uploaded.\n";
            std::cout << lod_gpu_cloud->summary();

        } else {
            full_gpu_cloud =
                std::make_unique<gs3d::render::PointCloudGpu>(
                    context,
                    renderer.command_pool(),
                    context.graphics_queue(),
                    gs3d::data::make_point_data_view(dataset)
                );

            std::cout << "[OK] PointCloudGpu uploaded.\n";
            std::cout << "gpu point_count = "
                      << full_gpu_cloud->point_count()
                      << '\n';
        }

        // Camera must be initialized before ViewportManager (which clones it).
        // Bounds are needed for fit-mode and for the CameraController.
        const gs3d::camera::CameraBounds bounds =
            make_camera_bounds(dataset);

        gs3d::camera::Camera initial_camera;
        VkExtent2D initial_viewport_extent = swapchain.extent();
        initial_camera.set_viewport(
            initial_viewport_extent.width,
            initial_viewport_extent.height
        );
        initialize_camera_from_config(initial_camera, config_, bounds);

        // ViewportManager: N (OffscreenFramebuffer, Camera) pairs.
        // All framebuffers use swapchain.image_format() → Vulkan-compatible with
        // each other, so a single PointPipeline works for all viewports.
        // Must be created after ImGui init (registers descriptors via AddTexture).
        gs3d::render::ViewportManager viewport_manager;
        viewport_manager.init(
            context,
            swapchain.image_format(),
            kMaxViewportCount,
            initial_viewport_extent,
            initial_camera
        );
        viewport_manager.set_active_count(
            std::clamp(config_.viewport_count, 1, kMaxViewportCount)
        );
        viewport_manager.set_clear_color(clear_color);

        gs3d::render::PointPipelineConfig pipeline_config;
        pipeline_config.vertex_shader_path =
            config_.vertex_shader_path;
        pipeline_config.fragment_shader_path =
            config_.fragment_shader_path;

        gs3d::render::PointPipeline point_pipeline(
            context,
            viewport_manager.render_pass(),
            pipeline_config
        );

        std::cout << "[OK] PointPipeline created.\n";

        gs3d::camera::CameraControllerConfig controller_config;
        controller_config.rotate_speed =
            config_.controller_rotate_speed;
        controller_config.pan_speed =
            config_.controller_pan_speed;
        controller_config.zoom_speed =
            config_.controller_zoom_speed;
        controller_config.invert_rotate_x =
            config_.controller_invert_rotate_x;
        controller_config.invert_rotate_y =
            config_.controller_invert_rotate_y;
        controller_config.invert_pan_x =
            config_.controller_invert_pan_x;
        controller_config.invert_pan_y =
            config_.controller_invert_pan_y;

        // One controller per viewport so each can be interacted with independently.
        std::vector<gs3d::camera::CameraController> controllers;
        controllers.reserve(
            static_cast<std::size_t>(viewport_manager.viewport_count())
        );
        for (int i = 0; i < viewport_manager.viewport_count(); ++i) {
            controllers.emplace_back(controller_config);
            controllers.back().set_bounds(bounds);
        }

        // Views start independent. The per-view UI can opt into sync group 0.
        gs3d::camera::CameraHub camera_hub;
        for (int i = 0; i < viewport_manager.viewport_count(); ++i) {
            camera_hub.add(
                i,
                &viewport_manager.camera(i),
                gs3d::camera::CameraHub::kIndependent
            );
        }

        std::cout << "[OK] CameraController initialized.\n";
        std::cout << "camera position = ["
                  << viewport_manager.camera(0).position().x << ", "
                  << viewport_manager.camera(0).position().y << ", "
                  << viewport_manager.camera(0).position().z << "]\n";

        std::cout << "camera target = ["
                  << viewport_manager.camera(0).target().x << ", "
                  << viewport_manager.camera(0).target().y << ", "
                  << viewport_manager.camera(0).target().z << "]\n";

        std::cout << "camera distance = "
                  << viewport_manager.camera(0).distance() << '\n';

        gs3d::render::PointPushConstants push{};
        push.point_size  = config_.initial_point_size;
        push.attr_index  = 0;   // default: value attribute (amplitude)
        push.value_min   = dataset.value_min();
        push.value_range = dataset.value_max() - dataset.value_min();
        if (push.value_range <= 0.0f) push.value_range = 1.0f;
        // MVP is set per-viewport inside render_all; clip_mode is zero-initialized.

        /*
         * Tracks the query box of the tile buffer currently on the GPU.
         * Used to clip LOD draws so LOD points don't overlap full-res tiles.
         * Reset when the tile buffer is cleared.
         */
        std::array<
            std::vector<std::uint64_t>,
            kMaxViewportCount
        > viewport_tile_ids;
        std::array<
            std::optional<gs3d::data::Gs3dTileQueryBox>,
            kMaxViewportCount
        > viewport_tile_query_boxes;

        /*
         * 异步磁盘读取（Potree/Cesium 模式）：
         * 后台线程读取 tile 数据，主线程每帧非阻塞检查 future 是否完成。
         * GPU upload 仍在主线程，调用前用 in-flight fence 代替 vkDeviceWaitIdle。
         */
        struct TileLoadResult {
            std::vector<std::uint64_t>          tile_ids;
            std::vector<std::pair<
                std::uint64_t,
                SharedTilePoints
            >>                                  tiles;
            gs3d::data::Gs3dTileQueryBox        actual_bbox;
            double                              read_seconds = 0.0;
            std::size_t                         cache_hit_tiles = 0;
            std::size_t                         cache_miss_tiles = 0;
            std::size_t                         candidate_tiles = 0;
        };

        TilePointCache tile_point_cache(
            config_.tile_cpu_cache_max_bytes
        );
        std::future<TileLoadResult> tile_load_future;
        std::optional<TileLoadResult> tile_upload_pending;
        // IDs dispatched to background thread (may differ from current selection)
        std::vector<std::uint64_t> tile_loading_ids;
        std::vector<std::uint64_t> debounced_tile_ids;
        auto tile_selection_changed_at =
            std::chrono::steady_clock::now();
        // Debounce interacting so rapid scroll zoom doesn't cause
        // frame-by-frame toggling (tiles pop in/out, LOD clip flicker).
        auto interacting_debounce_until =
            std::chrono::steady_clock::now();
        gs3d::util::Stopwatch tile_async_cycle_timer;

        /*
         * 全量预加载状态(tile_preload_all):后台一次性读取全部瓦片,主循环
         * 用大预算渐进上传到 GPU 常驻;全部驻留后 tiles_fully_resident=true,
         * 之后走"每帧选择可见子集、零加载"的快路径,不再碰流式状态机。
         * 仅在所有瓦片总字节 <= tile_preload_max_bytes 时启用,否则保持 false
         * 走原有按需流式。
         */
        const bool tile_preload_enabled =
            config_.tile_enabled &&
            config_.tile_preload_all &&
            !config_.benchmark_mode &&
            tile_reader.has_value() &&
            !tile_reader->records().empty() &&
            tile_reader->stats().total_point_bytes <=
                config_.tile_preload_max_bytes;
        std::future<std::vector<std::pair<
            std::uint64_t, SharedTilePoints>>> tile_preload_future;
        std::vector<std::pair<std::uint64_t, SharedTilePoints>>
            tile_preload_tiles;
        bool tile_preload_dispatched = false;
        bool tile_preload_failed = false;
        bool tiles_fully_resident = false;
        int tile_preload_stall_frames = 0;
        gs3d::util::Stopwatch tile_preload_timer;

        const auto make_cached_tile_views =
            [](const std::vector<std::pair<std::uint64_t, SharedTilePoints>>&
                   tiles) {
                std::vector<std::pair<
                    std::uint64_t,
                    gs3d::core::PointDataView
                >> views;
                views.reserve(tiles.size());
                for (const auto& [tile_id, points] : tiles) {
                    if (!points) {
                        continue;
                    }
                    views.emplace_back(
                        tile_id,
                        gs3d::data::make_point_data_view(*points)
                    );
                }
                return views;
            };

        // 从瓦片 id 列表算并集包围盒(供 clip 用)。
        const auto compute_tiles_bbox =
            [&tile_reader](const std::vector<std::uint64_t>& ids)
                -> gs3d::data::Gs3dTileQueryBox {
            gs3d::data::Gs3dTileQueryBox box;
            box.min_x = box.min_y = box.min_z =
                std::numeric_limits<float>::max();
            box.max_x = box.max_y = box.max_z =
                -std::numeric_limits<float>::max();
            for (const auto tile_id : ids) {
                const auto& r = tile_reader->record(tile_id);
                box.min_x = std::min(box.min_x, r.bbox_min_x);
                box.min_y = std::min(box.min_y, r.bbox_min_y);
                box.min_z = std::min(box.min_z, r.bbox_min_z);
                box.max_x = std::max(box.max_x, r.bbox_max_x);
                box.max_y = std::max(box.max_y, r.bbox_max_y);
                box.max_z = std::max(box.max_z, r.bbox_max_z);
            }
            return box;
        };

        ViewportResizeScheduler viewport_resize_scheduler(0.15);

        // Benchmark-mode instrumentation (no-ops when benchmark_mode is false).
        std::uint32_t benchmark_frame_index = 0;
        std::vector<double> benchmark_frame_times_ms;
        std::vector<double> benchmark_reload_seconds;
        if (config_.benchmark_mode) {
            benchmark_frame_times_ms.reserve(config_.benchmark_frame_count);
        }
        // Orbit for the first 2/3 of the run (measures interaction frame
        // time), then hold still for the last 1/3 (measures tile/LOD
        // settle/reload latency once the camera stops moving).
        const std::uint32_t benchmark_orbit_frames =
            config_.benchmark_frame_count * 2 / 3;

        gs3d::render::LodSelector lod_selector;

        if (config_.lod_enabled) {
            gs3d::render::LodSelectorConfig lod_selector_config;
            lod_selector_config.medium_delay_seconds =
                config_.lod_medium_delay_seconds;
            lod_selector_config.high_delay_seconds =
                config_.lod_high_delay_seconds;
            lod_selector_config.use_lowest_while_interacting =
                config_.lod_use_lowest_while_interacting;
            lod_selector_config.adaptive_interacting_level =
                config_.lod_adaptive_interacting_level;
            lod_selector_config.frame_time_budget_ms =
                config_.lod_frame_time_budget_ms;

            lod_selector.set_config(lod_selector_config);
        }

        gs3d::render::TileSelection tile_selection;
        gs3d::render::TileSelectionResult tile_result;
        bool tile_selection_dirty = true;
        int streaming_viewport_index = 0;

        if (config_.tile_enabled && tile_reader.has_value()) {
            tile_selection.set_config(
                make_tile_selection_config(config_)
            );

            tile_gpu_cloud =
                std::make_unique<gs3d::render::PointCloudTileGpu>();
            tile_gpu_cloud->set_resident_tile_budget(
                config_.tile_gpu_cache_max_tiles
            );

            std::cout << "[OK] TileSelection initialized.\n";
        }

        bool r_was_pressed = false;
        bool tab_was_pressed = false;

        /*
         * 多属性可视化（Potree activeAttributeName / CloudCompare scalar field）。
         * 所有属性数据已在 VBO 中，切换只改 push constant，零 GPU 重传。
         *
         * 当前支持的属性（对应比赛数据集的 fold/elevation 两个字段）：
         *   0 = value   (导入时存储的属性，本数据集是 fold)
         *   1 = z       (导入时的几何高度，本数据集是 elevation)
         */
        struct AttrDesc {
            const char* name;
            float min_val;
            float range;
        };
        const std::array<AttrDesc, 2> attr_table = {{
            { "Fold（褶皱）",
              dataset.value_min(),
              dataset.value_max() - dataset.value_min() },
            { "Elevation（高程）",
              dataset.bbox_min_z(),
              dataset.bbox_max_z() - dataset.bbox_min_z() }
        }};

        std::size_t last_lod_level =
             static_cast<std::size_t>(-1);
        // Hoisted out of the loop body so report_frame_time() can pair the
        // level rendered in frame N-1 with frame N-1's measured duration
        // (delta_seconds, computed at the top of frame N) before this
        // frame reassigns it.
        std::size_t lod_level_for_frame = 0;

        /*
         * 安全网：最后一次确认可用的 LOD 级别。select_level() 理论上
         * 永远返回有效值（LOD GPU cloud 所有级别都是启动时预上传的），
         * 但万一出现越界或无效索引，退回到 last_valid 而不是画空帧。
         */
        std::size_t last_valid_lod_level = 0;

        /*
         * KeepStableHighQuality 模式下,交互期间冻结的显示档位。空闲时持续
         * 刷新为当前稳定显示的档位(会收敛到 level 0 = 最高细节);交互开始
         * 时锁定该值,交互途中绝不切到比它更粗的档位。lower index = 更精细。
         */
        std::size_t frozen_display_lod = 0;

        const auto log_tile_upload =
            [this](
                const gs3d::render::PointCloudTileGpuStats& stats,
                const TileLoadResult& loaded,
                const gs3d::render::PointCloudTileGpuSyncResult& sync,
                double upload_seconds,
                double total_seconds
            ) {
                if (!config_.tile_verbose) {
                    return;
                }

                std::cout << "[TILE] async upload complete.\n";
                std::cout << "tile_count = "
                          << stats.tile_count << '\n';
                std::cout << "candidate_tile_count = "
                          << loaded.candidate_tiles << '\n';
                std::cout << "point_count = "
                          << stats.point_count << '\n';
                std::cout << "gpu_buffer_bytes = "
                          << stats.gpu_buffer_bytes << '\n';
                std::cout << "resident_tile_count = "
                          << stats.resident_tile_count << '\n';
                std::cout << "uploaded_tile_count = "
                          << sync.uploaded_tile_count << '\n';
                std::cout << "uploaded_point_count = "
                          << sync.uploaded_point_count << '\n';
                std::cout << "uploaded_bytes = "
                          << sync.uploaded_bytes << '\n';
                std::cout << "upload_complete = "
                          << (sync.complete ? "true" : "false")
                          << '\n';
                std::cout << "cache_hit_tiles = "
                          << loaded.cache_hit_tiles << '\n';
                std::cout << "cache_miss_tiles = "
                          << loaded.cache_miss_tiles << '\n';
                std::cout << "[TIME] tile.async_read_seconds = "
                          << loaded.read_seconds << '\n';
                std::cout << "[TIME] tile.async_upload_seconds = "
                          << upload_seconds << '\n';
                std::cout << "[TIME] tile.async_total_seconds = "
                          << total_seconds << '\n';
            };

        auto previous_time =
            std::chrono::steady_clock::now();

        // Smoothed FPS via exponential moving average
        float fps_smooth = 0.0f;

        print_controls(
                        config_.lod_enabled,
                        config_.tile_enabled
                    );

        gs3d::scene::SceneState scene_state;
        scene_state.dataset.active_dataset =
            config_.gs3d_path.filename().string();
        scene_state.dataset.path = config_.gs3d_path.string();
        scene_state.dataset.format = "GS3D";
        scene_state.dataset.point_count = dataset.point_count();
        scene_state.dataset.bounding_box =
            "[" + std::to_string(dataset.bbox_min_x()) + ", " +
            std::to_string(dataset.bbox_min_y()) + ", " +
            std::to_string(dataset.bbox_min_z()) + "] -> [" +
            std::to_string(dataset.bbox_max_x()) + ", " +
            std::to_string(dataset.bbox_max_y()) + ", " +
            std::to_string(dataset.bbox_max_z()) + "]";
        scene_state.dataset.dataset_tree = {
            scene_state.dataset.active_dataset,
            "瓦片",
            "细节层级",
            "属性"
        };
        scene_state.dataset.attributes.clear();
        for (const auto& attr : attr_table) {
            scene_state.dataset.attributes.emplace_back(attr.name);
        }
        {
            std::error_code ec;
            const auto file_bytes = std::filesystem::file_size(config_.gs3d_path, ec);
            if (!ec) {
                const double mb = static_cast<double>(file_bytes) / (1024.0 * 1024.0);
                std::ostringstream oss;
                oss.setf(std::ios::fixed);
                oss.precision(2);
                oss << mb << " MB";
                scene_state.dataset.file_size = oss.str();
            }
        }
        gs3d::app::AppState app_state;
        app_state.dataset.active_dataset = scene_state.dataset.active_dataset;
        app_state.dataset.path = scene_state.dataset.path;
        app_state.dataset.format = scene_state.dataset.format;
        app_state.dataset.point_count = scene_state.dataset.point_count;
        app_state.dataset.loaded_points = scene_state.dataset.point_count;
        app_state.dataset.file_size = scene_state.dataset.file_size;
        app_state.dataset.bounding_box = scene_state.dataset.bounding_box;
        app_state.dataset.dataset_tree = scene_state.dataset.dataset_tree;
        app_state.dataset.attributes = scene_state.dataset.attributes;
        app_state.render_settings.color_by_options =
            scene_state.dataset.attributes;
        app_state.render_views.resize(
            static_cast<std::size_t>(viewport_manager.viewport_count())
        );
        const int startup_view_count =
            std::clamp(config_.viewport_count, 1, kMaxViewportCount);
        for (int i = 0; i < viewport_manager.viewport_count(); ++i) {
            auto& view =
                app_state.render_views[static_cast<std::size_t>(i)];
            view.viewport_index = i;
            view.visible = i < startup_view_count;
            view.camera_linked = false;
        }
        std::vector<int> visible_viewports;
        visible_viewports.reserve(
            static_cast<std::size_t>(viewport_manager.viewport_count())
        );

        while (!window.should_close() &&
               (!config_.benchmark_mode ||
                benchmark_frame_index < config_.benchmark_frame_count)) {
            gs3d::util::Stopwatch benchmark_frame_timer;
            const auto current_time =
                std::chrono::steady_clock::now();

            const double delta_seconds =
                std::chrono::duration<double>(
                    current_time - previous_time
                ).count();

            previous_time = current_time;

            // Pairs the level rendered last frame with its measured
            // duration, driving LodSelectorConfig::adaptive_interacting_level
            // (no-op otherwise). Must run before lod_level_for_frame is
            // reassigned for *this* frame, further down.
            if (config_.lod_enabled && delta_seconds > 0.0) {
                /*
                 * On VK_PRESENT_MODE_FIFO_KHR the total wall-clock frame
                 * time includes vsync present-wait inside vkAcquireNextImageKHR
                 * (~16.67ms at 60Hz).  Subtracting that wait gives a better
                 * estimate of actual GPU render time so the adaptive LOD
                 * budget (14ms) doesn't silently pin to the lowest level on
                 * FIFO-only systems (iGPU / older hardware).
                 */
                const double present_wait_ms =
                    renderer.last_acquire_wait_ms();
                const double report_ms =
                    (delta_seconds * 1000.0) - present_wait_ms;
                lod_selector.report_frame_time(
                    lod_level_for_frame,
                    report_ms > 0.0 ? report_ms : 0.0
                );
            }

            window.poll_events();

            // Update FPS (exponential moving average, α=0.1)
            if (delta_seconds > 0.0) {
                const float frame_fps =
                    static_cast<float>(1.0 / delta_seconds);
                fps_smooth = fps_smooth > 0.0f
                    ? fps_smooth * 0.9f + frame_fps * 0.1f
                    : frame_fps;
            }

            const int n_viewports = viewport_manager.viewport_count();
            const auto& primary_camera =
                viewport_manager.camera(streaming_viewport_index);

            std::uint32_t loaded_tiles = 0;
            std::size_t pending_tile_count = tile_loading_ids.size();
            if (tile_upload_pending.has_value()) {
                const auto selected =
                    tile_upload_pending->tile_ids.size();
                const auto uploaded =
                    tile_gpu_cloud
                        ? tile_gpu_cloud->loaded_tile_ids().size()
                        : 0;
                pending_tile_count =
                    std::max(
                        pending_tile_count,
                        selected > uploaded
                            ? selected - uploaded
                            : std::size_t{0}
                    );
            }
            const std::uint32_t pending_tiles =
                static_cast<std::uint32_t>(
                    std::min<std::size_t>(
                        pending_tile_count,
                        std::numeric_limits<std::uint32_t>::max()
                    )
                );
            std::uint64_t gpu_buffer_bytes = 0;
            std::uint64_t visible_points = dataset.point_count();

            if (config_.tile_enabled && tile_gpu_cloud) {
                const auto& ts = tile_gpu_cloud->stats();
                loaded_tiles =
                    static_cast<std::uint32_t>(
                        ts.resident_tile_count
                    );
                gpu_buffer_bytes = ts.gpu_buffer_bytes;
                visible_points = ts.point_count > 0 ? ts.point_count : dataset.point_count();
            } else if (full_gpu_cloud) {
                gpu_buffer_bytes =
                    static_cast<std::uint64_t>(full_gpu_cloud->vertex_buffer_size());
            } else if (lod_gpu_cloud) {
                for (std::size_t i = 0; i < lod_gpu_cloud->level_count(); ++i) {
                    gpu_buffer_bytes += static_cast<std::uint64_t>(
                        lod_gpu_cloud->gpu_cloud(i).vertex_buffer_size()
                    );
                }
            }

            app_state.dataset.point_count = dataset.point_count();
            app_state.dataset.loaded_points = visible_points;
            app_state.render_settings.point_size = push.point_size;
            app_state.render_settings.color_by_index = static_cast<int>(push.attr_index);
            app_state.render_settings.loaded_tiles = loaded_tiles;
            app_state.render_settings.pending_tiles = pending_tiles;
            app_state.render_settings.cache_usage =
                std::to_string(loaded_tiles) + " / " +
                std::to_string(config_.tile_gpu_cache_max_tiles);
            const auto tile_cache_stats = tile_point_cache.stats();
            app_state.render_settings.cpu_cache_usage =
                std::to_string(
                    tile_cache_stats.resident_bytes /
                    (1024ull * 1024ull)
                ) + " / " +
                std::to_string(
                    tile_cache_stats.max_bytes /
                    (1024ull * 1024ull)
                ) + " MB";
            const auto tile_cache_requests =
                tile_cache_stats.hits + tile_cache_stats.misses;
            app_state.render_settings.cache_hit_rate =
                tile_cache_requests > 0
                    ? 100.0f * static_cast<float>(
                        tile_cache_stats.hits
                    ) /
                        static_cast<float>(tile_cache_requests)
                    : 0.0f;

            app_state.performance.fps = fps_smooth;
            app_state.performance.frame_time_ms =
                delta_seconds > 0.0 ? static_cast<float>(delta_seconds * 1000.0) : 0.0f;
            app_state.performance.visible_points = visible_points;
            app_state.performance.total_points = dataset.point_count();
            app_state.performance.loaded_tiles = loaded_tiles;
            app_state.performance.pending_tiles = pending_tiles;
            app_state.performance.gpu_memory_bytes = gpu_buffer_bytes;
            app_state.performance.lod_mode =
                config_.lod_enabled
                    ? "已启用细节层级"
                    : "全分辨率";

            app_state.status_bar.fps = fps_smooth;
            app_state.status_bar.visible_points = visible_points;
            app_state.status_bar.loaded_tiles = loaded_tiles;
            app_state.status_bar.pending_tiles = pending_tiles;
            app_state.status_bar.gpu_memory_bytes = gpu_buffer_bytes;
            app_state.status_bar.camera_position = format_vec3_text(primary_camera.position());
            app_state.status_bar.crs = "本地坐标 / 未知";
            app_state.status_bar.ready_state = "就绪";

            for (int i = 0; i < n_viewports; ++i) {
                const auto& camera = viewport_manager.camera(i);
                auto& view =
                    app_state.render_views[static_cast<std::size_t>(i)];
                view.viewport_index = i;
                view.descriptor =
                    viewport_manager.framebuffer(i).imgui_descriptor();
                view.show_live_image = view.descriptor != VK_NULL_HANDLE;
                view.image_width = camera.viewport_width();
                view.image_height = camera.viewport_height();
                view.points_visible = visible_points;
                view.points_total = dataset.point_count();
                view.frame_time_ms = app_state.performance.frame_time_ms;
                view.camera_mode = "轨道";
                view.position = format_vec3_text(camera.position());
                view.fov = camera.fov_y_degrees();

                const float vp_h =
                    static_cast<float>(camera.viewport_height());
                const float d = camera.distance();
                const float fov_rad =
                    camera.fov_y_degrees() * (3.14159265f / 180.0f);
                float scale_world = 500.0f;
                if (vp_h > 1.0f) {
                    const float pixel_world = 2.0f * d * std::tan(fov_rad * 0.5f) / vp_h;
                    if (pixel_world > 0.0f) {
                        scale_world = nice_scale_distance(pixel_world * 96.0f);
                    }
                }
                view.scale = format_scale_distance(scale_world);

                compute_axis_overlay(
                    view,
                    bounds,
                    camera,
                    dataset.origin_x(),
                    dataset.origin_y(),
                    dataset.origin_z()
                );

                compute_map_axis_overlay(
                    view,
                    camera,
                    dataset.origin_x(),
                    dataset.origin_y()
                );

                compute_gizmo_axes(view, camera);
            }

            const auto gui_cmds = imgui_layer.new_frame(app_state);
            const double now_seconds =
                std::chrono::duration<double>(
                    current_time.time_since_epoch()
                ).count();
            for (const auto& frame : gui_cmds.viewport_frames) {
                viewport_resize_scheduler.observe(
                    frame.index,
                    frame.width,
                    frame.height,
                    now_seconds
                );
            }

            visible_viewports.clear();
            for (const auto& view : app_state.render_views) {
                if (view.render_requested) {
                    visible_viewports.push_back(view.viewport_index);
                }
            }

            const bool imgui_wants_keyboard =
                ImGui::GetIO().WantCaptureKeyboard;

            // Apply GUI panel commands (Polyscope pattern: UI produces commands,
            // main loop applies them — keeps UI and app logic decoupled).
            if (gui_cmds.reset_camera_index >= 0 &&
                gui_cmds.reset_camera_index < n_viewports) {
                initialize_camera_from_config(
                    viewport_manager.camera(gui_cmds.reset_camera_index),
                    config_,
                    bounds
                );
                camera_hub.propagate(gui_cmds.reset_camera_index);
                streaming_viewport_index =
                    gui_cmds.reset_camera_index;
                tile_selection_dirty = true;
            }
            if (gui_cmds.point_size_changed) {
                push.point_size = std::clamp(gui_cmds.point_size, 1.0f, 10.0f);
            }
            if (gui_cmds.color_by_changed) {
                const int new_idx = std::clamp(
                    gui_cmds.color_by_index, 0,
                    static_cast<int>(attr_table.size()) - 1
                );
                scene_state.filters.active_attribute_index = new_idx;
                push.attr_index  = static_cast<std::uint32_t>(new_idx);
                const auto& a    = attr_table[push.attr_index];
                push.value_min   = a.min_val;
                push.value_range = a.range;
                if (push.value_range <= 0.0f) push.value_range = 1.0f;
                std::cout << "[ATTR] switched to: " << a.name << '\n';
            }
            if (gui_cmds.clear_cache_requested) {
                if (tile_preload_enabled && !tiles_fully_resident &&
                    !tile_preload_failed) {
                    // Cancel preload so clear() isn't immediately defeated
                    // by the background thread re-inserting tiles.
                    tile_preload_failed = true;
                    if (tile_preload_future.valid()) {
                        tile_preload_future.wait();
                    }
                    std::cout << "[TILE] preload cancelled for cache clear.\n";
                }
                tile_point_cache.clear();
                std::cout << "[TILE] CPU cache cleared.\n";
            }

            if (!imgui_wants_keyboard && window.key_pressed(GLFW_KEY_ESCAPE)) {
                window.request_close();
            }

            if (!imgui_wants_keyboard) {
                if (window.key_pressed(GLFW_KEY_EQUAL) ||
                    window.key_pressed(GLFW_KEY_KP_ADD)) {
                    push.point_size = std::min(
                        push.point_size + 0.05f,
                        10.0f
                    );
                }

                if (window.key_pressed(GLFW_KEY_MINUS) ||
                    window.key_pressed(GLFW_KEY_KP_SUBTRACT)) {
                    push.point_size = std::max(
                        push.point_size - 0.05f,
                        1.0f
                    );
                }
            }

            const bool r_pressed =
                !imgui_wants_keyboard &&
                (window.key_pressed(GLFW_KEY_R) ||
                 ImGui::IsKeyPressed(ImGuiKey_R, false));

            if (r_pressed && !r_was_pressed) {
                initialize_camera_from_config(
                    viewport_manager.camera(streaming_viewport_index),
                    config_,
                    bounds
                );
                camera_hub.propagate(streaming_viewport_index);
                tile_selection_dirty = true;
            }

            r_was_pressed = r_pressed;

            // Tab: cycle through color attributes (zero GPU cost — push constant only)
            const bool tab_pressed =
                !imgui_wants_keyboard && window.key_pressed(GLFW_KEY_TAB);
            if (tab_pressed && !tab_was_pressed) {
                push.attr_index =
                    (push.attr_index + 1u) %
                    static_cast<std::uint32_t>(attr_table.size());
                scene_state.filters.active_attribute_index =
                    static_cast<int>(push.attr_index);

                const auto& a = attr_table[push.attr_index];
                push.value_min   = a.min_val;
                push.value_range = a.range;
                if (push.value_range <= 0.0f) push.value_range = 1.0f;

                std::cout << "[ATTR] switched to: " << a.name << '\n';
            }
            tab_was_pressed = tab_pressed;

            for (const auto& view : app_state.render_views) {
                camera_hub.set_group(
                    view.viewport_index,
                    view.camera_linked
                        ? 0
                        : gs3d::camera::CameraHub::kIndependent
                );
            }

            bool interacting = false;
            bool camera_changed = false;
            for (const auto& frame : gui_cmds.viewport_frames) {
                if (frame.index < 0 ||
                    frame.index >= static_cast<int>(controllers.size())) {
                    continue;
                }

                if ((frame.hovered || frame.active) &&
                    streaming_viewport_index != frame.index) {
                    streaming_viewport_index = frame.index;
                    tile_selection_dirty = true;
                }

                if (frame.box_select_completed) {
                    auto& box_camera = viewport_manager.camera(frame.index);
                    const gs3d::camera::Viewport mouse_viewport{
                        frame.width, frame.height
                    };

                    // Anchor the selection plane to the REAL surface height
                    // under the dragged rectangle, not the orbit target's Z
                    // — for this viewer's height-field data, elevation
                    // relief can span roughly half the X/Y extent, so the
                    // target's Z can sit far from the local terrain height
                    // after panning, which previously made the zoomed-to
                    // location visibly disagree with the dragged rectangle.
                    std::vector<gs3d::core::PointDataView>
                        box_select_candidate_points;
                    if (config_.lod_enabled && lod_gpu_cloud &&
                        lod_level_for_frame < lod_dataset.level_count()) {
                        box_select_candidate_points.push_back(
                            gs3d::data::make_point_data_view(
                                lod_dataset.level(lod_level_for_frame).points
                            )
                        );
                    } else if (!config_.lod_enabled && dataset.has_point_data()) {
                        box_select_candidate_points.push_back(
                            gs3d::data::make_point_data_view(dataset)
                        );
                    }

                    const float rect_center_x =
                        0.5f * (frame.box_select_min_x + frame.box_select_max_x);
                    const float rect_center_y =
                        0.5f * (frame.box_select_min_y + frame.box_select_max_y);
                    const float rect_radius_px = 0.5f * std::sqrt(
                        (frame.box_select_max_x - frame.box_select_min_x) *
                            (frame.box_select_max_x - frame.box_select_min_x) +
                        (frame.box_select_max_y - frame.box_select_min_y) *
                            (frame.box_select_max_y - frame.box_select_min_y)
                    );
                    const auto anchor_hit =
                        gs3d::render::find_nearest_point_on_screen(
                            box_select_candidate_points,
                            rect_center_x,
                            rect_center_y,
                            mouse_viewport,
                            box_camera,
                            std::max(rect_radius_px, 12.0f)
                        );
                    const float plane_z =
                        anchor_hit ? anchor_hit->point.z : box_camera.target().z;

                    const auto selection_bounds =
                        gs3d::camera::box_select_world_bounds(
                            frame.box_select_min_x,
                            frame.box_select_min_y,
                            frame.box_select_max_x,
                            frame.box_select_max_y,
                            mouse_viewport,
                            box_camera,
                            bounds,
                            plane_z
                        );
                    if (selection_bounds) {
                        box_camera.fit_bounds(*selection_bounds);
                        camera_changed = true;
                        streaming_viewport_index = frame.index;
                        camera_hub.propagate(frame.index);
                    }
                }

                gs3d::camera::CameraInput input;
                input.viewport_width = frame.width;
                input.viewport_height = frame.height;
                input.delta_x = frame.mouse_delta_x;
                input.delta_y = frame.mouse_delta_y;
                input.scroll_y = frame.mouse_wheel;
                input.mouse_x = frame.mouse_local_x;
                input.mouse_y = frame.mouse_local_y;
                input.rotate = frame.rotate;
                input.pan = frame.pan;

                interacting = interacting || input.interacting();
                if (controllers[static_cast<std::size_t>(frame.index)]
                        .update(
                            viewport_manager.camera(frame.index),
                            input
                        )) {
                    camera_changed = true;
                    streaming_viewport_index = frame.index;
                    camera_hub.propagate(frame.index);
                }
            }

            if (config_.benchmark_mode &&
                benchmark_frame_index < benchmark_orbit_frames) {
                // Orbit + a slow zoom-in so the visible region actually
                // shrinks — a pure yaw orbit at a fixed distance can leave
                // the whole bbox in view the entire time, never forcing a
                // different tile selection, which would starve the reload-
                // latency measurement below.
                auto& bench_camera =
                    viewport_manager.camera(streaming_viewport_index);
                bench_camera.orbit(0.01f, 0.0f);
                bench_camera.zoom(0.999f);
                camera_hub.propagate(streaming_viewport_index);
                interacting = true;
                camera_changed = true;
            }

            if (camera_changed) {
                tile_selection_dirty = true;
            }

            /*
             * Debounce the interacting signal so rapid scroll zoom doesn't
             * cause frame-by-frame oscillation between true/false. Without
             * this, fast mouse-wheel scrolling produces frames where
             * scroll_y is 0 between discrete wheel events, briefly flipping
             * interacting to false.  That would:
             *   - toggle tiles on then off (tile_will_render)
             *   - toggle LOD clip bbox on then off
             *   - trigger tile selection update mid-scroll
             * …all of which cause visible flicker.
             *
             * The debounce keeps interacting=true for kInteractingDebounceSeconds
             * after the last real interacting frame, bridging the gaps between
             * discrete scroll events.
             */
            if (interacting) {
                interacting_debounce_until =
                    current_time +
                    std::chrono::milliseconds(
                        static_cast<long>(
                            kInteractingDebounceSeconds * 1000.0
                        )
                    );
            } else if (current_time < interacting_debounce_until) {
                interacting = true;
            }

            if (config_.lod_enabled) {
                lod_selector.update(
                    interacting,
                    delta_seconds
                );
            }

            /*
             * 全量预加载阶段:后台一次性读取全部瓦片,主循环用大预算逐帧
             * 上传到 GPU 常驻。全部驻留后切到快路径(下方),不再走流式。
             */
            if (tile_preload_enabled && !tiles_fully_resident &&
                !tile_preload_failed && tile_gpu_cloud) {
                if (!tile_preload_dispatched) {
                    tile_preload_dispatched = true;
                    tile_preload_timer.reset();
                    const auto& reader = *tile_reader;
                    tile_preload_future = std::async(
                        std::launch::async,
                        [&reader, &tile_point_cache]()
                            -> std::vector<std::pair<
                                std::uint64_t, SharedTilePoints>> {
                            std::vector<std::pair<
                                std::uint64_t, SharedTilePoints>> all;
                            all.reserve(reader.records().size());
                            for (const auto& rec : reader.records()) {
                                // Use find() (shared lock) for the check to
                                // avoid serializing against main-thread
                                // find()/stats() calls. Only put() (exclusive)
                                // on cache miss.
                                SharedTilePoints pts =
                                    tile_point_cache.find(rec.tile_id);
                                if (!pts) {
                                    auto loaded =
                                        std::make_shared<TilePoints>(
                                            reader.read_tile_points(
                                                rec.tile_id));
                                    tile_point_cache.put(rec.tile_id, loaded);
                                    pts = loaded;
                                }
                                all.emplace_back(rec.tile_id, pts);
                            }
                            return all;
                        });
                }

                if (tile_preload_tiles.empty() &&
                    tile_preload_future.valid() &&
                    tile_preload_future.wait_for(std::chrono::seconds(0)) ==
                        std::future_status::ready) {
                    try {
                        tile_preload_tiles = tile_preload_future.get();
                    } catch (const std::exception& e) {
                        std::cerr
                            << "[TILE] preload failed: " << e.what()
                            << " — falling back to streaming mode.\n";
                        tile_preload_failed = true;
                    }
                }

                if (!tile_preload_tiles.empty()) {
                    renderer.wait_for_in_flight_fences();
                    const auto preload_views =
                        make_cached_tile_views(tile_preload_tiles);
                    const auto sync =
                        tile_gpu_cloud->sync_from_cached_tiles(
                            context,
                            renderer.command_pool(),
                            context.graphics_queue(),
                            preload_views,
                            config_.tile_preload_upload_budget_bytes
                        );
                    if (config_.tile_verbose && sync.uploaded_bytes > 0) {
                        std::cout
                            << "[TILE] preload upload: bytes="
                            << sync.uploaded_bytes
                            << ", resident="
                            << sync.resident_tile_count << "/"
                            << tile_preload_tiles.size()
                            << ", complete="
                            << (sync.complete ? "true" : "false")
                            << '\n';
                    }
                    if (sync.complete) {
                        tiles_fully_resident = true;
                        tile_selection_dirty = true;
                        std::cout
                            << "[TILE] preload complete: "
                            << sync.resident_tile_count
                            << " tiles resident on GPU ("
                            << sync.resident_gpu_buffer_bytes
                            << " bytes) in "
                            << tile_preload_timer.elapsed_seconds()
                            << "s — interactive streaming disabled.\n";
                    } else if (sync.uploaded_bytes == 0) {
                        ++tile_preload_stall_frames;
                        if (tile_preload_stall_frames >= 3) {
                            std::cerr
                                << "[TILE] preload stalled ("
                                << sync.resident_tile_count << "/"
                                << tile_preload_tiles.size()
                                << " tiles uploaded, budget="
                                << config_.tile_preload_upload_budget_bytes
                                << " bytes/frame)"
                                << " — falling back to streaming.\n";
                            tile_preload_failed = true;
                        }
                    } else {
                        tile_preload_stall_frames = 0;
                    }
                }
            }

            /*
             * 快路径:全部瓦片已常驻 GPU。瓦片选择(纯 CPU frustum/屏幕尺寸,
             * 无 I/O)每帧都跑,包括交互期间,把可见子集直接设为绘制集——
             * 无异步读、无上传节流、无去抖等待。绘制仍只画可见子集,开销有界。
             */
            if (tiles_fully_resident) {
                if (camera_changed || tile_selection_dirty) {
                    tile_result = tile_selection.update(
                        viewport_manager.camera(streaming_viewport_index),
                        tile_index_view
                    );
                    tile_selection_dirty = false;
                    const auto view_index =
                        static_cast<std::size_t>(streaming_viewport_index);
                    if (tile_result.enabled &&
                        !tile_result.tile_ids.empty()) {
                        viewport_tile_ids[view_index] =
                            tile_result.tile_ids;
                        viewport_tile_query_boxes[view_index] =
                            compute_tiles_bbox(tile_result.tile_ids);
                    } else {
                        viewport_tile_ids[view_index].clear();
                        viewport_tile_query_boxes[view_index].reset();
                    }
                }
            } else if (config_.tile_enabled &&
                tile_reader.has_value() &&
                tile_gpu_cloud &&
                (!tile_preload_enabled || tile_preload_failed)) {
                if (!interacting && tile_selection_dirty) {
                    tile_result = tile_selection.update(
                        viewport_manager.camera(streaming_viewport_index),
                        tile_index_view
                    );
                    tile_selection_dirty = false;
                    if (tile_result.changed) {
                        debounced_tile_ids = tile_result.tile_ids;
                        tile_selection_changed_at = current_time;
                        /*
                         * Anti-flicker (docs/benchmark/flicker-audit.md
                         * Task 5/6): do NOT clear viewport_tile_ids here.
                         * The previously-displayed tiles are still resident
                         * on the GPU (evict_to_budget only drops tiles once
                         * the *new* selection has finished uploading) and
                         * stay valid to keep drawing. Clearing them the
                         * instant the selection changes — before the async
                         * read+upload for the new selection completes —
                         * caused a visible "detail drops to LOD, then pops
                         * back" cycle on every camera-settle event, lasting
                         * as long as the reload (0.2-0.9s on the 33M-point
                         * baseline). The swap to the new tile set happens
                         * below once `sync.complete` is true, not here.
                         */
                        if (tile_upload_pending.has_value() &&
                            tile_upload_pending->tile_ids !=
                                tile_result.tile_ids) {
                            tile_upload_pending.reset();
                        }
                    }
                }

                if (tile_load_future.valid() &&
                    tile_load_future.wait_for(std::chrono::seconds(0))
                        == std::future_status::ready) {
                    auto loaded = tile_load_future.get();
                    tile_load_future = {};
                    tile_loading_ids.clear();
                    if (!tile_selection_dirty &&
                        tile_result.enabled &&
                        loaded.tile_ids == tile_result.tile_ids) {
                        tile_upload_pending = std::move(loaded);
                    }
                }

                if (!tile_selection_dirty && !tile_result.enabled) {
                    debounced_tile_ids.clear();
                    tile_upload_pending.reset();
                    const auto view_index =
                        static_cast<std::size_t>(
                            streaming_viewport_index
                        );
                    viewport_tile_ids[view_index].clear();
                    viewport_tile_query_boxes[view_index].reset();
                } else if (tile_result.enabled && !interacting) {
                    if (tile_upload_pending.has_value() &&
                        tile_upload_pending->tile_ids ==
                            tile_result.tile_ids) {
                        gs3d::util::Stopwatch upload_timer;
                        renderer.wait_for_in_flight_fences();
                        const auto upload_views =
                            make_cached_tile_views(tile_upload_pending->tiles);
                        const auto sync =
                            tile_gpu_cloud->sync_from_cached_tiles(
                                context,
                                renderer.command_pool(),
                                context.graphics_queue(),
                                upload_views,
                                config_.tile_gpu_upload_budget_bytes
                            );
                        const double upload_seconds =
                            upload_timer.elapsed_seconds();
                        if (config_.tile_verbose &&
                            sync.uploaded_bytes > 0) {
                            std::cout
                                << "[TILE] upload slice: bytes="
                                << sync.uploaded_bytes
                                << ", seconds="
                                << upload_seconds
                                << ", complete="
                                << (sync.complete ? "true" : "false")
                                << '\n';
                        }

                        if (sync.complete) {
                            const auto view_index =
                                static_cast<std::size_t>(
                                    streaming_viewport_index
                                );
                            viewport_tile_ids[view_index] =
                                tile_upload_pending->tile_ids;
                            viewport_tile_query_boxes[view_index] =
                                tile_upload_pending->actual_bbox;
                            const double reload_total_seconds =
                                tile_async_cycle_timer.elapsed_seconds();
                            log_tile_upload(
                                tile_gpu_cloud->stats(),
                                *tile_upload_pending,
                                sync,
                                upload_seconds,
                                reload_total_seconds
                            );
                            if (config_.benchmark_mode) {
                                benchmark_reload_seconds.push_back(
                                    reload_total_seconds
                                );
                            }
                            tile_upload_pending.reset();
                            tile_loading_ids.clear();
                        } else {
                            viewport_tile_query_boxes[
                                static_cast<std::size_t>(
                                    streaming_viewport_index
                                )
                            ].reset();
                        }
                    }

                    const auto& loaded_ids =
                        viewport_tile_ids[
                            static_cast<std::size_t>(
                                streaming_viewport_index
                            )
                        ];
                    const bool buffer_stale =
                        loaded_ids.size() != tile_result.tile_ids.size() ||
                        !std::equal(
                            loaded_ids.begin(),
                            loaded_ids.end(),
                            tile_result.tile_ids.begin()
                        );
                    const bool load_in_progress =
                        tile_load_future.valid();
                    const bool upload_in_progress =
                        tile_upload_pending.has_value();
                    const double selection_stable_seconds =
                        std::chrono::duration<double>(
                            current_time - tile_selection_changed_at
                        ).count();
                    const bool selection_stable =
                        selection_stable_seconds >=
                        kTileSelectionDebounceSeconds;

                    if (buffer_stale &&
                        !load_in_progress &&
                        !upload_in_progress &&
                        selection_stable &&
                        !debounced_tile_ids.empty() &&
                        tile_loading_ids != debounced_tile_ids) {
                        gs3d::data::Gs3dTileQueryBox actual_bbox;
                        actual_bbox.min_x = actual_bbox.min_y =
                            actual_bbox.min_z =
                                std::numeric_limits<float>::max();
                        actual_bbox.max_x = actual_bbox.max_y =
                            actual_bbox.max_z =
                                -std::numeric_limits<float>::max();
                        for (const auto tile_id : debounced_tile_ids) {
                            const auto& record =
                                tile_reader->record(tile_id);
                            actual_bbox.min_x = std::min(
                                actual_bbox.min_x,
                                record.bbox_min_x
                            );
                            actual_bbox.min_y = std::min(
                                actual_bbox.min_y,
                                record.bbox_min_y
                            );
                            actual_bbox.min_z = std::min(
                                actual_bbox.min_z,
                                record.bbox_min_z
                            );
                            actual_bbox.max_x = std::max(
                                actual_bbox.max_x,
                                record.bbox_max_x
                            );
                            actual_bbox.max_y = std::max(
                                actual_bbox.max_y,
                                record.bbox_max_y
                            );
                            actual_bbox.max_z = std::max(
                                actual_bbox.max_z,
                                record.bbox_max_z
                            );
                        }

                        tile_loading_ids = debounced_tile_ids;
                        const auto ids = debounced_tile_ids;
                        const auto candidate_tile_count =
                            static_cast<std::size_t>(
                                tile_result.total_candidate_tiles
                            );
                        const auto& reader = *tile_reader;
                        tile_async_cycle_timer.reset();

                        std::vector<std::pair<
                            std::size_t,
                            std::uint64_t
                        >> missing_tiles;
                        std::vector<SharedTilePoints>
                            selected_tile_points(ids.size());
                        std::size_t cache_hit_tiles = 0;
                        for (std::size_t i = 0; i < ids.size(); ++i) {
                            // Use get() (not find()) to promote tile in LRU —
                            // actively rendered tiles must stay warm in cache.
                            selected_tile_points[i] =
                                tile_point_cache.get(ids[i]);
                            if (selected_tile_points[i]) {
                                ++cache_hit_tiles;
                            } else {
                                missing_tiles.emplace_back(i, ids[i]);
                            }
                        }

                        if (missing_tiles.empty()) {
                            TileLoadResult cached;
                            cached.tile_ids = ids;
                            cached.tiles.reserve(ids.size());
                            for (std::size_t i = 0; i < ids.size(); ++i) {
                                cached.tiles.emplace_back(
                                    ids[i],
                                    selected_tile_points[i]
                                );
                            }
                            cached.actual_bbox = actual_bbox;
                            cached.cache_hit_tiles = ids.size();
                            cached.candidate_tiles =
                                candidate_tile_count;
                            tile_upload_pending = std::move(cached);
                        } else {
                            tile_load_future = std::async(
                                std::launch::async,
                                [&reader,
                                 &tile_point_cache,
                                 ids,
                                 missing_tiles,
                                 selected_tile_points =
                                    std::move(selected_tile_points),
                                 actual_bbox,
                                 cache_hit_tiles,
                                 candidate_tile_count]() -> TileLoadResult {
                                    gs3d::util::Stopwatch read_timer;
                                    auto points_by_index =
                                        selected_tile_points;
                                    for (const auto& [index, tile_id] :
                                         missing_tiles) {
                                        auto points =
                                            std::make_shared<TilePoints>(
                                                reader.read_tile_points(
                                                    tile_id
                                                )
                                            );
                                        points_by_index[index] = points;
                                        tile_point_cache.put(
                                            tile_id,
                                            std::move(points)
                                        );
                                    }

                                    TileLoadResult loaded;
                                    loaded.tile_ids = ids;
                                    loaded.tiles.reserve(ids.size());
                                    for (std::size_t i = 0;
                                         i < ids.size();
                                         ++i) {
                                        loaded.tiles.emplace_back(
                                            ids[i],
                                            points_by_index[i]
                                        );
                                    }
                                    loaded.actual_bbox = actual_bbox;
                                    loaded.cache_hit_tiles =
                                        cache_hit_tiles;
                                    loaded.cache_miss_tiles =
                                        missing_tiles.size();
                                    loaded.candidate_tiles =
                                        candidate_tile_count;
                                    loaded.read_seconds =
                                        read_timer.elapsed_seconds();
                                    return loaded;
                                }
                            );

                            if (config_.tile_verbose) {
                                std::cout
                                    << "[TILE] async load dispatched, "
                                    << ids.size() << " tiles"
                                    << " (candidates="
                                    << candidate_tile_count
                                    << ", cache_hit="
                                    << cache_hit_tiles
                                    << ", cache_miss="
                                    << missing_tiles.size()
                                    << ").\n";
                            }
                        }
                    }
                }
            }

            // Select LOD level once per frame (not per-viewport) so all views
            // use the same level and the verbose log fires at most once.
            if (config_.lod_enabled && lod_gpu_cloud) {
                const auto level_count = lod_gpu_cloud->level_count();
                const auto requested =
                    lod_selector.select_level(level_count);

                // 安全网：select_level 理论上永远返回有效值（所有 LOD
                // 级别都在启动时一次性上传到 GPU），但万一索引越界退回到
                // last_valid，绝不画空帧。
                std::size_t resolved_lod;
                if (requested < level_count) {
                    resolved_lod = requested;
                } else {
                    resolved_lod = last_valid_lod_level;
                    std::cerr << "[WARN] LOD level out of range: "
                              << requested << " >= " << level_count
                              << ", falling back to "
                              << last_valid_lod_level << '\n';
                }

                /*
                 * KeepStableHighQuality:显示档位的"质量地板"是已经稳定收敛
                 * 到的最精细档位(idle 会收敛到 level 0 = 最高细节)。无论
                 * 交互中还是刚松手的过渡期,都绝不显示比这更粗的档位 ——
                 * 取 select_level() 返回值与 frozen_display_lod 中更精细的
                 * 一个(索引更小=更精细)。这样:
                 *   - 交互途中点云不会变稀疏(不降档);
                 *   - 松手后的 ~0.2s 过渡期也不会闪一下粗 LOD
                 *     (select_level 此时仍会短暂返回粗档,被地板挡住)。
                 * frozen 只在 idle 且 select_level 给出更精细档位时下移,
                 * 即只向"更高质量"更新,不会反向变粗。
                 * select_level() 仍照常在后台运行,供其他模式与状态延续。
                 */
                if (config_.interactive_display_mode ==
                        gs3d::app::InteractiveDisplayMode::AllowCoarseLOD) {
                    lod_level_for_frame = resolved_lod;
                } else {
                    if (!interacting && resolved_lod < frozen_display_lod) {
                        frozen_display_lod = resolved_lod;
                    }
                    lod_level_for_frame =
                        std::min(resolved_lod, frozen_display_lod);
                }
                last_valid_lod_level = lod_level_for_frame;

                if (lod_level_for_frame != last_lod_level) {
                    if (config_.lod_verbose) {
                        const auto& level =
                            lod_gpu_cloud->level(lod_level_for_frame);
                        std::cout << "[LOD] active level = "
                                  << lod_level_for_frame
                                  << ", points = "
                                  << level.gpu_point_count
                                  << ", idle_seconds = "
                                  << lod_selector.idle_seconds()
                                  << '\n';
                    }
                    last_lod_level = lod_level_for_frame;
                }
            }

            /*
             * 悬浮 tooltip（仅在悬停且未拖拽时查询，结果写进 AppState
             * 供下一帧 UiRoot 渲染——同一个"UI 出命令、主循环写回状态"
             * 的模式）。
             *
             * 候选点集合优先用当前 LOD 档位的点（≤ 阶梯最高档，目前
             * ≤1.5M 点）。但放大到能看清单个点时，画面上实际显示的是
             * 全分辨率 tile 的点，而不是稀疏的 LOD 降采样点——这时 LOD
             * 候选点在屏幕上彼此相距很远，鼠标 12px 半径内很可能一个都
             * 没有，悬浮 tooltip 因此消失，即使光标正对着一个清晰可见的
             * 点。所以再把"当前视口实际选中渲染的 tile"的真实点加入候选
             * 集合——这些点已经常驻 CPU 缓存（用于渲染上传），取用零额外
             * I/O。
             *
             * 仍按总点数设预算（kHoverTileCandidateBudget）只在选中 tile
             * 集合不大时合并：缩小视野时选中 tile 可逼近整个可见区域的
             * 全分辨率数据（实测一次到过 140 个 tile、共 2600 万+点），
             * 全部拿来逐点投影会让悬浮帧开销失控（压测炸过：1800 帧跑到
             * 90 秒还没完）。超预算时退回旧行为：只用 LOD 候选点，接受
             * "tooltip 显示降采样后的最近点"的精度妥协。
             */
            constexpr std::uint64_t kHoverTileCandidateBudget = 3'000'000;
            for (const auto& frame : gui_cmds.viewport_frames) {
                if (frame.index < 0 ||
                    frame.index >= static_cast<int>(app_state.render_views.size())) {
                    continue;
                }

                auto& view =
                    app_state.render_views[static_cast<std::size_t>(frame.index)];

                if (!frame.hovered || frame.active) {
                    view.hover_tooltip_visible = false;
                    continue;
                }

                std::vector<gs3d::core::PointDataView> candidate_point_sets;

                if (config_.lod_enabled && lod_gpu_cloud &&
                    lod_level_for_frame < lod_dataset.level_count()) {
                    candidate_point_sets.push_back(
                        gs3d::data::make_point_data_view(
                            lod_dataset.level(lod_level_for_frame).points
                        )
                    );
                } else if (!config_.lod_enabled && dataset.has_point_data()) {
                    candidate_point_sets.push_back(
                        gs3d::data::make_point_data_view(dataset)
                    );
                }

                // 真实渲染的全分辨率 tile 点，仅当总点数在预算内才合并
                // （见上方注释）；保活到本次查询结束。
                std::vector<SharedTilePoints> hover_tile_keepalive;
                if (config_.tile_enabled && tile_reader.has_value()) {
                    const auto& view_tile_ids =
                        viewport_tile_ids[
                            static_cast<std::size_t>(frame.index)
                        ];
                    std::uint64_t total_points = 0;
                    for (const auto tile_id : view_tile_ids) {
                        total_points +=
                            tile_reader->record(tile_id).point_count;
                    }
                    if (!view_tile_ids.empty() &&
                        total_points <= kHoverTileCandidateBudget) {
                        hover_tile_keepalive.reserve(view_tile_ids.size());
                        for (const auto tile_id : view_tile_ids) {
                            auto pts = tile_point_cache.find(tile_id);
                            if (pts) {
                                hover_tile_keepalive.push_back(
                                    std::move(pts)
                                );
                            }
                        }
                        for (const auto& pts : hover_tile_keepalive) {
                            candidate_point_sets.push_back(
                                gs3d::data::make_point_data_view(*pts)
                            );
                        }
                    }
                }

                const gs3d::camera::Viewport hover_viewport{
                    frame.width, frame.height
                };
                const auto hit = gs3d::render::find_nearest_point_on_screen(
                    candidate_point_sets,
                    frame.mouse_local_x,
                    frame.mouse_local_y,
                    hover_viewport,
                    viewport_manager.camera(frame.index)
                );

                view.hover_tooltip_visible = hit.has_value();
                if (hit) {
                    // Gs3dPoint x/y/z are origin-shifted at preprocess time
                    // (CsvToGs3dConverter subtracts the centroid); add it
                    // back so the tooltip matches the original CSV values.
                    view.hover_x =
                        static_cast<float>(
                            static_cast<double>(hit->point.x) + dataset.origin_x()
                        );
                    view.hover_y =
                        static_cast<float>(
                            static_cast<double>(hit->point.y) + dataset.origin_y()
                        );
                    view.hover_fold = hit->point.value;
                    view.hover_elevation =
                        static_cast<float>(
                            static_cast<double>(hit->point.z) + dataset.origin_z()
                        );
                }
            }

            renderer.draw_frame(
                window,
                gs3d::render::VulkanRenderer::FrameDrawCallbacks{
                    // pre_pass: all offscreen render passes execute here, before the
                    // swapchain render pass starts. Each viewport records its own
                    // vkCmdBeginRenderPass / draw / vkCmdEndRenderPass sequence into
                    // cmd; none of them nest inside each other or the swapchain pass.
                    .pre_pass = [&](VkCommandBuffer cmd) {
                        for (const int viewport_index :
                             visible_viewports) {
                            auto& framebuffer =
                                viewport_manager.framebuffer(
                                    viewport_index
                                );
                            const auto& viewport_camera =
                                viewport_manager.camera(
                                    viewport_index
                                );
                            framebuffer.render(
                                cmd,
                                [&](VkCommandBuffer c) {
                            // Per-viewport push: copy non-MVP fields from push,
                            // then fill in the per-viewport MVP matrix.
                            gs3d::render::PointPushConstants vp_push = push;
                            fill_push_constants(
                                vp_push,
                                viewport_camera
                            );
                            const VkExtent2D viewport_extent =
                                framebuffer.extent();

                            const auto view_index =
                                static_cast<std::size_t>(
                                    viewport_index
                                );
                            const auto& selected_tile_ids =
                                viewport_tile_ids[view_index];
                            // Single pass: compute both any/all resident flags
                            bool any_tile_resident = false;
                            bool all_tiles_resident =
                                !selected_tile_ids.empty();
                            for (const auto tile_id : selected_tile_ids) {
                                const bool resident =
                                    tile_gpu_cloud &&
                                    tile_gpu_cloud
                                        ->has_resident_tile(tile_id);
                                any_tile_resident =
                                    any_tile_resident || resident;
                                all_tiles_resident =
                                    all_tiles_resident && resident;
                            }
                            /*
                             * KeepStableHighQuality:交互期间继续绘制已驻留
                             * 的全分辨率 tile —— 新 tile 流式本就在交互期
                             * 冻结(见上方 !interacting 门控),所以这只是用
                             * 当前相机继续画"交互开始前已上传好的高质量
                             * buffer",无新上传、无中途驱逐,不空帧不闪烁。
                             * AllowCoarseLOD 才在交互期关掉 tile 叠加。
                             */
                            const bool tile_will_render =
                                (config_.interactive_display_mode !=
                                     gs3d::app::InteractiveDisplayMode::
                                         AllowCoarseLOD ||
                                 !interacting) &&
                                any_tile_resident;

                            gs3d::render::PointPushConstants lod_push = vp_push;
                            if (tile_will_render &&
                                all_tiles_resident &&
                                viewport_tile_query_boxes[view_index]
                                    .has_value()) {
                                const auto& b =
                                    *viewport_tile_query_boxes[view_index];
                                lod_push.clip_mode   = 1.0f;
                                lod_push.clip_min[0] = b.min_x;
                                lod_push.clip_min[1] = b.min_y;
                                lod_push.clip_min[2] = b.min_z;
                                lod_push.clip_min[3] = 0.0f;
                                lod_push.clip_max[0] = b.max_x;
                                lod_push.clip_max[1] = b.max_y;
                                lod_push.clip_max[2] = b.max_z;
                                lod_push.clip_max[3] = 0.0f;
                            }

                            point_pipeline.bind_for_viewport(
                                c,
                                viewport_extent
                            );

                            if (config_.lod_enabled) {
                                point_pipeline.draw_per_tile(
                                    c,
                                    lod_gpu_cloud->gpu_cloud(lod_level_for_frame),
                                    lod_push
                                );
                            } else {
                                point_pipeline.draw_per_tile(
                                    c,
                                    *full_gpu_cloud,
                                    lod_push
                                );
                            }

                            if (tile_will_render) {
                                for (const auto tile_id :
                                     selected_tile_ids) {
                                    if (!tile_gpu_cloud
                                            ->has_resident_tile(tile_id)) {
                                        continue;
                                    }
                                    point_pipeline.draw_per_tile(
                                        c,
                                        tile_gpu_cloud->gpu_cloud_for_tile(tile_id),
                                        vp_push
                                    );
                                }
                            }
                                }
                            );
                        }
                    },
                    // in_pass: only ImGui runs in the swapchain render pass.
                    // Each ImGui::Image() samples its viewport's offscreen texture.
                    .in_pass = [&](VkCommandBuffer cmd) {
                        imgui_layer.render(cmd);
                    }
                }
            );
            // Render ImGui platform windows (docked panels torn out to separate
            // OS windows). Must happen outside the main render pass.
            imgui_layer.render_platform_windows();
            // If draw_frame() returned early (minimized / swapchain out-of-date)
            // the draw callback was never invoked, so close the dangling ImGui frame.
            imgui_layer.discard_frame();

            // Rebuild framebuffer resources only after the user stops resizing.
            // All ready viewports share one device-idle synchronization point.
            const auto ready_resizes =
                viewport_resize_scheduler.take_ready(now_seconds);
            std::vector<gs3d::render::ViewportResizeRequest>
                resize_requests;
            resize_requests.reserve(ready_resizes.size());
            for (const auto& resize : ready_resizes) {
                resize_requests.push_back({
                    resize.index,
                    {resize.width, resize.height}
                });
                if (resize.index == streaming_viewport_index) {
                    tile_selection_dirty = true;
                }
            }
            viewport_manager.resize_many(resize_requests);

            if (config_.benchmark_mode) {
                benchmark_frame_times_ms.push_back(
                    benchmark_frame_timer.elapsed_milliseconds()
                );
                ++benchmark_frame_index;
            }
        }

        vkDeviceWaitIdle(context.device());

        if (config_.benchmark_mode) {
            std::cout << "[BENCH] frame_count = "
                      << benchmark_frame_times_ms.size() << '\n';
            std::cout << "[BENCH] frame_time_ms_p50 = "
                      << gs3d::util::percentile(benchmark_frame_times_ms, 50.0)
                      << '\n';
            std::cout << "[BENCH] frame_time_ms_p95 = "
                      << gs3d::util::percentile(benchmark_frame_times_ms, 95.0)
                      << '\n';
            std::cout << "[BENCH] frame_time_ms_p99 = "
                      << gs3d::util::percentile(benchmark_frame_times_ms, 99.0)
                      << '\n';
            if (!benchmark_reload_seconds.empty()) {
                std::cout << "[BENCH] reload_latency_seconds_p50 = "
                          << gs3d::util::percentile(
                                 benchmark_reload_seconds, 50.0)
                          << '\n';
                std::cout << "[BENCH] reload_latency_seconds_p95 = "
                          << gs3d::util::percentile(
                                 benchmark_reload_seconds, 95.0)
                          << '\n';
            } else {
                std::cout
                    << "[BENCH] reload_latency: "
                    << "no completed tile uploads captured.\n";
            }
        }

        std::cout << "[PASS] ViewerApp finished.\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "[FAIL] " << e.what() << '\n';
        return 1;
    }
}

} // namespace gs3d::app
