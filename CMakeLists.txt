cmake_minimum_required(VERSION 3.28.3)

project(GeoScatter3D LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)
find_package(Threads REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)

# Shared by GeoScatter3D (interactive) and GeoScatter3DBenchmark (manual
# performance harness, see tests/BenchmarkSuite.cpp) — both drive the same
# ViewerApp render loop, just with a different entry point.
set(GEOSCATTER3D_CORE_SOURCES
    src/gui/ImGuiLayer.cpp
    src/ui/UiRoot.cpp

    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp

    src/app/ViewerApp.cpp
    src/app/AppConfig.cpp
    src/app/ResourcePath.cpp
    src/app/TilePointCache.cpp
    src/app/ViewportResizeScheduler.cpp

    src/platform/Window.cpp

    src/data/CsvStreamReader.cpp
    src/data/CsvSniffer.cpp
    src/data/CsvChunkUtils.cpp
    src/data/CsvChunkPlanner.cpp
    src/data/CsvChunkReader.cpp
    src/data/DataSchema.cpp
    src/data/Delimiter.cpp
    src/data/Gs3dFormat.cpp
    src/data/Gs3dReader.cpp
    src/data/Gs3dVerifier.cpp
    src/data/Gs3dDataset.cpp
    src/data/Gs3dLodDataset.cpp
    src/data/Gs3dLodFormat.cpp
    src/data/Gs3dLodReader.cpp
    src/data/Gs3dTileFormat.cpp
    src/data/Gs3dTileReader.cpp

    src/camera/BoxSelect.cpp
    src/camera/Camera.cpp
    src/camera/CameraController.cpp
    src/camera/CameraHub.cpp
    src/camera/MouseRay.cpp

    src/preprocess/StatisticsPass.cpp
    src/preprocess/Gs3dWriter.cpp
    src/preprocess/CsvToGs3dConverter.cpp
    src/preprocess/Gs3dLodWriter.cpp
    src/preprocess/Gs3dTileWriter.cpp

    src/render/VulkanContext.cpp
    src/render/VulkanSwapchain.cpp
    src/render/VulkanRenderer.cpp
    src/render/VulkanBuffer.cpp
    src/render/PointCloudGpu.cpp
    src/render/PointPipeline.cpp
    src/render/VulkanDepthBuffer.cpp
    src/render/PointCloudLodGpu.cpp
    src/render/LodSelector.cpp
    src/render/PointCloudTileGpu.cpp
    src/render/TileSelection.cpp
    src/render/OffscreenFramebuffer.cpp
    src/render/ViewportManager.cpp
    src/render/NearestPointQuery.cpp
    src/render/AxisGrid.cpp

    include/gui/ImGuiLayer.hpp
    include/ui/UiRoot.hpp

    include/app/ViewerApp.hpp
    include/app/AppConfig.hpp
    include/app/ResourcePath.hpp
    include/app/AppState.hpp
    include/app/UiActions.hpp
    include/app/TilePointCache.hpp
    include/app/ViewportResizeScheduler.hpp

    include/platform/Window.hpp

    include/data/Gs3dVerifier.hpp
    include/data/CsvStreamReader.hpp
    include/data/CsvSniffer.hpp
    include/data/CsvChunkUtils.hpp
    include/data/CsvChunkPlanner.hpp
    include/data/CsvChunkReader.hpp
    include/data/DataSchema.hpp
    include/data/Delimiter.hpp
    include/data/Gs3dFormat.hpp
    include/data/Gs3dReader.hpp
    include/data/Gs3dDataset.hpp
    include/data/Gs3dLodDataset.hpp
    include/data/Gs3dLodFormat.hpp
    include/data/Gs3dLodReader.hpp
    include/data/Gs3dTileFormat.hpp
    include/data/Gs3dTileReader.hpp
    include/data/PointDataAdapters.hpp
    include/data/TileDataAdapters.hpp

    include/camera/BoxSelect.hpp
    include/camera/Camera.hpp
    include/camera/CameraController.hpp
    include/camera/CameraHub.hpp
    include/camera/MouseRay.hpp

    include/preprocess/StatisticsPass.hpp
    include/preprocess/Gs3dWriter.hpp
    include/preprocess/CsvToGs3dConverter.hpp
    include/preprocess/Gs3dLodWriter.hpp
    include/preprocess/Gs3dTileWriter.hpp

    include/render/VulkanContext.hpp
    include/render/VulkanSwapchain.hpp
    include/render/VulkanRenderer.hpp
    include/render/VulkanBuffer.hpp
    include/render/PointCloudGpu.hpp  
    include/render/PointPipeline.hpp
    include/render/VulkanDepthBuffer.hpp
    include/render/PointCloudLodGpu.hpp
    include/render/LodSelector.hpp
    include/render/PointCloudTileGpu.hpp
    include/render/FrameUploadBudget.hpp
    include/render/TileSelection.hpp
    include/render/OffscreenFramebuffer.hpp
    include/render/ViewportManager.hpp
    include/render/NearestPointQuery.hpp
    include/render/AxisGrid.hpp

    include/core/PointData.hpp
    include/core/TileData.hpp
    include/scene/SceneState.hpp

    include/util/ThreadPool.hpp
    include/util/Stopwatch.hpp
    include/util/PercentileStats.hpp
)

add_executable(GeoScatter3D
    src/main.cpp
    ${GEOSCATTER3D_CORE_SOURCES}
)

target_include_directories(GeoScatter3D PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tomlplusplus/include
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
)

target_link_libraries(GeoScatter3D PRIVATE
    Vulkan::Vulkan
    glfw
    Threads::Threads
)

include(CTest)

if(BUILD_TESTING)
    add_executable(GeoScatter3DDataTests
        tests/Gs3dFormatTests.cpp
        src/data/Gs3dFormat.cpp
        src/data/Gs3dReader.cpp
        src/data/Gs3dDataset.cpp
    )

    target_include_directories(GeoScatter3DDataTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    add_test(
        NAME GeoScatter3D.DataFormat
        COMMAND GeoScatter3DDataTests
    )

    add_executable(GeoScatter3DRuntimeTests
        tests/RuntimePerformanceTests.cpp
        src/app/TilePointCache.cpp
        src/app/ViewportResizeScheduler.cpp
        src/camera/BoxSelect.cpp
        src/camera/Camera.cpp
        src/camera/CameraController.cpp
        src/camera/MouseRay.cpp
        src/render/LodSelector.cpp
        src/render/NearestPointQuery.cpp
        src/render/AxisGrid.cpp
    )

    target_include_directories(GeoScatter3DRuntimeTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(GeoScatter3DRuntimeTests PRIVATE
        Threads::Threads
    )

    add_test(
        NAME GeoScatter3D.RuntimePerformance
        COMMAND GeoScatter3DRuntimeTests
    )

    add_test(
        NAME GeoScatter3D.IncludeDeps
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_include_deps.py
    )

    # Manual GPU performance harness — not registered with add_test()/ctest
    # (needs a real Vulkan device and takes seconds, not milliseconds; see
    # docs/plan/geoscatter3d-tasks.md Task 1). Run directly:
    #   ./build/GeoScatter3DBenchmark --config config/viewer.toml [frame_count]
    add_executable(GeoScatter3DBenchmark
        tests/BenchmarkSuite.cpp
        ${GEOSCATTER3D_CORE_SOURCES}
    )

    target_include_directories(GeoScatter3DBenchmark PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tomlplusplus/include
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
    )

    target_link_libraries(GeoScatter3DBenchmark PRIVATE
        Vulkan::Vulkan
        glfw
        Threads::Threads
    )
endif()
