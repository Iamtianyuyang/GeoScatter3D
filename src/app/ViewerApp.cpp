#include "app/ViewerApp.hpp"

#include "camera/Camera.hpp"
#include "camera/CameraController.hpp"
#include "data/Gs3dDataset.hpp"
#include "data/Gs3dLodDataset.hpp"
#include "data/Gs3dLodReader.hpp"
#include "data/Gs3dTileReader.hpp"

#include "platform/Window.hpp"
#include "render/LodSelector.hpp"
#include "render/PointCloudGpu.hpp"
#include "render/PointCloudLodGpu.hpp"
#include "render/PointPipeline.hpp"
#include "render/VulkanContext.hpp"
#include "render/VulkanRenderer.hpp"
#include "render/VulkanSwapchain.hpp"
#include "render/PointCloudTileGpu.hpp"
#include "render/TileSelection.hpp"

#include "preprocess/Gs3dLodWriter.hpp"

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <optional>

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

    tile_config.use_full_z_range =
        config.tile_use_full_z_range;

    return tile_config;
}

bool same_query_box(
    const std::optional<gs3d::data::Gs3dTileQueryBox>& loaded_box,
    const gs3d::data::Gs3dTileQueryBox& current_box
) noexcept {
    if (!loaded_box.has_value()) {
        return false;
    }

    constexpr float epsilon = 1.0e-3f;
    const auto close = [epsilon](float a, float b) noexcept {
        return std::abs(a - b) <= epsilon;
    };

    const auto& box = *loaded_box;

    return close(box.min_x, current_box.min_x) &&
           close(box.min_y, current_box.min_y) &&
           close(box.min_z, current_box.min_z) &&
           close(box.max_x, current_box.max_x) &&
           close(box.max_y, current_box.max_y) &&
           close(box.max_z, current_box.max_z);
}

gs3d::data::Gs3dLodDataset build_runtime_lod_dataset(
    const gs3d::data::Gs3dDataset& dataset,
    const ViewerAppConfig& config
) {
    gs3d::data::Gs3dLodBuildConfig lod_config;
    lod_config.include_full_resolution_level = false;
    lod_config.target_point_counts =
        config.lod_target_point_counts;
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
    const gs3d::camera::Camera& camera,
    const gs3d::data::Gs3dDataset& dataset
) {
    const auto mvp = camera.view_projection_matrix();

    std::copy(
        mvp.m.begin(),
        mvp.m.end(),
        push.mvp
    );

    push.value_min = dataset.value_min();
    push.value_range = dataset.value_max() - dataset.value_min();

    if (push.value_range <= 0.0f) {
        push.value_range = 1.0f;
    }

    push.clip_mode = 0.0f;
}

void print_dataset_info(
    const gs3d::data::Gs3dDataset& dataset
) {
    std::cout << "[OK] Dataset loaded.\n";
    std::cout << "point_count = " << dataset.point_count() << '\n';
    std::cout << "point_bytes = " << dataset.point_bytes() << '\n';

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
    std::cout << "Controls:\n";
    std::cout << "  Left drag   : orbit\n";
    std::cout << "  Right drag  : pan\n";
    std::cout << "  Middle drag : pan\n";
    std::cout << "  Wheel       : zoom\n";
    std::cout << "  + / -       : point size\n";
    std::cout << "  R           : reset view\n";
    std::cout << "  Esc         : quit\n";
    std::cout << "Render mode:\n";
    std::cout << "  LOD         : "
              << (lod_enabled ? "enabled" : "disabled")
              << '\n';
    std::cout << "  Tile full-res : "
              << (tile_enabled ? "enabled" : "disabled")
              << '\n';
}

bool window_interacting(
    const gs3d::platform::Window& window
) {
    const auto& mouse = window.mouse_state();

    return mouse.left_pressed ||
           mouse.right_pressed ||
           mouse.middle_pressed ||
           mouse.scroll_y != 0.0;
}

} // namespace

ViewerApp::ViewerApp(ViewerAppConfig config)
    : config_(std::move(config))
{
}

int ViewerApp::run() {
    try {
        const auto dataset =
            gs3d::data::Gs3dDatasetLoader::load(config_.gs3d_path);

        if (!dataset.is_consistent()) {
            std::cerr << "[FAIL] dataset is inconsistent.\n";
            return 1;
        }

        if (dataset.empty()) {
            std::cerr << "[FAIL] dataset is empty.\n";
            return 1;
        }

        print_dataset_info(dataset);

        std::optional<gs3d::data::Gs3dTileReader> tile_reader;

        if (config_.tile_enabled) {
            tile_reader =
                gs3d::data::Gs3dTileReader::open(
                    config_.tile_index_path,
                    config_.tile_data_path,
                    dataset.header()
                );

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
        }

        gs3d::data::Gs3dLodDataset lod_dataset;

        if (config_.lod_enabled) {
            lod_dataset =
                load_or_build_lod_dataset(
                    dataset,
                    config_
                );
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

        std::cout << "[OK] VulkanContext created.\n";
        std::cout << "Physical device: "
                  << context.physical_device_name() << '\n';

        gs3d::render::VulkanSwapchain swapchain(context, window);
        gs3d::render::VulkanRenderer renderer(context, swapchain);

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
            lod_gpu_cloud =
                std::make_unique<gs3d::render::PointCloudLodGpu>(
                    context,
                    renderer.command_pool(),
                    context.graphics_queue(),
                    lod_dataset
                );

            std::cout << "[OK] PointCloudLodGpu uploaded.\n";
            std::cout << lod_gpu_cloud->summary();

        } else {
            full_gpu_cloud =
                std::make_unique<gs3d::render::PointCloudGpu>(
                    context,
                    renderer.command_pool(),
                    context.graphics_queue(),
                    dataset
                );

            std::cout << "[OK] PointCloudGpu uploaded.\n";
            std::cout << "gpu point_count = "
                      << full_gpu_cloud->point_count()
                      << '\n';
        }

        gs3d::render::PointPipelineConfig pipeline_config;
        pipeline_config.vertex_shader_path =
            config_.vertex_shader_path;
        pipeline_config.fragment_shader_path =
            config_.fragment_shader_path;

        gs3d::render::PointPipeline point_pipeline(
            context,
            renderer.render_pass(),
            pipeline_config
        );

        std::cout << "[OK] PointPipeline created.\n";

        const gs3d::camera::CameraBounds bounds =
            make_camera_bounds(dataset);

        gs3d::camera::Camera camera;
        camera.set_viewport(
            config_.window_width,
            config_.window_height
        );

        initialize_camera_from_config(
            camera,
            config_,
            bounds
        );

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

        gs3d::camera::CameraController controller(controller_config);
        controller.set_bounds(bounds);

        std::cout << "[OK] CameraController initialized.\n";
        std::cout << "camera position = ["
                  << camera.position().x << ", "
                  << camera.position().y << ", "
                  << camera.position().z << "]\n";

        std::cout << "camera target = ["
                  << camera.target().x << ", "
                  << camera.target().y << ", "
                  << camera.target().z << "]\n";

        std::cout << "camera distance = "
                  << camera.distance() << '\n';

        gs3d::render::PointPushConstants push{};
        push.point_size = config_.initial_point_size;

        fill_push_constants(
            push,
            camera,
            dataset
        );

        /*
         * Tracks the query box of the tile buffer currently on the GPU.
         * Used to clip LOD draws so LOD points don't overlap full-res tiles.
         * Reset when the tile buffer is cleared.
         */
        std::optional<gs3d::data::Gs3dTileQueryBox> loaded_tile_query_box;

        /*
         * 异步磁盘读取（Potree/Cesium 模式）：
         * 后台线程读取 tile 数据，主线程每帧非阻塞检查 future 是否完成。
         * GPU upload 仍在主线程，调用前用 in-flight fence 代替 vkDeviceWaitIdle。
         */
        struct TileLoadResult {
            std::vector<gs3d::data::Gs3dPoint>  points;
            std::vector<std::uint64_t>          tile_ids;
            gs3d::data::Gs3dTileQueryBox        actual_bbox;
        };

        std::future<TileLoadResult> tile_load_future;
        // IDs dispatched to background thread (may differ from current selection)
        std::vector<std::uint64_t> tile_loading_ids;

        gs3d::render::LodSelector lod_selector;

        if (config_.lod_enabled) {
            gs3d::render::LodSelectorConfig lod_selector_config;
            lod_selector_config.medium_delay_seconds =
                config_.lod_medium_delay_seconds;
            lod_selector_config.high_delay_seconds =
                config_.lod_high_delay_seconds;
            lod_selector_config.use_lowest_while_interacting =
                config_.lod_use_lowest_while_interacting;

            lod_selector.set_config(lod_selector_config);
        }

        gs3d::render::TileSelection tile_selection;

        if (config_.tile_enabled && tile_reader.has_value()) {
            tile_selection.set_config(
                make_tile_selection_config(config_)
            );

            tile_gpu_cloud =
                std::make_unique<gs3d::render::PointCloudTileGpu>();

            std::cout << "[OK] TileSelection initialized.\n";
        }

        bool r_was_pressed = false;

        std::size_t last_lod_level =
             static_cast<std::size_t>(-1);

        auto previous_time =
            std::chrono::steady_clock::now();

        print_controls(
                        config_.lod_enabled,
                        config_.tile_enabled
                    );

        while (!window.should_close()) {
            const auto current_time =
                std::chrono::steady_clock::now();

            const double delta_seconds =
                std::chrono::duration<double>(
                    current_time - previous_time
                ).count();

            previous_time = current_time;

            window.poll_events();

            if (window.key_pressed(GLFW_KEY_ESCAPE)) {
                window.request_close();
            }

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

            const bool r_pressed =
                window.key_pressed(GLFW_KEY_R);

            if (r_pressed && !r_was_pressed) {
                initialize_camera_from_config(
                    camera,
                    config_,
                    bounds
                );

                std::cout << "[OK] Camera reset.\n";
            }

            r_was_pressed = r_pressed;

            controller.update(
                camera,
                window
            );

            fill_push_constants(
                push,
                camera,
                dataset
            );

            const bool interacting = window_interacting(window);

            if (config_.lod_enabled) {
                lod_selector.update(
                    interacting,
                    delta_seconds
                );
            }

            if (config_.tile_enabled &&
                tile_reader.has_value() &&
                tile_gpu_cloud) {
                const auto tile_result =
                    tile_selection.update(camera, *tile_reader);

                if (!tile_result.enabled) {
                    // Tile mode disabled (camera too far away).
                    // Cancel pending load then release GPU buffer.
                    if (tile_load_future.valid()) {
                        tile_load_future.wait();
                        tile_load_future = {};
                        tile_loading_ids.clear();
                    }
                    if (tile_gpu_cloud->valid()) {
                        renderer.wait_for_in_flight_fences();
                        tile_gpu_cloud->clear();
                        loaded_tile_query_box.reset();
                        if (config_.tile_verbose) {
                            std::cout << "[TILE] disabled, buffer cleared.\n";
                        }
                    }
                } else {
                    /*
                     * Potree/Cesium 异步加载模式：
                     *
                     * 1. 每帧非阻塞检查后台磁盘读取是否完成
                     *    → 完成后在主线程做 GPU upload（用 in-flight fence 代替
                     *      vkDeviceWaitIdle，只等渲染帧完成而非整个 GPU 队列）
                     *
                     * 2. 交互停止后，若 buffer 与当前选择不一致，
                     *    派发新的后台加载请求（不阻塞渲染循环）
                     *
                     * 3. 交互期间：不渲染 tile cloud（仅渲染 LOD）
                     *    → 消除旋转/拖动/缩放时的 GPU 负载
                     */

                    // ── Step 1: apply completed load ─────────────────────
                    if (tile_load_future.valid() &&
                        tile_load_future.wait_for(std::chrono::seconds(0))
                            == std::future_status::ready) {

                        auto loaded = tile_load_future.get();
                        tile_load_future = {};

                        if (loaded.tile_ids == tile_result.tile_ids) {
                            // Selection unchanged since load was dispatched
                            renderer.wait_for_in_flight_fences();
                            tile_gpu_cloud->upload_from_points(
                                context,
                                renderer.command_pool(),
                                context.graphics_queue(),
                                std::move(loaded.points),
                                loaded.tile_ids
                            );
                            loaded_tile_query_box = loaded.actual_bbox;

                            if (config_.tile_verbose) {
                                const auto& stats = tile_gpu_cloud->stats();
                                std::cout << "[TILE] async upload complete.\n";
                                std::cout << "tile_count = "
                                          << stats.tile_count << '\n';
                                std::cout << "point_count = "
                                          << stats.point_count << '\n';
                                std::cout << "gpu_buffer_bytes = "
                                          << stats.gpu_buffer_bytes << '\n';
                            }
                        }
                        // If selection changed while loading, discard result;
                        // a new load will be dispatched below.
                        tile_loading_ids.clear();
                    }

                    // ── Step 2: dispatch new load if needed ───────────────
                    if (!interacting) {
                        const auto& loaded_ids =
                            tile_gpu_cloud->loaded_tile_ids();

                        const bool buffer_stale =
                            loaded_ids.size() != tile_result.tile_ids.size() ||
                            !std::equal(
                                loaded_ids.begin(), loaded_ids.end(),
                                tile_result.tile_ids.begin()
                            );

                        const bool load_in_progress =
                            tile_load_future.valid() &&
                            tile_load_future.wait_for(std::chrono::seconds(0))
                                != std::future_status::ready;

                        if (buffer_stale && !load_in_progress &&
                            tile_loading_ids != tile_result.tile_ids) {

                            // Compute actual data bbox on main thread
                            // (record metadata, no I/O)
                            gs3d::data::Gs3dTileQueryBox actual_bbox;
                            actual_bbox.min_x = actual_bbox.min_y =
                                actual_bbox.min_z =
                                    std::numeric_limits<float>::max();
                            actual_bbox.max_x = actual_bbox.max_y =
                                actual_bbox.max_z =
                                    -std::numeric_limits<float>::max();
                            for (const auto tid : tile_result.tile_ids) {
                                const auto& rec = tile_reader->record(tid);
                                actual_bbox.min_x = std::min(actual_bbox.min_x, rec.bbox_min_x);
                                actual_bbox.min_y = std::min(actual_bbox.min_y, rec.bbox_min_y);
                                actual_bbox.min_z = std::min(actual_bbox.min_z, rec.bbox_min_z);
                                actual_bbox.max_x = std::max(actual_bbox.max_x, rec.bbox_max_x);
                                actual_bbox.max_y = std::max(actual_bbox.max_y, rec.bbox_max_y);
                                actual_bbox.max_z = std::max(actual_bbox.max_z, rec.bbox_max_z);
                            }

                            tile_loading_ids = tile_result.tile_ids;
                            const auto ids = tile_result.tile_ids;
                            const auto& tr  = *tile_reader;

                            tile_load_future = std::async(
                                std::launch::async,
                                [&tr, ids, actual_bbox]() -> TileLoadResult {
                                    TileLoadResult r;
                                    r.tile_ids   = ids;
                                    r.actual_bbox = actual_bbox;
                                    r.points =
                                        gs3d::render::PointCloudTileGpu::read_tiles(
                                            tr, ids
                                        );
                                    return r;
                                }
                            );

                            if (config_.tile_verbose) {
                                std::cout << "[TILE] async load dispatched, "
                                          << ids.size() << " tiles.\n";
                            }
                        }
                    }
                }
            }

            renderer.draw_frame(
                window,
                [&](VkCommandBuffer command_buffer) {
                    /*
                     * Build a LOD push-constant variant that clips out the
                     * region covered by the loaded full-res tiles, so LOD
                     * points no longer overlap with the precise local data.
                     */
                    gs3d::render::PointPushConstants lod_push = push;
                    // 只有 tile cloud 本帧会被渲染时才裁剪 LOD。
                    // 交互期间 tile cloud 被跳过，若此时仍裁剪 LOD 则全黑。
                    const bool tile_will_render =
                        tile_gpu_cloud &&
                        tile_gpu_cloud->valid() &&
                        !interacting;
                    if (tile_will_render &&
                        loaded_tile_query_box.has_value()) {
                        const auto& b = *loaded_tile_query_box;
                        lod_push.clip_mode  = 1.0f;
                        lod_push.clip_min[0] = b.min_x;
                        lod_push.clip_min[1] = b.min_y;
                        lod_push.clip_min[2] = b.min_z;
                        lod_push.clip_min[3] = 0.0f;
                        lod_push.clip_max[0] = b.max_x;
                        lod_push.clip_max[1] = b.max_y;
                        lod_push.clip_max[2] = b.max_z;
                        lod_push.clip_max[3] = 0.0f;
                    }

                    if (config_.lod_enabled) {
                        const std::size_t level_index =
                            lod_selector.select_level(
                                lod_gpu_cloud->level_count()
                            );
                        if (level_index != last_lod_level) {
                            if (config_.lod_verbose) {
                                const auto& level =
                                    lod_gpu_cloud->level(level_index);

                                std::cout << "[LOD] active level = "
                                        << level_index
                                        << ", points = "
                                        << level.gpu_point_count
                                        << ", idle_seconds = "
                                        << lod_selector.idle_seconds()
                                        << '\n';
                            }

                            last_lod_level = level_index;
                        }
                        const auto& cloud =
                            lod_gpu_cloud->gpu_cloud(level_index);

                        point_pipeline.draw(
                            command_buffer,
                            cloud,
                            renderer.extent(),
                            lod_push
                        );
                    } else {
                        point_pipeline.draw(
                            command_buffer,
                            *full_gpu_cloud,
                            renderer.extent(),
                            lod_push
                        );
                    }
                    /*
                     * Potree 策略：交互期间只渲染 LOD，跳过 tile cloud。
                     * tile cloud 可能有数百万点，每帧渲染代价高；
                     * 用户移动相机时不需要精细细节，低质 LOD 已够用。
                     * 停止操作后，tile cloud 加载完成后立即显示。
                     */
                    if (tile_gpu_cloud && tile_gpu_cloud->valid() && !interacting) {
                        point_pipeline.draw(
                            command_buffer,
                            tile_gpu_cloud->gpu_cloud(),
                            renderer.extent(),
                            push
                        );
                    }
                }
            );
        }

        vkDeviceWaitIdle(context.device());

        std::cout << "[PASS] ViewerApp finished.\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "[FAIL] " << e.what() << '\n';
        return 1;
    }
}

} // namespace gs3d::app
