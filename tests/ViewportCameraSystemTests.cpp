#include "app/ViewportCameraSystem.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("ViewportCameraSystem owns and updates each viewport", "[viewport_camera]")
{
    const gs3d::camera::CameraBounds bounds{
        .min = {-10.0f, -10.0f, -10.0f},
        .max = {10.0f, 10.0f, 10.0f}
    };
    gs3d::app::ViewportCameraSystem cameras({}, bounds, 2);
    gs3d::camera::Camera camera;
    gs3d::app::ViewportFrameCmd frame;
    frame.index = 1;
    frame.width = 800;
    frame.height = 600;
    frame.mouse_wheel = 1.0f;
    frame.mouse_on_image = true;
    frame.mouse_local_x = 400.0f;
    frame.mouse_local_y = 300.0f;

    CHECK(cameras.viewport_count() == 2);
    CHECK(cameras.contains(1));
    CHECK_FALSE(cameras.contains(2));
    const float before_height = camera.ortho_height();
    const auto update = cameras.update(frame, camera);
    CHECK(update.interacting);
    CHECK(update.camera_changed);
    CHECK(camera.ortho_height() < before_height);
}
