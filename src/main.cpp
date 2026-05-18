// src/main.cpp

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

static constexpr const char* TEST_GS3D_PATH =
    "/home/tianyy/project/GeoScatter3D/data/test.gs3d";

static constexpr const char* VERT_SHADER_PATH =
    "/home/tianyy/project/GeoScatter3D/assets/shaders/point.vert.spv";

static constexpr const char* FRAG_SHADER_PATH =
    "/home/tianyy/project/GeoScatter3D/assets/shaders/point.frag.spv";

static gs3d::camera::CameraBounds make_camera_bounds(
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

static void fill_push_constants(
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

static void reset_camera_fixed(
    gs3d::camera::Camera& camera
) {
    camera.look_at(
        {0.0f, -14000.0f, 6000.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    );

    camera.set_perspective(
        45.0f,
        1.0f,
        100000.0f
    );
}

int main() {
    try {
        const auto dataset =
            gs3d::data::Gs3dDatasetLoader::load(TEST_GS3D_PATH);

        if (!dataset.is_consistent()) {
            std::cerr << "[FAIL] dataset is inconsistent.\n";
            return 1;
        }

        if (dataset.empty()) {
            std::cerr << "[FAIL] dataset is empty.\n";
            return 1;
        }

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

        gs3d::platform::WindowConfig window_config;
        window_config.width = 1280;
        window_config.height = 720;
        window_config.title = "GeoScatter3D Viewer - Pivot Orbit Test";
        window_config.resizable = true;

        gs3d::platform::Window window(window_config);

        gs3d::render::VulkanContextConfig vk_config;
        vk_config.enable_validation_layers = true;
        vk_config.application_name = "GeoScatter3D";

        gs3d::render::VulkanContext context(window, vk_config);

        std::cout << "[OK] VulkanContext created.\n";
        std::cout << "Physical device: "
                  << context.physical_device_name() << '\n';

        gs3d::render::VulkanSwapchain swapchain(context, window);
        gs3d::render::VulkanRenderer renderer(context, swapchain);

        gs3d::render::ClearColor clear_color;
        clear_color.r = 0.015f;
        clear_color.g = 0.018f;
        clear_color.b = 0.025f;
        clear_color.a = 1.0f;
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
        pipeline_config.vertex_shader_path = VERT_SHADER_PATH;
        pipeline_config.fragment_shader_path = FRAG_SHADER_PATH;

        gs3d::render::PointPipeline point_pipeline(
            context,
            renderer.render_pass(),
            pipeline_config
        );

        std::cout << "[OK] PointPipeline created.\n";

        const gs3d::camera::CameraBounds bounds =
            make_camera_bounds(dataset);

        gs3d::camera::Camera camera;
        camera.set_viewport(1280, 720);
        reset_camera_fixed(camera);

        gs3d::camera::CameraControllerConfig controller_config;
        controller_config.rotate_speed = 1.0f;
        controller_config.pan_speed = 1.0f;
        controller_config.zoom_speed = 1.0f;
        controller_config.invert_rotate_x = false;
        controller_config.invert_rotate_y = false;
        controller_config.invert_pan_x = false;
        controller_config.invert_pan_y = false;

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
        push.point_size = 1.0f;

        fill_push_constants(
            push,
            camera,
            dataset
        );

        std::cout << "[OK] Entering render loop.\n";
        std::cout << "Controls:\n";
        std::cout << "  Left drag   : orbit around pivot\n";
        std::cout << "  Right drag  : pan\n";
        std::cout << "  Middle drag : pan\n";
        std::cout << "  Wheel       : zoom\n";
        std::cout << "  + / -       : point size\n";
        std::cout << "  R           : reset fixed view\n";
        std::cout << "  Esc         : quit\n";

        bool r_was_pressed = false;

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
                reset_camera_fixed(camera);
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

        std::cout << "[PASS] CameraController render test completed.\n";

    } catch (const std::exception& e) {
        std::cerr << "[FAIL] " << e.what() << '\n';
        return 1;
    }

    return 0;
}