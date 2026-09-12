# ---------------------------------------------------------------------------
# Platform-neutral Vulkan render core: no GLFW, Flutter or native window code.
# Surface/swapchain frontends are optional adapters and must not be required by
# the headless engine target.
# ---------------------------------------------------------------------------

add_library(gs3d_render STATIC
    AxisGrid.cpp
    GpuFrameTimer.cpp
    LodSelector.cpp
    NearestPointQuery.cpp
    OffscreenFramebuffer.cpp
    PresentSurface.cpp
    PointCloudGpu.cpp
    PointCloudLodGpu.cpp
    PointCloudTileGpu.cpp
    PointPipeline.cpp
    TileDeviceArena.cpp
    TileSelection.cpp
    ViewportManager.cpp
    VulkanBuffer.cpp
    VulkanContext.cpp
    VulkanDepthBuffer.cpp
    VulkanGpuInfo.cpp
)

target_include_directories(gs3d_render PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(gs3d_render PUBLIC
    gs3d_data
    gs3d_platform
    Vulkan::Vulkan
)
