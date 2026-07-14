#include "app/ViewerApp.hpp"
#include "app/ViewerAppInternal.hpp"

#include "camera/Camera.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {

constexpr gs3d::camera::CameraBounds kBounds{
    .min = {-10.0f, -20.0f, 0.0f},
    .max = {10.0f, 20.0f, 30.0f},
};

} // namespace

TEST_CASE("Viewer camera configuration fits bounds when requested", "[camera_configuration]")
{
    gs3d::app::ViewerCameraConfig config;
    config.mode = "fit";
    config.position = {999.0f, 999.0f, 999.0f};

    gs3d::camera::Camera camera;
    gs3d::app::initialize_camera_from_config(camera, config, kBounds);

    CHECK(camera.projection_mode() == gs3d::camera::ProjectionMode::Orthographic);
    CHECK(camera.target().x == Catch::Approx(0.0f));
    CHECK(camera.target().y == Catch::Approx(0.0f));
    CHECK(camera.target().z == Catch::Approx(15.0f));
}

TEST_CASE("Viewer camera configuration applies an explicit camera", "[camera_configuration]")
{
    gs3d::app::ViewerCameraConfig config;
    config.mode = "manual";
    config.position = {12.0f, -9.0f, 7.0f};
    config.target = {2.0f, 3.0f, 4.0f};
    config.up = {0.0f, 0.0f, 1.0f};
    config.fov_y = 60.0f;

    gs3d::camera::Camera camera;
    gs3d::app::initialize_camera_from_config(camera, config, kBounds);

    CHECK(camera.projection_mode() == gs3d::camera::ProjectionMode::Orthographic);
    CHECK(camera.position().x == Catch::Approx(12.0f));
    CHECK(camera.position().y == Catch::Approx(-9.0f));
    CHECK(camera.position().z == Catch::Approx(7.0f));
    CHECK(camera.target().x == Catch::Approx(2.0f));
    CHECK(camera.target().y == Catch::Approx(3.0f));
    CHECK(camera.target().z == Catch::Approx(4.0f));
    CHECK(camera.ortho_height() > 0.0f);
    CHECK(camera.near_plane() == Catch::Approx(0.01f));
    CHECK(camera.far_plane() == Catch::Approx(1.0e7f));
}
