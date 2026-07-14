#include "app/ViewerLodFrameSystem.hpp"
#include "camera/Camera.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE(
    "ViewerLodFrameSystem derives orthographic world units per pixel",
    "[lod_frame]"
)
{
    gs3d::camera::Camera camera;
    camera.set_viewport(800, 200);
    camera.set_orthographic(400.0f, 0.01f, 1000.0f);

    CHECK(
        gs3d::app::ViewerLodFrameSystem::world_per_pixel(camera) ==
        Catch::Approx(2.0f)
    );

    camera.set_viewport(800, 100);
    CHECK(
        gs3d::app::ViewerLodFrameSystem::world_per_pixel(camera) ==
        Catch::Approx(4.0f)
    );
}
