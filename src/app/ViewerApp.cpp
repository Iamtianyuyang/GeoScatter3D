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
#include <exception>
#include <iostream>
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

    tile_config.enable_distance =
        config.tile_enable_distance;

    tile_config.near_distance =
        config.tile_near_distance;

    tile_config.middle_distance =
        config.tile_middle_distance;

    tile_config.near_half_size =
        config.tile_near_half_size;

    tile_config.middle_half_size =
        config.tile_middle_half_size;

    tile_config.far_half_size =
        config.tile_far_half_size;

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

    push.padding = 0.0f;
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

            if (config_.lod_enabled) {
                lod_selector.update(
                    window_interacting(window),
                    delta_seconds
                );
            }

            if (config_.tile_enabled &&
                tile_reader.has_value() &&
                tile_gpu_cloud) {
                const auto tile_result =
                    tile_selection.update(
                        camera,
                        *tile_reader
                    );

                if (!tile_result.enabled) {
                    if (tile_gpu_cloud->valid()) {
                        /*
                        * clear() 会销毁旧 VkBuffer。
                        * 销毁前必须确保 GPU 不再使用它。
                        */
                        vkDeviceWaitIdle(context.device());

                        tile_gpu_cloud->clear();

                        if (config_.tile_verbose) {
                            std::cout << "[TILE] disabled, local full-res buffer cleared.\n";
                        }
                    }
                } else if (tile_result.changed) {
                    /*
                    * 当前 PointCloudTileGpu::update_from_tiles() 会销毁旧 VkBuffer。
                    * 旧 buffer 可能仍被上一帧 command buffer 使用。
                    * 第一版先用 vkDeviceWaitIdle 保证安全。
                    * 后续再改成延迟销毁 / frames-in-flight 资源回收。
                    */
                    vkDeviceWaitIdle(context.device());

                    tile_gpu_cloud->update_from_tiles(
                        context,
                        renderer.command_pool(),
                        context.graphics_queue(),
                        *tile_reader,
                        tile_result.tile_ids
                    );

                    if (config_.tile_verbose) {
                        const auto& stats =
                            tile_gpu_cloud->stats();

                        std::cout << "[TILE] active full-res tiles updated.\n";
                        std::cout << "tile_count = "
                                << stats.tile_count
                                << '\n';
                        std::cout << "point_count = "
                                << stats.point_count
                                << '\n';
                        std::cout << "gpu_buffer_bytes = "
                                << stats.gpu_buffer_bytes
                                << '\n';
                        std::cout << "camera_distance = "
                                << tile_result.camera_distance
                                << '\n';
                        std::cout << "query_half_size = "
                                << tile_result.query_half_size
                                << '\n';
                    }
                }
            }

            renderer.draw_frame(
                window,
                [&](VkCommandBuffer command_buffer) {
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
                            push
                        );
                    } else {
                        point_pipeline.draw(
                            command_buffer,
                            *full_gpu_cloud,
                            renderer.extent(),
                            push
                        );
                    }
                    if (tile_gpu_cloud && tile_gpu_cloud->valid()) {
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