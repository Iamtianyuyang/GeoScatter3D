# ---------------------------------------------------------------------------
# Render Subsystem: Vulkan Pipelines, Buffers, Shaders, and LOD Controllers
# ---------------------------------------------------------------------------

add_library(gs3d_render STATIC
    AxisGrid.cpp
    GpuFrameTimer.cpp
    LodSelector.cpp
    NearestPointQuery.cpp
    OffscreenFramebuffer.cpp
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
    VulkanRenderer.cpp
    VulkanSwapchain.cpp
)

target_include_directories(gs3d_render PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(gs3d_render PUBLIC
    gs3d_data
    gs3d_platform
    Vulkan::Vulkan
)

# Viewport LOD Controller
add_library(gs3d_viewport_lod STATIC
    ${PROJECT_SOURCE_DIR}/src/app/systems/ViewportLodController.cpp
)

target_include_directories(gs3d_viewport_lod PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(gs3d_viewport_lod PUBLIC
    gs3d_render
)
