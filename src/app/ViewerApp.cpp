#include "app/ViewerApp.hpp"
#include "util/Log.hpp"
#include "app/ViewerDatasetSession.hpp"
#include "app/ViewerDatasetDescriptor.hpp"
#include "app/ViewerBenchmarkController.hpp"
#include "app/NavigationMapSystem.hpp"
#include "app/ViewerAttributeMapping.hpp"
#include "app/ViewerFrameStateSynchronizer.hpp"
#include "app/ScreenshotService.hpp"
#include "app/ViewerAppStateInitialization.hpp"
#include "app/ViewerAppGpuPick.hpp"
#include "app/ViewerAppInternal.hpp"
#include "app/ViewerAppRunState.hpp"
#include "app/ViewerAppTileStreaming.hpp"

#include "app/AppState.hpp"
#include "app/PreprocessedBundle.hpp"
#include "app/TilePointCache.hpp"
#include "app/ViewportCameraSystem.hpp"
#include "app/ViewportLodController.hpp"
#include "app/ViewportPresentationState.hpp"
#include "app/ViewportResizeScheduler.hpp"
#include "gui/ImGuiLayer.hpp"
#include "gui/UiFonts.hpp"
#include "ui/SvgLogoTexture.hpp"
#include "ui/UiPalette.hpp"
#include "ui/WorkspaceManager.hpp"
#include "render/ViewportManager.hpp"
#include "imgui.h"

#include "camera/BoxSelect.hpp"
#include "camera/Camera.hpp"
#include "camera/CameraController.hpp"
#include "camera/CameraHub.hpp"
#include "camera/MouseRay.hpp"
#include "core/DatasetDescriptor.hpp"
#include "data/Gs3dDataset.hpp"
#include "data/Gs3dReader.hpp"
#include "data/Gs3dLodDataset.hpp"
#include "data/Gs3dLodReader.hpp"
#include "data/Gs3dLodTargets.hpp"
#include "data/PointDataAdapters.hpp"
#include "data/TileDataAdapters.hpp"
#include "data/Gs3dTileReader.hpp"

#include "platform/Window.hpp"
#include "render/AxisGrid.hpp"
#include "render/LodSelector.hpp"
#include "render/PointCloudGpu.hpp"
#include "render/PointCloudLodGpu.hpp"
#include "render/PointPipeline.hpp"
#include "render/VulkanContext.hpp"
#include "render/VulkanRenderer.hpp"
#include "render/VulkanSwapchain.hpp"
#include "render/PointCloudTileGpu.hpp"
#include "render/TileSelection.hpp"
#include "render/VulkanBuffer.hpp"

#include "preprocess/Gs3dLodWriter.hpp"
#include "util/Stopwatch.hpp"
#include "scene/SceneState.hpp"

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <optional>
#include <sstream>
#include <unordered_map>

namespace gs3d::app {

namespace {

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

gs3d::render::SwapchainPresentModeHint benchmark_present_mode_hint(
    const std::string& mode
) {
    if (mode == "immediate") {
        return gs3d::render::SwapchainPresentModeHint::Immediate;
    }
    if (mode == "mailbox") {
        return gs3d::render::SwapchainPresentModeHint::Mailbox;
    }
    if (mode == "fifo") {
        return gs3d::render::SwapchainPresentModeHint::Fifo;
    }
    return gs3d::render::SwapchainPresentModeHint::Auto;
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
        config.tile.min_pixel_size;
    tile_config.max_visible_tiles =
        config.tile.max_visible_tiles;

    tile_config.use_full_z_range =
        config.tile.use_full_z_range;

    return tile_config;
}

gs3d::render::PointCloudLodSource build_lod_source(
    const gs3d::data::Gs3dLodDataset& lod_dataset,
    const std::vector<std::vector<std::uint32_t>>& lod_point_ids
) {
    gs3d::render::PointCloudLodSource source;
    source.levels.reserve(lod_dataset.level_count());
    for (std::size_t i = 0; i < lod_dataset.level_count(); ++i) {
        const auto& level = lod_dataset.level(i);
        source.levels.push_back({
            level.name,
            gs3d::data::Gs3dLodDataset::voxel_mode_name(level.voxel_mode),
            level.level_index,
            level.source_point_count,
            level.target_point_count,
            level.voxel_size,
            gs3d::data::make_point_data_view(
                level.points,
                lod_point_ids[i].data()
            )
        });
    }
    return source;
}


void print_controls(
    bool lod_enabled,
    bool tile_enabled
){
    gs3d::util::log::info() << "[OK] Entering render loop.\n";
    gs3d::util::log::info() << "操作说明：\n";
    gs3d::util::log::info() << "  左键拖动：轨道旋转\n";
    gs3d::util::log::info() << "  右键拖动：视角平移\n";
    gs3d::util::log::info() << "  滚轮：缩放到光标位置\n";
    gs3d::util::log::info() << "  Ctrl+左键拖动：框选\n";
    gs3d::util::log::info() << "  双击点：选择并设置旋转中心\n";
    gs3d::util::log::info() << "  F：聚焦选中点\n";
    gs3d::util::log::info() << "  + / -：调整点大小\n";
    gs3d::util::log::info() << "  R：恢复全局视图\n";
    gs3d::util::log::info() << "  Tab：切换着色属性\n";
    gs3d::util::log::info() << "  Esc：退出\n";
    gs3d::util::log::info() << "渲染模式：\n";
    gs3d::util::log::info() << "  LOD         : "
              << (lod_enabled ? "启用" : "关闭")
              << '\n';
    gs3d::util::log::info() << "  全分辨率瓦片："
              << (tile_enabled ? "启用" : "关闭")
              << '\n';
}
} // namespace

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

// 假设数据集坐标单位为米（UTM / 本地网格）。若源数据使用其他单位
// （如英尺、度），需要按数据集配置比例尺单位标签。

// ponytail: 硬编码公制单位，若支持多数据源需改为可配置。
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
namespace {

constexpr double kInteractingDebounceSeconds = 0.15;

} // namespace


ViewerApp::ViewerApp(ViewerAppConfig config)
    : config_(std::move(config))
{
}

int ViewerApp::run() {
    try {
        open_request_.reset();
        gs3d::util::Stopwatch startup_timer;

        auto dataset_session = prepare_viewer_dataset(config_);
        if (!dataset_session.has_value()) {
            return 1;
        }

        auto& dataset = dataset_session->dataset;
        const auto& full_point_ids = dataset_session->full_point_ids;
        auto& tile_reader = dataset_session->tile_reader;
        auto& tile_index_view = dataset_session->tile_index_view;
        auto& tile_point_ids_by_tile = dataset_session->tile_point_ids_by_tile;
        auto& lod_dataset = dataset_session->lod_dataset;
        auto& lod_point_ids = dataset_session->lod_point_ids;
        auto& runtime_points_by_id = dataset_session->runtime_points_by_id;
        auto& runtime_points_valid_by_id =
            dataset_session->runtime_points_valid_by_id;

        gs3d::platform::WindowConfig window_config;
        window_config.width = config_.window.width;
        window_config.height = config_.window.height;
        window_config.title = config_.window.title;
        window_config.resizable = config_.window.resizable;

        gs3d::platform::Window window(window_config);

        gs3d::render::VulkanContextConfig vk_config;
        vk_config.enable_validation_layers =
            config_.graphics.enable_validation_layers;
        vk_config.application_name = "GeoScatter3D";
        vk_config.preferred_gpu = config_.graphics.preferred_gpu;

        gs3d::render::VulkanContext context(window, vk_config);
        gs3d::util::log::info() << "[TIME] viewer.startup_seconds = "
                  << startup_timer.elapsed_seconds()
                  << '\n';

        gs3d::util::log::info() << "[OK] VulkanContext created.\n";
        gs3d::util::log::info() << "Physical device: "
                  << context.physical_device_name() << '\n';

        gs3d::render::VulkanSwapchain swapchain(
            context,
            window,
            benchmark_present_mode_hint(
                config_.benchmark.present_mode
            )
        );
        gs3d::render::VulkanRenderer renderer(context, swapchain);

        gs3d::gui::ImGuiLayer imgui_layer;
        imgui_layer.init(
            window.native_handle(),
            context,
            renderer,
            swapchain.image_count(),
            config_.window.ui_layout_ini_path,
            config_.window.ui_scale_multiplier,
            config_.window.enable_multi_viewports
        );

        // Size the window adaptively to the monitor, mirroring the welcome
        // page's approach but with a wider 16:10 ratio suitable for a
        // multi-panel workbench.
        {
            const float ui_scale = gs3d::gui::ui_fonts().ui_scale;
            constexpr int kBaseOuterWidth = 1440;
            constexpr int kMinOuterWidth = 1100;
            constexpr float kAspect = 16.0f / 10.0f;

            int outer_w = std::max(kMinOuterWidth,
                static_cast<int>(std::lround(
                    static_cast<float>(kBaseOuterWidth) * ui_scale)));

            GLFWmonitor* monitor = glfwGetPrimaryMonitor();
            if (monitor != nullptr) {
                int work_x = 0, work_y = 0, work_w = 0, work_h = 0;
                glfwGetMonitorWorkarea(
                    monitor, &work_x, &work_y, &work_w, &work_h);
                if (work_w > 0 && work_h > 0) {
                    const int w_limit = static_cast<int>(
                        std::floor(static_cast<float>(work_w) * 0.85f));
                    const int h_limit = static_cast<int>(
                        std::floor(static_cast<float>(work_h) * 0.85f));
                    const int w_from_h = static_cast<int>(
                        std::floor(static_cast<float>(h_limit) * kAspect));
                    outer_w = std::clamp(outer_w, kMinOuterWidth,
                        std::max(kMinOuterWidth,
                                 std::min(w_limit, w_from_h)));
                }
            }

            const int outer_h = static_cast<int>(std::lround(
                static_cast<float>(outer_w) / kAspect));

            int frame_l = 0, frame_t = 0, frame_r = 0, frame_b = 0;
            glfwGetWindowFrameSize(window.native_handle(),
                                   &frame_l, &frame_t, &frame_r, &frame_b);
            const int client_w = std::max(1, outer_w - frame_l - frame_r);
            const int client_h = std::max(1, outer_h - frame_t - frame_b);
            glfwSetWindowSize(window.native_handle(), client_w, client_h);

            GLFWmonitor* center_monitor = glfwGetPrimaryMonitor();
            if (center_monitor != nullptr) {
                int work_x = 0, work_y = 0, work_w = 0, work_h = 0;
                glfwGetMonitorWorkarea(center_monitor,
                                       &work_x, &work_y, &work_w, &work_h);
                glfwSetWindowPos(window.native_handle(),
                                 work_x + (work_w - outer_w) / 2,
                                 work_y + (work_h - outer_h) / 2);
            }
        }

        // 交换链是 UNORM 格式——配置里的 clear_color 按 sRGB 语义书写，
        // 直接使用，无需颜色空间转换。
        gs3d::render::ClearColor clear_color;
        clear_color.r = config_.graphics.clear_color[0];
        clear_color.g = config_.graphics.clear_color[1];
        clear_color.b = config_.graphics.clear_color[2];
        clear_color.a = config_.graphics.clear_color[3];
        renderer.set_clear_color(clear_color);

        gs3d::ui::SvgLogoTexture logo_texture(
            context.device(),
            context.physical_device(),
            context.graphics_queue(),
            renderer.command_pool(),
            "assets/icon.svg",
            128
        );
        gs3d::util::log::info() << "[OK] SvgLogoTexture loaded.\n";

        std::unique_ptr<gs3d::render::PointCloudGpu> full_gpu_cloud;
        std::unique_ptr<gs3d::render::PointCloudLodGpu> lod_gpu_cloud;
        std::unique_ptr<gs3d::render::PointCloudTileGpu> tile_gpu_cloud;

        if (config_.lod.enabled) {
            const auto lod_source =
                build_lod_source(lod_dataset, lod_point_ids);
            lod_gpu_cloud =
                std::make_unique<gs3d::render::PointCloudLodGpu>(
                    context,
                    renderer.command_pool(),
                    context.graphics_queue(),
                    lod_source
                );

            gs3d::util::log::info() << "[OK] PointCloudLodGpu uploaded.\n";
            gs3d::util::log::info() << lod_gpu_cloud->summary();

        } else {
            full_gpu_cloud =
                std::make_unique<gs3d::render::PointCloudGpu>(
                    context,
                    renderer.command_pool(),
                    context.graphics_queue(),
                    gs3d::data::make_point_data_view(
                        dataset,
                        full_point_ids.data()
                    )
                );

            gs3d::util::log::info() << "[OK] PointCloudGpu uploaded.\n";
            gs3d::util::log::info() << "gpu point_count = "
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
            std::clamp(config_.window.viewport_count, 1, kMaxViewportCount)
        );
        viewport_manager.set_clear_color(clear_color);

        gs3d::render::PointPipelineConfig pipeline_config;
        pipeline_config.vertex_shader_path =
            config_.graphics.vertex_shader_path;
        pipeline_config.fragment_shader_path =
            config_.graphics.fragment_shader_path;

        gs3d::render::PointPipeline point_pipeline(
            context,
            viewport_manager.render_pass(),
            pipeline_config
        );

        gs3d::util::log::info() << "[OK] PointPipeline created.\n";

        GpuPickReadback gpu_pick_readback(
            context,
            renderer.frames_in_flight(),
            static_cast<std::size_t>(viewport_manager.viewport_count())
        );
        PickDebugFrameDumper pick_debug_frame_dumper(
            context,
            renderer.frames_in_flight()
        );
        std::uint64_t pick_debug_dump_count = 0;
        bool pick_debug_dump_completed = false;
        std::vector<bool> pending_hover_miss_dump(
            static_cast<std::size_t>(viewport_manager.viewport_count()),
            false
        );
        ViewerAppPickState pick;
        {
            const auto n = static_cast<std::size_t>(viewport_manager.viewport_count());
            pick.latest_hover_points.resize(n);
            pick.latest_capture_x.resize(n, 0.0f);
            pick.latest_capture_y.resize(n, 0.0f);
            // Frames since last successful pick hit — clears stale hover
            // data after ~0.5 s of no hits.
            pick.hover_timeout.resize(n, 0);
            // Consecutive GPU pick misses (has_hit=false) — clears hover
            // after a short debounce so moving between points doesn't flicker.
            pick.consecutive_no_hit.resize(n, 0);
            pick.requests.resize(n);
        }
        ViewerBenchmarkController benchmark_controller(
            config_.benchmark.enabled,
            config_.benchmark.frame_count,
            renderer.frames_in_flight(),
            config_.benchmark.pick_script_path
        );
        auto& benchmark_session = benchmark_controller.session();
        const bool benchmark_pick_enabled = benchmark_controller.pick_enabled();
        auto& benchmark_pick_issue_cpu_ms = benchmark_controller.issue_cpu_ms();
        auto& benchmark_pick_issue_metadata = benchmark_controller.issue_metadata();
        auto& benchmark_pick_results = benchmark_controller.results();

        gs3d::camera::CameraControllerConfig controller_config;
        controller_config.rotate_speed =
            config_.controller.rotate_speed;
        controller_config.pan_speed =
            config_.controller.pan_speed;
        controller_config.zoom_speed =
            config_.controller.zoom_speed;
        controller_config.invert_rotate_x =
            config_.controller.invert_rotate_x;
        controller_config.invert_rotate_y =
            config_.controller.invert_rotate_y;
        controller_config.invert_pan_x =
            config_.controller.invert_pan_x;
        controller_config.invert_pan_y =
            config_.controller.invert_pan_y;

        ViewportCameraSystem viewport_cameras(
            controller_config,
            bounds,
            static_cast<std::size_t>(viewport_manager.viewport_count())
        );
        std::vector<std::optional<gs3d::camera::Vec3>>
            selected_focus_points(
                static_cast<std::size_t>(viewport_manager.viewport_count())
            );

        // Views start independent. The per-view UI can opt into sync group 0.
        gs3d::camera::CameraHub camera_hub;
        for (int i = 0; i < viewport_manager.viewport_count(); ++i) {
            camera_hub.add(
                i,
                &viewport_manager.camera(i),
                gs3d::camera::CameraHub::kIndependent
            );
        }

        gs3d::util::log::info() << "[OK] CameraController initialized.\n";
        gs3d::util::log::info() << "camera position = ["
                  << viewport_manager.camera(0).position().x << ", "
                  << viewport_manager.camera(0).position().y << ", "
                  << viewport_manager.camera(0).position().z << "]\n";

        gs3d::util::log::info() << "camera target = ["
                  << viewport_manager.camera(0).target().x << ", "
                  << viewport_manager.camera(0).target().y << ", "
                  << viewport_manager.camera(0).target().z << "]\n";

        gs3d::util::log::info() << "camera distance = "
                  << viewport_manager.camera(0).distance() << '\n';

        /*
         * 瓦片流式状态：每视口可见瓦片集与 clip 包围盒、CPU 缓存、异步
         * future、全量预加载进度。见 ViewerAppTileStreaming.hpp；必须
         * 声明在 tile_reader / tile_point_ids_by_tile 之后（后台 future
         * 引用它们，析构时先 join future）。
         */
        ViewerAppTileStreamState tile_stream(
            config_.tile.cpu_cache_max_bytes
        );
        tile_stream.preload_enabled =
            config_.tile.enabled &&
            config_.tile.preload_all &&
            !config_.benchmark.enabled &&
            tile_reader.has_value() &&
            !tile_reader->records().empty() &&
            tile_reader->stats().total_point_bytes <=
                config_.tile.preload_max_bytes;

        // Debounce interacting so rapid scroll zoom doesn't cause
        // frame-by-frame toggling (tiles pop in/out, LOD clip flicker).
        auto interacting_debounce_until =
            std::chrono::steady_clock::now();

        const auto resolve_hover_point_from_visible_tiles =
            [&tile_stream](
                std::size_t view_index,
                std::uint32_t point_id,
                float mouse_x,
                float mouse_y
            ) {
                static_cast<void>(mouse_x);
                static_cast<void>(mouse_y);
                const auto candidate_point_sets =
                    collect_visible_hover_tile_views(
                        tile_stream,
                        view_index
                    );
                if (candidate_point_sets.empty()) {
                    return std::optional<gs3d::data::Gs3dPoint>{};
                }

                if (const auto exact =
                        find_point_by_id_in_views(
                            candidate_point_sets,
                            point_id
                        )) {
                    return exact;
                }
                return std::optional<gs3d::data::Gs3dPoint>{};
            };

        ViewportResizeScheduler viewport_resize_scheduler(0.15);

        gs3d::render::LodSelector lod_selector;

        if (config_.lod.enabled) {
            gs3d::render::LodSelectorConfig lod_selector_config;
            lod_selector_config.medium_delay_seconds =
                config_.lod.medium_delay_seconds;
            lod_selector_config.high_delay_seconds =
                config_.lod.high_delay_seconds;
            lod_selector_config.use_lowest_while_interacting =
                config_.lod.use_lowest_while_interacting;
            lod_selector_config.adaptive_interacting_level =
                config_.lod.adaptive_interacting_level;
            lod_selector_config.frame_time_budget_ms =
                config_.lod.frame_time_budget_ms;

            lod_selector.set_config(lod_selector_config);
        }

        gs3d::render::TileSelection tile_selection;
        gs3d::render::TileSelectionResult tile_result;
        bool tile_selection_dirty = true;
        int streaming_viewport_index = 0;

        if (config_.tile.enabled && tile_reader.has_value()) {
            tile_selection.set_config(
                make_tile_selection_config(config_)
            );

            tile_gpu_cloud =
                std::make_unique<gs3d::render::PointCloudTileGpu>();
            tile_gpu_cloud->set_resident_tile_budget(
                config_.tile.gpu_cache_max_tiles
            );

            gs3d::util::log::info() << "[OK] TileSelection initialized.\n";
        }

        bool r_was_pressed = false;
        bool f_was_pressed = false;
        bool tab_was_pressed = false;
        bool shift_tab_was_pressed = false;

        ViewerAttributeMapping attribute_mapping(
            dataset,
            config_.input.primary_value_field_name,
            config_.input.z_field_name
        );
        const auto& primary_value_name = attribute_mapping.primary_value_name();
        const auto& z_field_name = attribute_mapping.z_field_name();
        const auto& attr_list = attribute_mapping.descriptors();
        float height_exag = 1.0f;
        auto push = attribute_mapping.make_initial_push(
            config_.graphics.initial_point_size,
            height_exag
        );

        ViewportLodController viewport_lod;
        // Hoisted out of the loop body so report_frame_time() can pair the
        // level rendered in frame N-1 with frame N-1's measured duration
        // (delta_seconds, computed at the top of frame N) before this
        // frame reassigns it.
        std::size_t lod_level_for_frame = 0;

        auto previous_time =
            std::chrono::steady_clock::now();

        // Smoothed FPS via exponential moving average
        float fps_smooth = 0.0f;

        print_controls(
                        config_.lod.enabled,
                        config_.tile.enabled
                    );

        auto dataset_descriptor = make_viewer_dataset_descriptor(
            dataset,
            config_.input.gs3d_path,
            attr_list
        );
        gs3d::scene::SceneState scene_state;
        scene_state.active_dataset = &dataset_descriptor;
        scene_state.active_attribute_index = 0;  // 颜色=fold (attr_list[0])
        scene_state.active_height_index    = 1;  // 高度=高程 (attr_list[1])
        ViewportPresentationState viewport_presentation(
            static_cast<std::size_t>(viewport_manager.viewport_count()),
            push,
            scene_state,
            height_exag
        );
        auto& viewport_pushes = viewport_presentation.pushes();
        auto& viewport_scene_states = viewport_presentation.scenes();
        auto& viewport_height_exags =
            viewport_presentation.height_exaggerations();
        const std::size_t viewport_state_count =
            static_cast<std::size_t>(viewport_manager.viewport_count());
        const ViewerAppStateInitializationInput initial_state_input{
            dataset_descriptor,
            attr_list,
            lod_dataset,
            tile_reader,
            tile_stream.preload_enabled,
            viewport_state_count,
            config_.window.viewport_count,
            config_.benchmark.enabled
        };
        AppState app_state =
            make_initial_viewer_app_state(initial_state_input);
        viewport_presentation.initialize_visibility(app_state);

        app_state.logo_texture = logo_texture.descriptor();

        // ── analysis.toml persistence ──────────────────────────────────
        app_state.bundle_dir = config_.input.bundle_dir;
        auto& persisted_measurement =
            app_state.measurements.empty()
                ? app_state.measurement
                : app_state.measurements.front();
        load_analysis(app_state.bundle_dir, persisted_measurement);
        app_state.measurement = persisted_measurement;
        persisted_measurement.on_changed = [&app_state]() {
            const auto& measurement =
                app_state.measurements.empty()
                    ? app_state.measurement
                    : app_state.measurements.front();
            save_analysis(app_state.bundle_dir, measurement);
        };

        const auto& nav_cloud =
            lod_gpu_cloud
                ? lod_gpu_cloud->lowest_detail().gpu_cloud
                : *full_gpu_cloud;
        NavigationMapSystem navigation_maps;
        navigation_maps.initialize(
            context,
            renderer.command_pool(),
            swapchain.image_format(),
            dataset,
            app_state,
            point_pipeline,
            nav_cloud,
            viewport_pushes,
            dataset.bbox_max_z()
        );

        std::vector<int> visible_viewports;
        visible_viewports.reserve(
            static_cast<std::size_t>(viewport_manager.viewport_count())
        );
        const ViewerFrameStateSynchronizer frame_state_synchronizer(
            dataset.origin_z(),
            config_.tile.gpu_cache_max_tiles,
            config_.lod.enabled
        );

        const auto consume_ready_pick_frame_slot =
            [this,
             &pick,
             &pick_debug_frame_dumper,
             &gpu_pick_readback,
             &runtime_points_by_id,
             &runtime_points_valid_by_id,
             &viewport_cameras,
             &selected_focus_points,
             &viewport_manager,
             &bounds,
             &viewport_pushes,
             &streaming_viewport_index,
             &tile_selection_dirty,
             &benchmark_pick_enabled,
             &benchmark_pick_issue_cpu_ms,
             &benchmark_pick_issue_metadata,
             &benchmark_pick_results,
             &resolve_hover_point_from_visible_tiles]
            (std::uint32_t frame_slot) {
                ViewerAppPickLookupContext pick_lookup{
                    runtime_points_by_id,
                    runtime_points_valid_by_id
                };
                ViewerAppPickCameraContext pick_camera{
                    viewport_cameras.controllers(),
                    selected_focus_points,
                    viewport_manager,
                    bounds,
                    viewport_pushes,
                    streaming_viewport_index,
                    tile_selection_dirty
                };
                ViewerAppBenchmarkPickContext pick_benchmark{
                    benchmark_pick_enabled,
                    benchmark_pick_issue_cpu_ms,
                    benchmark_pick_issue_metadata,
                    benchmark_pick_results
                };
                this->consume_ready_pick_frame_slot(
                    frame_slot, pick, pick_debug_frame_dumper,
                    gpu_pick_readback, pick_lookup, pick_camera,
                    pick_benchmark, resolve_hover_point_from_visible_tiles);
            };

        // ponytail: screenshot staging — allocated on demand in post_pass, read
        // back after draw_frame. Only one screenshot at a time.
        ScreenshotService screenshot_service;

        while (!window.should_close() &&
               benchmark_session.should_continue()) {
            gs3d::util::Stopwatch benchmark_frame_timer;
            double benchmark_cpu_frame_ms = 0.0;
            double benchmark_camera_update_ms_frame = 0.0;
            double benchmark_lod_tile_select_ms_frame = 0.0;
            double benchmark_cpu_cull_ms_frame = 0.0;
            double benchmark_upload_record_ms_frame = 0.0;
            const auto current_time =
                std::chrono::steady_clock::now();

            const double delta_seconds =
                std::chrono::duration<double>(
                    current_time - previous_time
                ).count();

            previous_time = current_time;

            for (std::uint32_t frame_slot = 0;
                 frame_slot < renderer.frames_in_flight();
                 ++frame_slot) {
                if (!renderer.is_frame_slot_ready(frame_slot)) {
                    continue;
                }
                consume_ready_pick_frame_slot(frame_slot);
            }

            // Pairs the level rendered last frame with its measured
            // duration, driving LodSelectorConfig::adaptive_interacting_level
            // (no-op otherwise). Must run before lod_level_for_frame is
            // reassigned for *this* frame, further down.
            if (config_.lod.enabled && delta_seconds > 0.0) {
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
            // Pending = required GPU working-set tiles not yet resident.
            // Stages 1/2 fall back to the full candidate set.
            std::size_t pending_tile_count = 0;
            if (config_.tile.enabled && tile_gpu_cloud &&
                tile_result.enabled) {
                const auto& pending_source =
                    tile_stream.gpu_required_tile_ids.empty()
                        ? tile_result.tile_ids
                        : tile_stream.gpu_required_tile_ids;
                for (const auto tile_id : pending_source) {
                    if (!tile_gpu_cloud->has_resident_tile(tile_id)) {
                        ++pending_tile_count;
                    }
                }
            }
            if (tile_stream.load_future.valid()) {
                pending_tile_count = std::max(
                    pending_tile_count,
                    tile_stream.loading_ids.size());
            }
            const std::uint32_t pending_tiles =
                static_cast<std::uint32_t>(
                    std::min<std::size_t>(
                        pending_tile_count,
                        std::numeric_limits<std::uint32_t>::max()
                    )
                );
            std::uint64_t gpu_buffer_bytes = 0;
            std::uint64_t gpu_resident_points = 0;

            if (config_.tile.enabled && tile_gpu_cloud) {
                const auto& ts = tile_gpu_cloud->stats();
                loaded_tiles =
                    static_cast<std::uint32_t>(
                        ts.resident_tile_count
                    );
                gpu_buffer_bytes += ts.gpu_buffer_bytes;
                gpu_resident_points += ts.point_count;
            }
            if (full_gpu_cloud) {
                gpu_buffer_bytes +=
                    static_cast<std::uint64_t>(full_gpu_cloud->vertex_buffer_size());
                gpu_resident_points += full_gpu_cloud->point_count();
            }
            if (lod_gpu_cloud) {
                for (std::size_t i = 0; i < lod_gpu_cloud->level_count(); ++i) {
                    gpu_buffer_bytes += static_cast<std::uint64_t>(
                        lod_gpu_cloud->gpu_cloud(i).vertex_buffer_size()
                    );
                    gpu_resident_points +=
                        lod_gpu_cloud->level(i).gpu_point_count;
                }
            }

            // 视窗中实际可见的点数：瓦片模式下统计视锥体筛选后
            // 的瓦片点数和，非瓦片模式下使用 GPU 驻留点数。
            std::uint64_t visible_points = gpu_resident_points;
            if (tile_result.enabled && tile_reader.has_value()) {
                visible_points = 0;
                for (const auto tile_id : tile_result.tile_ids) {
                    visible_points +=
                        tile_reader->record(tile_id).point_count;
                }
            }

            const auto tile_cache_stats = tile_stream.point_cache.stats();
            const ViewerFrameTileCacheMetrics tile_cache_metrics{
                .resident_bytes = tile_cache_stats.resident_bytes,
                .max_bytes = tile_cache_stats.max_bytes,
                .hits = tile_cache_stats.hits,
                .misses = tile_cache_stats.misses
            };
            const ViewerFrameStateMetrics frame_state_metrics{
                .dataset_point_count = dataset.point_count(),
                .gpu_resident_points = gpu_resident_points,
                .visible_points = visible_points,
                .gpu_buffer_bytes = gpu_buffer_bytes,
                .loaded_tiles = loaded_tiles,
                .pending_tiles = pending_tiles,
                .fps = fps_smooth,
                .frame_time_ms = delta_seconds > 0.0
                    ? static_cast<float>(delta_seconds * 1000.0)
                    : 0.0f,
                .camera_position = format_vec3_text(primary_camera.position())
            };
            frame_state_synchronizer.synchronize(
                app_state,
                viewport_pushes,
                viewport_scene_states,
                viewport_height_exags,
                tile_cache_metrics,
                frame_state_metrics
            );

            {
                ViewerAppRenderViewContext render_ctx{
                    viewport_manager,
                    dataset,
                    bounds,
                    viewport_pushes,
                    primary_value_name,
                    z_field_name,
                    visible_points,
                    n_viewports
                };
                fill_render_views(app_state, render_ctx, pick, selected_focus_points);
            }

            navigation_maps.synchronize_view_rects(app_state);
            app_state.measurement = measurement_for_view(
                app_state,
                app_state.active_viewport_index
            );
            app_state.region_stats = region_stats_for_view(
                app_state,
                app_state.active_viewport_index
            );

            const int default_view_source =
                viewport_presentation.first_visible_main_view(app_state);
            auto gui_cmds = imgui_layer.new_frame(app_state);
            viewport_presentation.copy_newly_visible_views(
                app_state,
                default_view_source
            );
            int runtime_active_count = 1;
            for (const auto& view : app_state.render_views) {
                if (view.visible) {
                    runtime_active_count = std::max(
                        runtime_active_count,
                        view.viewport_index + 1
                    );
                }
            }
            runtime_active_count = std::clamp(
                runtime_active_count,
                1,
                viewport_manager.viewport_count()
            );
            viewport_manager.set_active_count(runtime_active_count);
            if (streaming_viewport_index < 0 ||
                streaming_viewport_index >=
                    static_cast<int>(app_state.render_views.size()) ||
                !app_state
                     .render_views[
                         static_cast<std::size_t>(streaming_viewport_index)
                     ]
                     .visible) {
                const auto first_visible = std::find_if(
                    app_state.render_views.begin(),
                    app_state.render_views.end(),
                    [](const auto& view) {
                        return view.visible;
                    }
                );
                streaming_viewport_index =
                    first_visible != app_state.render_views.end()
                        ? first_visible->viewport_index
                        : 0;
                tile_selection_dirty = true;
            }
            apply_project_open_commands(gui_cmds, window);
            const double now_seconds =
                std::chrono::duration<double>(
                    current_time.time_since_epoch()
                ).count();
            benchmark_controller.apply_scripted_viewport(
                gui_cmds,
                config_.window.width,
                config_.window.height
            );
            observe_viewport_resize_requests(
                gui_cmds, viewport_resize_scheduler, now_seconds);

            build_visible_viewports(visible_viewports, app_state.render_views);

            const bool imgui_wants_keyboard =
                ImGui::GetIO().WantCaptureKeyboard;
            const bool keyboard_shortcuts_allowed =
                !ImGui::GetIO().WantTextInput;
            gs3d::util::Stopwatch benchmark_camera_timer;

            {
                ViewerAppCameraCommandContext cam_ctx{
                    .n_viewports = n_viewports,
                    .controllers = viewport_cameras.controllers(),
                    .viewport_manager = viewport_manager,
                    .camera_hub = camera_hub,
                    .bounds = bounds,
                    .streaming_viewport_index = streaming_viewport_index,
                    .tile_selection_dirty = tile_selection_dirty
                };
                apply_reset_camera_command(gui_cmds, cam_ctx);
            }
            {
                const auto main_targets = [&]() {
                    std::vector<int> targets;
                    targets.push_back(app_state.active_viewport_index);
                    return targets;
                };
                for (const auto& command :
                     gui_cmds.render_settings_commands) {
                    const auto targets =
                        !command.has_viewport_scope
                        ? main_targets()
                        : command.viewport_indices;
                    for (const int viewport_index : targets) {
                        if (viewport_index < 0 ||
                            viewport_index >=
                                static_cast<int>(viewport_pushes.size())) {
                            continue;
                        }
                        const auto idx =
                            static_cast<std::size_t>(viewport_index);
                        ViewerAppRenderSettingsContext render_ctx{
                            .push = viewport_pushes[idx],
                            .scene_state = viewport_scene_states[idx],
                            .navigation_map =
                                navigation_map_for_view(
                                    app_state,
                                    viewport_index
                                ),
                            .attr_list = attr_list,
                            .dataset = dataset,
                            .height_exag = viewport_height_exags[idx]
                        };
                        apply_render_setting_commands(command, render_ctx);
                    }
                }
            }
            if (gui_cmds.clear_cache_requested) {
                clear_tile_cpu_cache(tile_stream);
            }
            screenshot_service.request(gui_cmds, app_state, swapchain);

            {
                RegionStatsCommandContext rs_ctx{
                    .viewport_manager = viewport_manager,
                    .dataset = dataset,
                    .bounds = bounds,
                    .primary_value_name = primary_value_name,
                    .z_field_name = z_field_name
                };
                handle_region_stats_commands(gui_cmds, app_state, rs_ctx);
            }

            if (!imgui_wants_keyboard && window.key_pressed(GLFW_KEY_ESCAPE)) {
                window.request_close();
            }

            if (!imgui_wants_keyboard) {
                const auto active_render_index =
                    static_cast<std::size_t>(
                        std::clamp(
                            streaming_viewport_index,
                            0,
                            static_cast<int>(viewport_pushes.size()) - 1
                        )
                    );
                auto& active_push = viewport_pushes[active_render_index];
                if (window.key_pressed(GLFW_KEY_EQUAL) ||
                    window.key_pressed(GLFW_KEY_KP_ADD)) {
                    active_push.point_size = std::min(
                        active_push.point_size + 0.05f,
                        10.0f
                    );
                }

                if (window.key_pressed(GLFW_KEY_MINUS) ||
                    window.key_pressed(GLFW_KEY_KP_SUBTRACT)) {
                    active_push.point_size = std::max(
                        active_push.point_size - 0.05f,
                        1.0f
                    );
                }
            }

            const bool r_pressed =
                keyboard_shortcuts_allowed &&
                (window.key_pressed(GLFW_KEY_R) ||
                 ImGui::IsKeyPressed(ImGuiKey_R, false));

            if (r_pressed && !r_was_pressed) {
                viewport_cameras.controller(streaming_viewport_index)
                    .clear_orbit_pivot();
                initialize_camera_from_config(
                    viewport_manager.camera(streaming_viewport_index),
                    config_,
                    bounds
                );
                camera_hub.propagate(streaming_viewport_index);
                tile_selection_dirty = true;
            }

            r_was_pressed = r_pressed;

            const bool f_pressed =
                keyboard_shortcuts_allowed &&
                (window.key_pressed(GLFW_KEY_F) ||
                 ImGui::IsKeyPressed(ImGuiKey_F, false));

            if (f_pressed && !f_was_pressed) {
                const auto focus_index =
                    static_cast<std::size_t>(streaming_viewport_index);
                if (focus_index < selected_focus_points.size() &&
                    selected_focus_points[focus_index].has_value()) {
                    viewport_cameras.controller(
                        streaming_viewport_index
                    ).focus_on(
                        viewport_manager.camera(
                            streaming_viewport_index
                        ),
                        *selected_focus_points[focus_index]
                    );
                    camera_hub.propagate(streaming_viewport_index);
                    tile_selection_dirty = true;
                    gs3d::util::log::info()
                        << "[CAMERA] focused selected point in viewport "
                        << streaming_viewport_index << '\n';
                } else {
                    gs3d::util::log::info()
                        << "[CAMERA] focus skipped: no selected point in viewport "
                        << streaming_viewport_index << '\n';
                }
            }

            f_was_pressed = f_pressed;

            // Tab: cycle color attribute; Shift+Tab: cycle height attribute
            // (zero GPU cost — push constant only)
            const bool tab_held =
                !imgui_wants_keyboard && window.key_pressed(GLFW_KEY_TAB);
            const bool shift_mod =
                window.key_pressed(GLFW_KEY_LEFT_SHIFT) ||
                window.key_pressed(GLFW_KEY_RIGHT_SHIFT);

            if (!tab_held) {
                tab_was_pressed = false;
                shift_tab_was_pressed = false;
            } else if (shift_mod && !shift_tab_was_pressed) {
                shift_tab_was_pressed = true;
                tab_was_pressed = true;
                const auto active_render_index =
                    static_cast<std::size_t>(
                        std::clamp(
                            streaming_viewport_index,
                            0,
                            static_cast<int>(viewport_pushes.size()) - 1
                        )
                    );
                auto& active_scene =
                    viewport_scene_states[active_render_index];
                auto& active_push = viewport_pushes[active_render_index];
                auto& active_height_exag =
                    viewport_height_exags[active_render_index];
                const auto n = static_cast<std::uint32_t>(attr_list.size());
                const std::uint32_t new_idx =
                    (static_cast<std::uint32_t>(
                        active_scene.active_height_index) + 1u) % n;
                active_scene.active_height_index = static_cast<int>(new_idx);
                attribute_mapping.apply_height_to(
                    active_push,
                    attr_list[new_idx],
                    active_height_exag
                );
                gs3d::util::log::info() << "[HEIGHT] switched to: " << attr_list[new_idx].name << '\n';
            } else if (!tab_was_pressed) {
                tab_was_pressed = true;
                const auto active_render_index =
                    static_cast<std::size_t>(
                        std::clamp(
                            streaming_viewport_index,
                            0,
                            static_cast<int>(viewport_pushes.size()) - 1
                        )
                    );
                auto& active_scene =
                    viewport_scene_states[active_render_index];
                auto& active_push = viewport_pushes[active_render_index];
                const auto n = static_cast<std::uint32_t>(attr_list.size());
                const std::uint32_t new_idx =
                    (static_cast<std::uint32_t>(
                        active_scene.active_attribute_index) + 1u) % n;
                active_scene.active_attribute_index = static_cast<int>(new_idx);
                const auto& a = attr_list[new_idx];
                active_push.color_source = static_cast<std::uint32_t>(a.source);
                active_push.color_min    = a.min_val;
                active_push.color_range  = a.range();
                if (active_push.color_range <= 0.0f) {
                    active_push.color_range = 1.0f;
                }
                navigation_map_for_view(
                    app_state,
                    static_cast<int>(active_render_index)
                ).dirty = true;
                gs3d::util::log::info() << "[COLOR] switched to: " << a.name << '\n';
            }

            sync_camera_link_groups(app_state, camera_hub);

            bool interacting = false;
            bool camera_changed = false;
            for (const auto& frame : gui_cmds.viewport_frames) {
                if (!viewport_cameras.contains(frame.index)) {
                    continue;
                }

                if ((frame.hovered || frame.active) &&
                    streaming_viewport_index != frame.index) {
                    streaming_viewport_index = frame.index;
                    tile_selection_dirty = true;
                }

                const auto camera_update = viewport_cameras.update(
                    frame,
                    viewport_manager.camera(frame.index)
                );
                interacting = interacting || camera_update.interacting;
                if (camera_update.camera_changed) {
                    camera_changed = true;
                    streaming_viewport_index = frame.index;
                    camera_hub.propagate(frame.index);
                }
            }

            if (!benchmark_pick_enabled &&
                benchmark_session.should_orbit()) {
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

            benchmark_camera_update_ms_frame =
                benchmark_camera_timer.elapsed_milliseconds();

            if (camera_changed) {
                tile_selection_dirty = true;
            }

            gs3d::util::Stopwatch benchmark_lod_tile_timer;
            const auto flush_benchmark_lod_tile_stage =
                [&]() {
                    benchmark_lod_tile_select_ms_frame +=
                        benchmark_lod_tile_timer.elapsed_milliseconds();
                    benchmark_lod_tile_timer.reset();
                };

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

            if (config_.lod.enabled) {
                lod_selector.update(
                    interacting,
                    delta_seconds
                );
            }

            if (config_.tile.enabled && tile_reader.has_value()) {
                auto tile_config =
                    make_tile_selection_config(config_);
                const auto stream_index =
                    static_cast<std::size_t>(
                        std::clamp(
                            streaming_viewport_index,
                            0,
                            static_cast<int>(viewport_pushes.size()) - 1
                        )
                    );
                const auto& stream_push = viewport_pushes[stream_index];
                tile_config.height_offset = stream_push.height_offset;
                tile_config.height_mult = stream_push.height_mult;
                tile_config.height_source = stream_push.height_source;
                tile_selection.set_config(tile_config);
            }

            {
                ViewerAppTileStreamFrameContext tile_ctx{
                    .context = context,
                    .renderer = renderer,
                    .viewport_manager = viewport_manager,
                    .tile_reader =
                        tile_reader.has_value() ? &*tile_reader : nullptr,
                    .tile_point_ids_by_tile = tile_point_ids_by_tile,
                    .tile_gpu_cloud = tile_gpu_cloud.get(),
                    .tile_selection = tile_selection,
                    .tile_result = tile_result,
                    .tile_index_view = tile_index_view,
                    .runtime_points_by_id = runtime_points_by_id,
                    .runtime_points_valid_by_id = runtime_points_valid_by_id,
                    .streaming_viewport_index = streaming_viewport_index,
                    .camera_changed = camera_changed,
                    .interacting = interacting,
                    .current_time = current_time,
                    .tile_selection_dirty = tile_selection_dirty,
                    .lod_tile_timer = benchmark_lod_tile_timer,
                    .lod_tile_select_ms_frame =
                        benchmark_lod_tile_select_ms_frame,
                    .cpu_cull_ms_frame = benchmark_cpu_cull_ms_frame,
                    .upload_record_ms_frame =
                        benchmark_upload_record_ms_frame,
                    .reload_seconds = benchmark_session.samples().reload_seconds
                };
                update_tile_streaming(tile_stream, tile_ctx);
            }
            // Select LOD level once per frame (not per-viewport) so all views
            // use the same level and the verbose log fires at most once.
            if (config_.lod.enabled && lod_gpu_cloud) {
                const auto voxel_sizes = lod_gpu_cloud->voxel_sizes();
                float world_per_pixel = 0.0f;
                float spatial_ortho_h = 0.0f;
                {
                    const auto& cam =
                        viewport_manager.camera(streaming_viewport_index);
                    spatial_ortho_h = cam.ortho_height();
                    const float vp_h =
                        static_cast<float>(cam.viewport_height());
                    if (vp_h > 0.0f) {
                        world_per_pixel = spatial_ortho_h / vp_h;
                    }
                }

                const auto selection = viewport_lod.select(
                    lod_selector,
                    voxel_sizes,
                    world_per_pixel,
                    interacting,
                    config_.lod.interactive_display_mode ==
                        gs3d::app::InteractiveDisplayMode::AllowCoarseLOD,
                    config_.lod.high_delay_seconds
                );
                if (selection) {
                    lod_level_for_frame = selection->level;
                    if (selection->used_fallback) {
                        gs3d::util::log::warning()
                            << "[WARN] LOD level out of range: "
                            << selection->requested_level
                            << " >= " << voxel_sizes.size()
                            << ", falling back to "
                            << selection->fallback_level << "\n";
                    }

                    if (selection->changed) {
                        if (config_.lod.verbose) {
                            const auto& level =
                                lod_gpu_cloud->level(lod_level_for_frame);
                            gs3d::util::log::info() << "[LOD] active level = "
                                      << lod_level_for_frame
                                      << ", points = "
                                      << level.gpu_point_count
                                      << ", idle_seconds = "
                                      << lod_selector.idle_seconds()
                                      << ", ortho_h = "
                                      << world_per_pixel
                                      << " m/px\n";
                        }

                        if (const char* env =
                                std::getenv("GS3D_LOD_DEBUG")) {
                            if (env[0] == '1') {
                                gs3d::util::log::info()
                                    << "[LODDBG] ortho_h="
                                    << spatial_ortho_h
                                    << " wpix=" << world_per_pixel
                                    << " spatial="
                                    << selection->spatial_level
                                    << (interacting ? "(frozen)" : "")
                                    << " temporal="
                                    << selection->temporal_level
                                    << " -> level="
                                    << lod_level_for_frame
                                    << " frozen="
                                    << selection->frozen_display_level
                                    << (interacting
                                            ? " (interacting)\n"
                                            : " (idle)\n");
                            }
                        }
                    }
                }
            }

            flush_benchmark_lod_tile_stage();
            std::vector<float> viewport_point_sizes;
            viewport_point_sizes.reserve(viewport_pushes.size());
            for (const auto& view_push : viewport_pushes) {
                viewport_point_sizes.push_back(view_push.point_size);
            }
            prepare_gpu_pick_requests(
                pick,
                viewport_manager,
                app_state,
                gui_cmds,
                viewport_point_sizes,
                benchmark_controller
            );

            renderer.draw_frame(
                window,
                gs3d::render::VulkanRenderer::FrameDrawCallbacks{
                    .frame_ready = [&](std::uint32_t frame_slot) {
                        consume_ready_pick_frame_slot(frame_slot);
                    },
                    // pre_pass: all offscreen render passes execute here, before the
                    // swapchain render pass starts. Each viewport records its own
                    // vkCmdBeginRenderPass / draw / vkCmdEndRenderPass sequence into
                    // cmd; none of them nest inside each other or the swapchain pass.
                    .pre_pass = [&](VkCommandBuffer cmd) {
                        navigation_maps.record_dirty_thumbnails(
                            cmd,
                            app_state,
                            point_pipeline,
                            nav_cloud,
                            viewport_pushes,
                            dataset.bbox_max_z()
                        );

                        ViewerAppViewportDrawContext draw_ctx{
                            .visible_viewports = visible_viewports,
                            .viewport_manager = viewport_manager,
                            .point_pipeline = point_pipeline,
                            .viewport_pushes = viewport_pushes,
                            .lod_gpu_cloud = lod_gpu_cloud.get(),
                            .full_gpu_cloud = full_gpu_cloud.get(),
                            .tile_gpu_cloud = tile_gpu_cloud.get(),
                            .tile_stream = tile_stream,
                            .tile_result = tile_result,
                            .pick = pick,
                            .gpu_pick_readback = gpu_pick_readback,
                            .pick_debug_frame_dumper =
                                pick_debug_frame_dumper,
                            .pending_hover_miss_dump =
                                pending_hover_miss_dump,
                            .pick_debug_dump_count = pick_debug_dump_count,
                            .pick_debug_dump_completed =
                                pick_debug_dump_completed,
                            .lod_level_for_frame = lod_level_for_frame,
                            .interacting = interacting,
                            .benchmark_pick_enabled = benchmark_pick_enabled,
                            .app_frame_index =
                                benchmark_session.app_frame_index(),
                            .benchmark_pick_issue_cpu_ms =
                                benchmark_pick_issue_cpu_ms,
                            .benchmark_pick_issue_metadata =
                                benchmark_pick_issue_metadata
                        };
                        record_viewport_passes(cmd, draw_ctx);
                    },
                    // in_pass: only ImGui runs in the swapchain render pass.
                    // Each ImGui::Image() samples its viewport's offscreen texture.
                    .in_pass = [&](VkCommandBuffer cmd) {
                        imgui_layer.render(cmd);
                    },
                    // post_pass: after the swapchain render pass ends, copy
                    // the viewport region to a staging buffer for screenshots.
                    .post_pass = [&](VkCommandBuffer cmd, std::uint32_t image_index) {
                        screenshot_service.record_copy(
                            cmd,
                            image_index,
                            context,
                            swapchain
                        );
                    }
                }
            );
            const double benchmark_draw_record_ms_frame =
                renderer.last_draw_record_cpu_ms();
            const double benchmark_acquire_wait_ms_frame =
                renderer.last_acquire_wait_ms();
            const double benchmark_frame_fence_wait_ms_frame =
                renderer.last_frame_fence_wait_ms();
            const double benchmark_upload_fence_wait_ms_frame =
                renderer.last_upload_fence_wait_ms();
            benchmark_cpu_frame_ms =
                benchmark_camera_update_ms_frame +
                benchmark_lod_tile_select_ms_frame +
                benchmark_cpu_cull_ms_frame +
                benchmark_upload_record_ms_frame +
                benchmark_draw_record_ms_frame;
            // Render ImGui platform windows (docked panels torn out to separate
            // OS windows). Must happen outside the main render pass.
            imgui_layer.render_platform_windows();
            // If draw_frame() returned early (minimized / swapchain out-of-date)
            // the draw callback was never invoked, so close the dangling ImGui frame.
            imgui_layer.discard_frame();

            // ── Screenshot PNG write ──────────────────────────────────
            screenshot_service.write_pending(context, swapchain);

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

            benchmark_session.record_frame(
                {
                    .wall_frame_ms = benchmark_frame_timer.elapsed_milliseconds(),
                    .cpu_frame_ms = benchmark_cpu_frame_ms,
                    .camera_update_ms = benchmark_camera_update_ms_frame,
                    .lod_tile_select_ms = benchmark_lod_tile_select_ms_frame,
                    .cpu_cull_ms = benchmark_cpu_cull_ms_frame,
                    .upload_record_ms = benchmark_upload_record_ms_frame,
                    .draw_record_ms = benchmark_draw_record_ms_frame,
                    .acquire_wait_ms = benchmark_acquire_wait_ms_frame,
                    .frame_fence_wait_ms = benchmark_frame_fence_wait_ms_frame,
                    .upload_fence_wait_ms = benchmark_upload_fence_wait_ms_frame
                },
                renderer.has_last_gpu_frame_ms()
                    ? std::optional<double>(renderer.last_gpu_frame_ms())
                    : std::nullopt
            );
        }

        vkDeviceWaitIdle(context.device());

        if (benchmark_session.enabled()) {
            print_benchmark_report(
                benchmark_session.samples(),
                swapchain.present_mode()
            );
        }

        if (benchmark_controller.write_pick_results(
                config_.benchmark.pick_result_path
            )) {
            gs3d::util::log::benchmark() << "[BENCH] pick_result_path = "
                      << config_.benchmark.pick_result_path.string()
                      << '\n';
            gs3d::util::log::benchmark() << "[BENCH] pick_result_count = "
                      << benchmark_pick_results.size()
                      << '\n';
        }

        gs3d::util::log::info() << "[PASS] ViewerApp finished.\n";
        return 0;

    } catch (const std::exception& e) {
        gs3d::util::log::error() << "[FAIL] " << e.what() << '\n';
        return 1;
    }
}

} // namespace gs3d::app
