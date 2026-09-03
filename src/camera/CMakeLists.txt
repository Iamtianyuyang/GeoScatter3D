# ---------------------------------------------------------------------------
# Camera Subsystem: Interactive Viewport Camera, Box Select, and Mouse Rays
# ---------------------------------------------------------------------------

add_library(gs3d_viewport_camera STATIC
    BoxSelect.cpp
    Camera.cpp
    CameraController.cpp
    CameraHub.cpp
    MouseRay.cpp
    ${PROJECT_SOURCE_DIR}/src/app/systems/ViewportCameraSystem.cpp
)

target_include_directories(gs3d_viewport_camera PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(gs3d_viewport_camera PUBLIC
    gs3d_app_support
    gs3d_render
)
