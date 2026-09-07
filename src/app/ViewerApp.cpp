#include "app/ViewerApp.hpp"
#include "util/Log.hpp"
#include "app/ViewerDatasetSession.hpp"
#include "app/ViewerDatasetDescriptor.hpp"
#include "app/ViewerBenchmarkController.hpp"
#include "app/ViewerKeyboardShortcutSystem.hpp"
#include "app/ViewerCameraFrameSystem.hpp"
#include "app/ViewerRenderSettingsSystem.hpp"
#include "app/ViewerRenderRuntime.hpp"
#include "app/ViewerViewportRenderSystem.hpp"
#include "app/ViewerRuntimeConfiguration.hpp"
#include "app/NavigationMapSystem.hpp"
#include "app/ViewerAttributeMapping.hpp"
#include "app/ViewerFrameClock.hpp"
#include "app/ViewerFrameRenderer.hpp"
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
#include "app/UserPreferences.hpp"
#include "ui/UiPalette.hpp"
#include "ui/WorkspaceManager.hpp"
#include "ui/Theme.hpp"
#include "ui/UiFonts.hpp"
#include "render/ViewportManager.hpp"

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
#include "render/TileSelection.hpp"
#include "render/VulkanBuffer.hpp"

#include "preprocess/Gs3dLodWriter.hpp"
#include "util/Stopwatch.hpp"
#include "core/SceneState.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <optional>
#include <sstream>

namespace gs3d::app {

namespace {

// TIA-111：处理控制面命令产生的动作，合并到 gui_cmds 并清空 control_actions。
void apply_control_actions(
    AppState& app_state,
    UiActions& gui_cmds
) {
    auto& ca = app_state.control_actions;
    if (ca.open_requested) { gui_cmds.open_requested = true; }
    if (ca.open_bundle_requested) { gui_cmds.open_bundle_requested = true; }
    if (ca.show_welcome_requested) { gui_cmds.show_welcome_requested = true; }
    if (ca.screenshot_requested) { gui_cmds.screenshot_requested = true; }
    if (ca.restore_default_workspace_requested) {
        gui_cmds.restore_default_workspace_requested = true;
    }
    if (ca.theme_change_requested) {
        apply_theme(
            static_cast<gs3d::ui::ThemeId>(ca.theme_id),
            gs3d::gui::ui_fonts().ui_scale
        );
        gs3d::app::UiPreferences p;
        p.theme = gs3d::ui::theme_tokens(static_cast<gs3d::ui::ThemeId>(ca.theme_id)).id;
        gs3d::app::save_ui_preferences(p);
    }
    if (ca.reset_camera_index >= 0) {
        gui_cmds.reset_camera_index = ca.reset_camera_index;
    }
    if (ca.camera_view_axis == -2) {
        // 特殊值：新建视图
        if (gs3d::ui::has_hidden_view(app_state)) {
            gs3d::ui::show_first_hidden_view(app_state);
        }
    } else if (ca.camera_view_axis == -3) {
        // 特殊值：新建工作窗口
        if (gs3d::ui::has_hidden_view(app_state)) {
            gs3d::ui::create_workspace_window(app_state);
        }
    } else if (ca.camera_view_axis >= 0) {
        gui_cmds.camera_view_axis = ca.camera_view_axis;
    }
    gui_cmds.render_settings_commands.insert(gui_cmds.render_settings_commands.end(), ca.render_settings_commands.begin(), ca.render_settings_commands.end());
    ca = AppState::ControlActions{};  // 清空
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

ViewerViewportRenderOptions make_viewport_render_options(
    const ViewerLodConfig& lod_config,
    const ViewerPickDebugConfig& pick_debug_config
) {
    return {
        .lod_enabled = lod_config.enabled,
        .render_tiles_while_interacting =
            lod_config.interactive_display_mode !=
                InteractiveDisplayMode::AllowCoarseLOD,
        .pick_debug_dump_enabled = pick_debug_config.dump_enabled,
        .pick_debug_dump_once_on_hover =
            pick_debug_config.dump_once_on_hover,
    };
}

void print_controls(bool lod_enabled, bool tile_enabled) {
    gs3d::util::log::info() << "[OK] Entering render loop.\n"
        << "操作: 左键拖动=轨道旋转, 右键拖动=视角平移, 滚轮=缩放, "
        << "Ctrl+左键=框选, 双击=选择, F=聚焦, +/-=点大小, R=恢复, Tab=属性, Esc=退出\n"
        << "LOD: " << (lod_enabled ? "启用" : "关闭")
        << ", 瓦片: " << (tile_enabled ? "启用" : "关闭") << '\n';
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
        ViewerRenderRuntime render_runtime(
            window,
            {
                .window_config = config_.window,
                .graphics_config = config_.graphics,
                .benchmark_config = config_.benchmark,
                .camera_config = config_.camera,
                .lod_config = config_.lod,
                .tile_config = config_.tile,
                .dataset = dataset,
                .full_point_ids = full_point_ids,
                .lod_dataset = lod_dataset,
                .lod_point_ids = lod_point_ids,
                .tile_reader_available = tile_reader.has_value(),
                .on_context_created = [&startup_timer]() {
                    gs3d::util::log::info()
                        << "[TIME] viewer.startup_seconds = "
                        << startup_timer.elapsed_seconds() << '\n';
                }
            }
        );
        auto& context = render_runtime.context();
        auto& swapchain = render_runtime.swapchain();
        auto& renderer = render_runtime.renderer();
        auto& imgui_layer = render_runtime.imgui_layer();
        auto& viewport_manager = render_runtime.viewport_manager();
        auto& point_pipeline = render_runtime.point_pipeline();
        const auto& bounds = render_runtime.bounds();
        auto* full_gpu_cloud = render_runtime.full_gpu_cloud();
        auto* lod_gpu_cloud = render_runtime.lod_gpu_cloud();
        auto* tile_gpu_cloud = render_runtime.tile_gpu_cloud();
        ViewerViewportRenderSystem viewport_render_system(
            make_viewport_render_options(config_.lod, config_.pick_debug)
        );

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

        const auto& cam0 = viewport_manager.camera(0);
        gs3d::util::log::info() << "[OK] CameraController initialized.\n"
            << "pos=[" << cam0.position().x << "," << cam0.position().y << "," << cam0.position().z << "] "
            << "target=[" << cam0.target().x << "," << cam0.target().y << "," << cam0.target().z << "] "
            << "dist=" << cam0.distance() << '\n';

        /*
         * 瓦片流式状态（见 ViewerAppTileStreaming.hpp）；须声明在
         * tile_reader 之后：后台 future 引用它们，析构时先 join。
         */
        TileStreamingSystem tile_streaming(
            config_.tile,
            config_.benchmark.enabled,
            tile_reader
        );
        auto& tile_stream = tile_streaming.state();

        const auto resolve_hover_point_from_visible_tiles =
            [&tile_stream](std::size_t view_index, std::uint32_t point_id,
                           float mouse_x, float mouse_y) {
                static_cast<void>(mouse_x); static_cast<void>(mouse_y);
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

        ViewerLodFrameSystem lod_frame_system;  // retains N-1 level for report_frame_time
        ViewerFrameClock frame_clock;

        print_controls(config_.lod.enabled, config_.tile.enabled);

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
        AppState app_state = make_initial_viewer_app_state(
            initial_state_input, config_.window.layout,
            gs3d::app::load_render_settings_preferences());
        viewport_presentation.initialize_visibility(app_state);

        app_state.logo_texture = (TextureHandle)render_runtime.logo_descriptor();

        // analysis.toml persistence
        app_state.bundle_dir = config_.input.bundle_dir;
        auto& meas_ref = app_state.measurements.empty()
            ? app_state.measurement : app_state.measurements.front();
        load_analysis(app_state.bundle_dir, meas_ref);
        app_state.measurement = meas_ref;
        meas_ref.on_changed = [&app_state]() {
            const auto& m = app_state.measurements.empty()
                ? app_state.measurement : app_state.measurements.front();
            save_analysis(app_state.bundle_dir, m);
        };

        const auto& nav_cloud = render_runtime.navigation_cloud();
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
        visible_viewports.reserve(viewport_manager.viewport_count());
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

        // ponytail: screenshot staging — one capture at a time.
        ScreenshotService screenshot_service;
        // TIA-109: 控制面会话（TCP+JSON-RPC+帧同步截图）；未启用时零开销。
        ControlPlaneSession control_session(
            config_.control_plane, app_state, screenshot_service, swapchain
        );
        ViewerFrameRenderer frame_renderer(
            window,
            context,
            swapchain,
            renderer,
            imgui_layer,
            navigation_maps,
            point_pipeline,
            nav_cloud,
            viewport_render_system,
            pick_system,
            pick_frame_context,
            benchmark_controller,
            screenshot_service,
            dataset.bbox_max_z()
        );

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

            // 配对上一帧 LOD 级别与实测时长（须在 lod_level_for_frame
            // 重赋值前运行，否则 no-op）。
            if (config_.lod.enabled && delta_seconds > 0.0) {
                // FIFO vsync 的 present-wait 计入墙钟帧时长，减去后才不会
                // 让自适应 LOD 预算（14ms）在 FIFO 系统上误判。
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
                .tile_gpu_cloud = tile_gpu_cloud,
                .full_gpu_cloud = full_gpu_cloud,
                .lod_gpu_cloud = lod_gpu_cloud,
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
            // TIA-109: 请求在帧边界应用 —— 必须在 new_frame 之前，命令的
            // 效果才会出现在本帧 UI 绘制与截图里（帧同步，不会截到上一帧）。
            control_session.poll();
            if (control_session.quit_requested()) window.request_close();

            auto gui_cmds = imgui_layer.new_frame(app_state);
            apply_control_actions(app_state, gui_cmds);
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

            const bool ui_wants_keyboard = false;
            const bool keyboard_shortcuts_allowed = true;
            gs3d::util::Stopwatch benchmark_camera_timer;
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
            apply_camera_view_axis_command(gui_cmds, cam_ctx); // nav-ball click
            render_settings.apply_commands(
                gui_cmds.render_settings_commands,
                app_state,
                viewport_pushes,
                viewport_scene_states,
                attr_list,
                dataset,
                viewport_height_exags
            );
            if (gui_cmds.clear_cache_requested ||
                (!benchmark_pick_enabled && benchmark_session.should_force_tile_reload())) {
                tile_streaming.clear_cache(renderer, tile_gpu_cloud);
                tile_selection_dirty = true;
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
                .ui_wants_keyboard = ui_wants_keyboard,
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
                    .tile_gpu_cloud = tile_gpu_cloud,
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
                    .reload_samples = benchmark_session.samples().tile_reload_samples
                };
                tile_streaming.update(
                    tile_ctx,
                    config_.tile,
                    config_.benchmark.enabled
                );
            }
            const auto lod_level_for_frame = lod_frame_system.update({
                .lod_gpu_cloud = lod_gpu_cloud,
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

            const auto render_measurements = frame_renderer.render({
                .app_state = app_state,
                .visible_viewports = visible_viewports,
                .viewport_manager = viewport_manager,
                .viewport_pushes = viewport_pushes,
                .lod_gpu_cloud = lod_gpu_cloud,
                .full_gpu_cloud = full_gpu_cloud,
                .tile_gpu_cloud = tile_gpu_cloud,
                .tile_stream = tile_stream,
                .tile_result = tile_result,
                .lod_level_for_frame = lod_level_for_frame,
                .interacting = interacting
            });
            const double benchmark_draw_record_ms_frame =
                render_measurements.draw_record_ms;
            const double benchmark_acquire_wait_ms_frame =
                render_measurements.acquire_wait_ms;
            const double benchmark_frame_fence_wait_ms_frame =
                render_measurements.frame_fence_wait_ms;
            const double benchmark_upload_fence_wait_ms_frame =
                render_measurements.upload_fence_wait_ms;
            benchmark_cpu_frame_ms =
                benchmark_camera_update_ms_frame +
                benchmark_lod_tile_select_ms_frame +
                benchmark_cpu_cull_ms_frame +
                benchmark_upload_record_ms_frame +
                benchmark_draw_record_ms_frame;
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
