#include "app/ViewerApp.hpp"
#include "util/Log.hpp"
#include "app/ViewerDatasetSession.hpp"
#include "app/ViewerDatasetDescriptor.hpp"
#include "app/ViewerBenchmarkController.hpp"
#include "app/ViewerKeyboardShortcutSystem.hpp"
#include "app/ViewerCameraFrameSystem.hpp"
#include "app/ViewerRenderSettingsSystem.hpp"
#include "app/ViewerRuntimeConfiguration.hpp"
#include "app/ViewerWorkbenchLayout.hpp"
#include "app/NavigationMapSystem.hpp"
#include "app/ViewerAttributeMapping.hpp"
#include "app/ViewerFrameClock.hpp"
#include "app/ViewerFrameMetricsCollector.hpp"
#include "app/ViewerLodFrameSystem.hpp"
#include "app/ViewerFrameStateSynchronizer.hpp"
#include "app/ScreenshotService.hpp"
#include "app/ViewerAppStateInitialization.hpp"
#include "app/ViewerPickSystem.hpp"
#include "app/ViewerAppInternal.hpp"
#include "app/ViewerAppRunState.hpp"
#include "app/ViewerAppTileStreaming.hpp"

#include "app/AppState.hpp"
#include "app/PreprocessedBundle.hpp"
#include "app/TilePointCache.hpp"
#include "app/ViewportCameraSystem.hpp"
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

        gs3d::platform::Window window(make_window_config(config_.window));
        const auto vk_config = make_vulkan_context_config(config_.graphics);

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

        std::optional<DesktopWorkArea> primary_work_area;
        if (GLFWmonitor* monitor = glfwGetPrimaryMonitor(); monitor != nullptr) {
            DesktopWorkArea work_area;
            glfwGetMonitorWorkarea(
                monitor,
                &work_area.x,
                &work_area.y,
                &work_area.width,
                &work_area.height
            );
            primary_work_area = work_area;
        }
        WindowFrameInsets frame_insets;
        glfwGetWindowFrameSize(
            window.native_handle(),
            &frame_insets.left,
            &frame_insets.top,
            &frame_insets.right,
            &frame_insets.bottom
        );
        const auto workbench_layout = compute_workbench_window_layout(
            gs3d::gui::ui_fonts().ui_scale,
            primary_work_area,
            frame_insets
        );
        glfwSetWindowSize(
            window.native_handle(),
            workbench_layout.client_width,
            workbench_layout.client_height
        );
        if (workbench_layout.outer_x.has_value() &&
            workbench_layout.outer_y.has_value()) {
            glfwSetWindowPos(
                window.native_handle(),
                *workbench_layout.outer_x,
                *workbench_layout.outer_y
            );
        }

        // 交换链是 UNORM 格式——配置里的 clear_color 按 sRGB 语义书写，
        // 直接使用，无需颜色空间转换。
        const auto clear_color = make_clear_color(config_.graphics);
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
        initialize_camera_from_config(initial_camera, config_.camera, bounds);

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

        gs3d::render::PointPipeline point_pipeline(
            context,
            viewport_manager.render_pass(),
            make_point_pipeline_config(config_.graphics)
        );

        gs3d::util::log::info() << "[OK] PointPipeline created.\n";

        ViewerPickSystem pick_system(
            context,
            renderer.frames_in_flight(),
            static_cast<std::size_t>(viewport_manager.viewport_count()),
            config_.pick_debug.dump_enabled,
            config_.pick_debug.dump_dir
        );
        auto& pick = pick_system.state();
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

        ViewportCameraSystem viewport_cameras(
            make_camera_controller_config(config_.controller),
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
        TileStreamingSystem tile_streaming(
            config_.tile,
            config_.benchmark.enabled,
            tile_reader
        );
        auto& tile_stream = tile_streaming.state();

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
            lod_selector.set_config(make_lod_selector_config(config_.lod));
        }

        gs3d::render::TileSelection tile_selection;
        gs3d::render::TileSelectionResult tile_result;
        bool tile_selection_dirty = true;
        int streaming_viewport_index = 0;

        if (config_.tile.enabled && tile_reader.has_value()) {
            tile_selection.set_config(
                make_tile_selection_config(config_.tile)
            );

            tile_gpu_cloud =
                std::make_unique<gs3d::render::PointCloudTileGpu>();
            tile_gpu_cloud->set_resident_tile_budget(
                config_.tile.gpu_cache_max_tiles
            );

            gs3d::util::log::info() << "[OK] TileSelection initialized.\n";
        }

        ViewerAttributeMapping attribute_mapping(
            dataset,
            config_.input.primary_value_field_name,
            config_.input.z_field_name
        );
        ViewerKeyboardShortcutSystem keyboard_shortcuts;
        ViewerCameraFrameSystem camera_frame_system(
            std::chrono::milliseconds(150)
        );
        ViewerRenderSettingsSystem render_settings;
        ViewerFrameMetricsCollector frame_metrics_collector;
        const auto& primary_value_name = attribute_mapping.primary_value_name();
        const auto& z_field_name = attribute_mapping.z_field_name();
        const auto& attr_list = attribute_mapping.descriptors();
        float height_exag = 1.0f;
        auto push = attribute_mapping.make_initial_push(
            config_.graphics.initial_point_size,
            height_exag
        );

        ViewerLodFrameSystem lod_frame_system;
        // The system retains the level rendered in frame N-1 so
        // report_frame_time() can pair it with frame N-1's measured duration
        // (delta_seconds, computed at the top of frame N) before selecting
        // this frame's level.

        ViewerFrameClock frame_clock;

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

        ViewerPickFrameContext pick_frame_context{
            runtime_points_by_id,
            runtime_points_valid_by_id,
            viewport_cameras.controllers(),
            selected_focus_points,
            viewport_manager,
            bounds,
            viewport_pushes,
            streaming_viewport_index,
            tile_selection_dirty,
            benchmark_controller,
            resolve_hover_point_from_visible_tiles
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
            const auto frame_clock_tick = frame_clock.tick(current_time);
            const double delta_seconds = frame_clock_tick.delta_seconds;

            pick_system.consume_ready_frames(renderer, pick_frame_context);

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
                lod_selector.report_frame_time(
                    lod_frame_system.current_level(),
                    ViewerFrameClock::estimate_render_work_milliseconds(
                        delta_seconds,
                        present_wait_ms
                    )
                );
            }

            window.poll_events();

            const int n_viewports = viewport_manager.viewport_count();
            const ViewerFrameMetricsContext frame_metrics_context{
                .tile_enabled = config_.tile.enabled,
                .tile_gpu_cloud = tile_gpu_cloud.get(),
                .full_gpu_cloud = full_gpu_cloud.get(),
                .lod_gpu_cloud = lod_gpu_cloud.get(),
                .tile_stream = tile_stream,
                .tile_result = tile_result,
                .tile_reader = tile_reader.has_value() ? &*tile_reader : nullptr,
                .dataset_point_count = dataset.point_count(),
                .fps = frame_clock_tick.smoothed_fps,
                .delta_seconds = delta_seconds,
                .camera_position = format_vec3_text(
                    viewport_manager.camera(streaming_viewport_index).position()
                )
            };
            const auto frame_metrics = frame_metrics_collector.collect(
                frame_metrics_context
            );
            frame_state_synchronizer.synchronize(
                app_state,
                viewport_pushes,
                viewport_scene_states,
                viewport_height_exags,
                frame_metrics.tile_cache,
                frame_metrics.frame_state
            );

            {
                ViewerAppRenderViewContext render_ctx{
                    viewport_manager,
                    dataset,
                    bounds,
                    viewport_pushes,
                    primary_value_name,
                    z_field_name,
                    frame_metrics.frame_state.visible_points,
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
            const auto runtime_viewports =
                viewport_presentation.reconcile_runtime_viewports(
                    app_state,
                    viewport_manager.viewport_count(),
                    streaming_viewport_index,
                    visible_viewports
                );
            viewport_manager.set_active_count(
                runtime_viewports.active_viewport_count
            );
            if (runtime_viewports.streaming_viewport_changed) {
                streaming_viewport_index =
                    runtime_viewports.streaming_viewport_index;
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
                    .camera_config = config_.camera,
                    .bounds = bounds,
                    .streaming_viewport_index = streaming_viewport_index,
                    .tile_selection_dirty = tile_selection_dirty
                };
                apply_reset_camera_command(gui_cmds, cam_ctx);
            }
            render_settings.apply_commands(
                gui_cmds.render_settings_commands,
                app_state,
                viewport_pushes,
                viewport_scene_states,
                attr_list,
                dataset,
                viewport_height_exags
            );
            if (gui_cmds.clear_cache_requested) {
                tile_streaming.clear_cpu_cache();
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

            ViewerKeyboardShortcutContext shortcut_context{
                .window = window,
                .imgui_wants_keyboard = imgui_wants_keyboard,
                .keyboard_shortcuts_allowed = keyboard_shortcuts_allowed,
                .camera_config = config_.camera,
                .app_state = app_state,
                .viewport_manager = viewport_manager,
                .viewport_pushes = viewport_pushes,
                .viewport_scene_states = viewport_scene_states,
                .viewport_height_exaggerations = viewport_height_exags,
                .attributes = attr_list,
                .attribute_mapping = attribute_mapping,
                .viewport_cameras = viewport_cameras,
                .camera_hub = camera_hub,
                .bounds = bounds,
                .selected_focus_points = selected_focus_points,
                .streaming_viewport_index = streaming_viewport_index,
                .tile_selection_dirty = tile_selection_dirty
            };
            keyboard_shortcuts.process(shortcut_context);

            const ViewerCameraFrameContext camera_context{
                .viewport_frames = gui_cmds.viewport_frames,
                .viewport_cameras = viewport_cameras,
                .viewport_manager = viewport_manager,
                .camera_hub = camera_hub,
                .streaming_viewport_index = streaming_viewport_index,
                .benchmark_orbit =
                    !benchmark_pick_enabled && benchmark_session.should_orbit(),
                .current_time = current_time
            };
            const auto camera_frame = camera_frame_system.update(
                camera_context
            );
            streaming_viewport_index = camera_frame.streaming_viewport_index;
            const bool interacting = camera_frame.interacting;
            const bool camera_changed = camera_frame.camera_changed;

            benchmark_camera_update_ms_frame =
                benchmark_camera_timer.elapsed_milliseconds();

            if (camera_frame.streaming_viewport_changed || camera_changed) {
                tile_selection_dirty = true;
            }

            gs3d::util::Stopwatch benchmark_lod_tile_timer;
            const auto flush_benchmark_lod_tile_stage =
                [&]() {
                    benchmark_lod_tile_select_ms_frame +=
                        benchmark_lod_tile_timer.elapsed_milliseconds();
                    benchmark_lod_tile_timer.reset();
                };

            if (config_.lod.enabled) {
                lod_selector.update(
                    interacting,
                    delta_seconds
                );
            }

            if (config_.tile.enabled && tile_reader.has_value()) {
                auto tile_config =
                    make_tile_selection_config(config_.tile);
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
                tile_streaming.update(
                    tile_ctx,
                    config_.tile,
                    config_.benchmark.enabled
                );
            }
            const auto lod_level_for_frame = lod_frame_system.update({
                .lod_gpu_cloud = lod_gpu_cloud.get(),
                .lod_selector = lod_selector,
                .viewport_manager = viewport_manager,
                .streaming_viewport_index = streaming_viewport_index,
                .interacting = interacting,
                .options = {
                    .enabled = config_.lod.enabled,
                    .allow_coarse_while_interacting =
                        config_.lod.interactive_display_mode ==
                            InteractiveDisplayMode::AllowCoarseLOD,
                    .high_delay_seconds = config_.lod.high_delay_seconds,
                    .verbose = config_.lod.verbose
                }
            });

            flush_benchmark_lod_tile_stage();
            std::vector<float> viewport_point_sizes;
            viewport_point_sizes.reserve(viewport_pushes.size());
            for (const auto& view_push : viewport_pushes) {
                viewport_point_sizes.push_back(view_push.point_size);
            }
            pick_system.prepare_requests(
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
                        pick_system.consume_ready_frame_slot(
                            frame_slot,
                            pick_frame_context
                        );
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
                            .pick = pick_system.state(),
                            .gpu_pick_readback = pick_system.gpu_readback(),
                            .pick_debug_frame_dumper =
                                pick_system.debug_frame_dumper(),
                            .pending_hover_miss_dump =
                                pick_system.pending_hover_miss_dump(),
                            .pick_debug_dump_count =
                                pick_system.debug_dump_count(),
                            .pick_debug_dump_completed =
                                pick_system.debug_dump_completed(),
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
