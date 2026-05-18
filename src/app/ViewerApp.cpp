#include "app/ViewerApp.hpp"

#include "camera/Camera.hpp"
#include "camera/CameraController.hpp"
#include "data/Gs3dDataset.hpp"
#include "platform/Window.hpp"
#include "render/PointCloudGpu.hpp"
#include "render/PointPipeline.hpp"
#include "render/VulkanContext.hpp"
#include "render/VulkanRenderer.hpp"
#include "render/VulkanSwapchain.hpp"

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <exception>
#include <iostream>

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

void print_controls() {
    std::cout << "[OK] Entering render loop.\n";
    std::cout << "Controls:\n";
    std::cout << "  Left drag   : orbit\n";
    std::cout << "  Right drag  : pan\n";
    std::cout << "  Middle drag : pan\n";
    std::cout << "  Wheel       : zoom\n";
    std::cout << "  + / -       : point size\n";
    std::cout << "  R           : reset view\n";
    std::cout << "  Esc         : quit\n";
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

        gs3d::render::PointCloudGpu gpu_cloud(
            context,
            renderer.command_pool(),
            context.graphics_queue(),
            dataset
        );

        std::cout << "[OK] PointCloudGpu uploaded.\n";
        std::cout << "gpu point_count = "
                  << gpu_cloud.point_count() << '\n';

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

        bool r_was_pressed = false;

        print_controls();

        while (!window.should_close()) {
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

            renderer.draw_frame(
                window,
                [&](VkCommandBuffer command_buffer) {
                    point_pipeline.draw(
                        command_buffer,
                        gpu_cloud,
                        renderer.extent(),
                        push
                    );
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