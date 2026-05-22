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
#include "util/Stopwatch.hpp"

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
#include <mutex>
#include <string>
#include <optional>
#include <sstream>
#include <unordered_map>

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

[[nodiscard]]
std::string selection_cache_key(
    const std::vector<std::uint64_t>& tile_ids
) {
    std::string key;
    key.reserve(tile_ids.size() * 10);

    for (const auto tid : tile_ids) {
        key += std::to_string(tid);
        key.push_back(',');
    }

    return key;
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
    const gs3d::camera::Camera& camera
) {
    const auto mvp = camera.view_projection_matrix();
    std::copy(mvp.m.begin(), mvp.m.end(), push.mvp);
    // value_min / value_range / attr_index are managed by the
    // attribute-selection system and must not be overwritten here.
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
    std::cout << "  Tab         : cycle color attribute\n";
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

constexpr double kTileSelectionDebounceSeconds = 0.12;

} // namespace

ViewerApp::ViewerApp(ViewerAppConfig config)
    : config_(std::move(config))
{
}

int ViewerApp::run() {
    try {
        gs3d::util::Stopwatch startup_timer;

        gs3d::util::Stopwatch dataset_load_timer;
        const auto dataset =
            gs3d::data::Gs3dDatasetLoader::load(config_.gs3d_path);
        std::cout << "[TIME] viewer.dataset_load_seconds = "
                  << dataset_load_timer.elapsed_seconds()
                  << '\n';

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
        }

        gs3d::data::Gs3dLodDataset lod_dataset;

        if (config_.lod_enabled) {
            gs3d::util::Stopwatch lod_timer;
            lod_dataset =
                load_or_build_lod_dataset(
                    dataset,
                    config_
                );
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
        push.point_size  = config_.initial_point_size;
        push.attr_index  = 0;   // default: value attribute (amplitude)
        push.value_min   = dataset.value_min();
        push.value_range = dataset.value_max() - dataset.value_min();
        if (push.value_range <= 0.0f) push.value_range = 1.0f;

        fill_push_constants(push, camera);

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
            std::vector<std::uint64_t>          tile_ids;
            gs3d::data::Gs3dTileQueryBox        actual_bbox;
            double                              read_seconds = 0.0;
            std::size_t                         cache_hit_tiles = 0;
            std::size_t                         cache_miss_tiles = 0;
            std::size_t                         candidate_tiles = 0;
        };

        std::future<TileLoadResult> tile_load_future;
        // IDs dispatched to background thread (may differ from current selection)
        std::vector<std::uint64_t> tile_loading_ids;
        std::vector<std::uint64_t> debounced_tile_ids;
        auto tile_selection_changed_at =
            std::chrono::steady_clock::now();
        gs3d::util::Stopwatch tile_async_cycle_timer;

        std::unordered_map<
            std::uint64_t,
            std::shared_ptr<std::vector<gs3d::data::Gs3dPoint>>
        > tile_point_cache;
        std::mutex tile_cache_mutex;

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
         * 当前支持的属性：
         *   0 = value   (导入时存储的属性，如振幅/反射率)
         *   1 = z       (高程/深度)
         */
        struct AttrDesc {
            const char* name;
            float min_val;
            float range;
        };
        const std::array<AttrDesc, 2> attr_table = {{
            { "value (amplitude)",
              dataset.value_min(),
              dataset.value_max() - dataset.value_min() },
            { "z (elevation)",
              dataset.bbox_min_z(),
              dataset.bbox_max_z() - dataset.bbox_min_z() }
        }};

        std::size_t last_lod_level =
             static_cast<std::size_t>(-1);

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

        const auto collect_cached_tile_points =
            [&tile_point_cache, &tile_cache_mutex](
                const std::vector<std::uint64_t>& ids
            ) {
                std::vector<std::pair<
                    std::uint64_t,
                    std::shared_ptr<const std::vector<gs3d::data::Gs3dPoint>>
                >> tiles;
                tiles.reserve(ids.size());

                std::scoped_lock lock(tile_cache_mutex);
                for (const auto tid : ids) {
                    const auto it = tile_point_cache.find(tid);
                    if (it == tile_point_cache.end() || !it->second) {
                        throw std::runtime_error(
                            "ViewerApp: tile cache missing expected tile"
                        );
                    }
                    tiles.emplace_back(tid, it->second);
                }

                return tiles;
            };

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

            // Tab: cycle through color attributes (zero GPU cost — push constant only)
            const bool tab_pressed = window.key_pressed(GLFW_KEY_TAB);
            if (tab_pressed && !tab_was_pressed) {
                push.attr_index =
                    (push.attr_index + 1u) %
                    static_cast<std::uint32_t>(attr_table.size());

                const auto& a = attr_table[push.attr_index];
                push.value_min   = a.min_val;
                push.value_range = a.range;
                if (push.value_range <= 0.0f) push.value_range = 1.0f;

                std::cout << "[ATTR] switched to: " << a.name << '\n';
            }
            tab_was_pressed = tab_pressed;

            controller.update(
                camera,
                window
            );

            fill_push_constants(push, camera);

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
                    debounced_tile_ids.clear();
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

                    if (tile_result.changed) {
                        debounced_tile_ids = tile_result.tile_ids;
                        tile_selection_changed_at = current_time;
                    }

                    // ── Step 1: apply completed load ─────────────────────
                    if (tile_load_future.valid() &&
                        tile_load_future.wait_for(std::chrono::seconds(0))
                            == std::future_status::ready) {

                        auto loaded = tile_load_future.get();
                        tile_load_future = {};

                        if (loaded.tile_ids == tile_result.tile_ids) {
                            // Selection unchanged since load was dispatched
                            const auto cached_tiles =
                                collect_cached_tile_points(loaded.tile_ids);
                            gs3d::util::Stopwatch upload_timer;
                            renderer.wait_for_in_flight_fences();
                            const auto sync =
                                tile_gpu_cloud->sync_from_cached_tiles(
                                    context,
                                    renderer.command_pool(),
                                    context.graphics_queue(),
                                    cached_tiles
                                );
                            loaded_tile_query_box = loaded.actual_bbox;
                            log_tile_upload(
                                tile_gpu_cloud->stats(),
                                loaded,
                                sync,
                                upload_timer.elapsed_seconds(),
                                tile_async_cycle_timer.elapsed_seconds()
                            );
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

                        const double selection_stable_seconds =
                            std::chrono::duration<double>(
                                current_time - tile_selection_changed_at
                            ).count();

                        const bool selection_stable =
                            selection_stable_seconds >=
                            kTileSelectionDebounceSeconds;

                        if (buffer_stale &&
                            !load_in_progress &&
                            selection_stable &&
                            !debounced_tile_ids.empty() &&
                            tile_loading_ids != debounced_tile_ids) {

                            // Compute actual data bbox on main thread
                            // (record metadata, no I/O)
                            gs3d::data::Gs3dTileQueryBox actual_bbox;
                            actual_bbox.min_x = actual_bbox.min_y =
                                actual_bbox.min_z =
                                    std::numeric_limits<float>::max();
                            actual_bbox.max_x = actual_bbox.max_y =
                                actual_bbox.max_z =
                                    -std::numeric_limits<float>::max();
                            for (const auto tid : debounced_tile_ids) {
                                const auto& rec = tile_reader->record(tid);
                                actual_bbox.min_x = std::min(actual_bbox.min_x, rec.bbox_min_x);
                                actual_bbox.min_y = std::min(actual_bbox.min_y, rec.bbox_min_y);
                                actual_bbox.min_z = std::min(actual_bbox.min_z, rec.bbox_min_z);
                                actual_bbox.max_x = std::max(actual_bbox.max_x, rec.bbox_max_x);
                                actual_bbox.max_y = std::max(actual_bbox.max_y, rec.bbox_max_y);
                                actual_bbox.max_z = std::max(actual_bbox.max_z, rec.bbox_max_z);
                            }

                            tile_loading_ids = debounced_tile_ids;
                            const auto ids = debounced_tile_ids;
                            const auto candidate_tile_count =
                                static_cast<std::size_t>(
                                    tile_result.total_candidate_tiles
                                );
                            const auto& tr  = *tile_reader;
                            tile_async_cycle_timer.reset();

                            std::vector<std::uint64_t> missing_ids;
                            std::size_t cache_hit_tiles = 0;
                            {
                                std::scoped_lock lock(tile_cache_mutex);
                                for (const auto tid : ids) {
                                    if (tile_point_cache.contains(tid)) {
                                        ++cache_hit_tiles;
                                    } else {
                                        missing_ids.push_back(tid);
                                    }
                                }
                            }

                            if (missing_ids.empty()) {
                                tile_async_cycle_timer.reset();

                                TileLoadResult cached;
                                cached.tile_ids = ids;
                                cached.actual_bbox = actual_bbox;
                                cached.cache_hit_tiles = ids.size();
                                cached.cache_miss_tiles = 0;
                                cached.candidate_tiles =
                                    candidate_tile_count;

                                const auto cached_tiles =
                                    collect_cached_tile_points(ids);

                                gs3d::util::Stopwatch upload_timer;
                                renderer.wait_for_in_flight_fences();
                                const auto sync =
                                    tile_gpu_cloud->sync_from_cached_tiles(
                                        context,
                                        renderer.command_pool(),
                                        context.graphics_queue(),
                                        cached_tiles
                                    );
                                loaded_tile_query_box = cached.actual_bbox;
                                tile_loading_ids.clear();

                                log_tile_upload(
                                    tile_gpu_cloud->stats(),
                                    cached,
                                    sync,
                                    upload_timer.elapsed_seconds(),
                                    tile_async_cycle_timer.elapsed_seconds()
                                );
                                continue;
                            }

                            tile_load_future = std::async(
                                std::launch::async,
                                [&tr,
                                 &tile_point_cache,
                                 &tile_cache_mutex,
                                 ids,
                                 missing_ids,
                                  actual_bbox,
                                 cache_hit_tiles,
                                 candidate_tile_count]() -> TileLoadResult {
                                    gs3d::util::Stopwatch read_timer;

                                    for (const auto tid : missing_ids) {
                                        auto points =
                                            std::make_shared<
                                                std::vector<gs3d::data::Gs3dPoint>
                                            >(tr.read_tile_points(tid));
                                        std::scoped_lock lock(tile_cache_mutex);
                                        tile_point_cache[tid] = std::move(points);
                                    }

                                    TileLoadResult r;
                                    r.tile_ids = ids;
                                    r.actual_bbox = actual_bbox;
                                    r.cache_hit_tiles = cache_hit_tiles;
                                    r.cache_miss_tiles = missing_ids.size();
                                    r.candidate_tiles =
                                        candidate_tile_count;
                                    r.read_seconds = read_timer.elapsed_seconds();
                                    return r;
                                }
                            );

                            if (config_.tile_verbose) {
                                std::cout << "[TILE] async load dispatched, "
                                          << ids.size() << " tiles"
                                          << " (candidates="
                                          << candidate_tile_count
                                          << ")"
                                          << " (cache_hit="
                                          << cache_hit_tiles
                                          << ", cache_miss="
                                          << missing_ids.size()
                                          << ").\n";
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
                    /*
                     * Tile cloud 始终渲染（已在 GPU 内存中，无额外 I/O 开销）。
                     * 交互期间 tile 选区冻结，但已加载的全精度数据持续可见，
                     * 不会退化为 LOD —— 对比 Potree/Cesium 的流式加载场景，
                     * 本地渲染无需牺牲视觉质量换取带宽节省。
                     * LOD 仅补全 tile 未覆盖的区域。
                     */
                    const bool tile_will_render =
                        tile_gpu_cloud &&
                        tile_gpu_cloud->valid();
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
                    if (tile_will_render) {
                        for (const auto tile_id : tile_gpu_cloud->loaded_tile_ids()) {
                            point_pipeline.draw(
                                command_buffer,
                                tile_gpu_cloud->gpu_cloud_for_tile(tile_id),
                                renderer.extent(),
                                push
                            );
                        }
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
