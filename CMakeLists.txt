cmake_minimum_required(VERSION 3.28.3)

project(GeoScatter3D LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # Sources contain UTF-8 (Chinese) string literals; without /utf-8 MSVC
    # interprets them in the system code page and mangles or rejects them.
    add_compile_options(/utf-8)
endif()

find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)
find_package(Threads REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)

# Shader sources are the canonical inputs. Generated SPIR-V lives only in the
# build tree, so a source edit is always visible to the build graph.
set(
    GS3D_GLSLANG_VALIDATOR
    ""
    CACHE FILEPATH
    "Path to glslangValidator used to compile GeoScatter3D shaders"
)

if(NOT GS3D_GLSLANG_VALIDATOR)
    find_program(
        GS3D_GLSLANG_VALIDATOR_DISCOVERED
        NAMES glslangValidator
        HINTS ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}
    )
    set(
        GS3D_GLSLANG_VALIDATOR
        ${GS3D_GLSLANG_VALIDATOR_DISCOVERED}
        CACHE FILEPATH
        "Path to glslangValidator used to compile GeoScatter3D shaders"
        FORCE
    )
endif()

if(NOT GS3D_GLSLANG_VALIDATOR)
    message(FATAL_ERROR
        "glslangValidator is required to compile GLSL shaders. Install the "
        "Vulkan SDK or glslang tools, or set GS3D_GLSLANG_VALIDATOR."
    )
endif()

set(GS3D_GENERATED_ASSETS_DIR
    ${CMAKE_CURRENT_BINARY_DIR}/generated-assets
)
set(GS3D_GENERATED_SHADER_DIR
    ${GS3D_GENERATED_ASSETS_DIR}/shaders
)
set(GS3D_SHADER_INCLUDE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders
)
set(GS3D_SHADER_COMMON_HEADERS
    ${GS3D_SHADER_INCLUDE_DIR}/point_push_constants.glsl
)

set(GS3D_SHADER_OUTPUTS)
foreach(shader_source
        ${GS3D_SHADER_INCLUDE_DIR}/point.vert
        ${GS3D_SHADER_INCLUDE_DIR}/point.frag)
    get_filename_component(shader_name ${shader_source} NAME)
    set(shader_output
        ${GS3D_GENERATED_SHADER_DIR}/${shader_name}.spv
    )
    add_custom_command(
        OUTPUT ${shader_output}
        COMMAND ${CMAKE_COMMAND} -E make_directory
                ${GS3D_GENERATED_SHADER_DIR}
        COMMAND ${GS3D_GLSLANG_VALIDATOR}
                -V
                -I${GS3D_SHADER_INCLUDE_DIR}
                ${shader_source}
                -o ${shader_output}
        DEPENDS ${shader_source} ${GS3D_SHADER_COMMON_HEADERS}
        COMMENT "Compiling ${shader_name} to SPIR-V"
        VERBATIM
    )
    list(APPEND GS3D_SHADER_OUTPUTS ${shader_output})
endforeach()

add_custom_target(gs3d_shaders ALL DEPENDS ${GS3D_SHADER_OUTPUTS})

add_library(gs3d_data STATIC
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
)

target_include_directories(gs3d_data PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(gs3d_data PUBLIC
    Threads::Threads
)

add_library(gs3d_preprocess STATIC
    src/preprocess/StatisticsPass.cpp
    src/preprocess/Gs3dWriter.cpp
    src/preprocess/CsvToGs3dConverter.cpp
    src/preprocess/Gs3dLodWriter.cpp
    src/preprocess/Gs3dTileWriter.cpp
)

target_include_directories(gs3d_preprocess PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(gs3d_preprocess PUBLIC
    gs3d_data
)

add_library(gs3d_imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)

target_include_directories(gs3d_imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(gs3d_imgui PUBLIC
    Vulkan::Vulkan
    glfw
)

add_library(gs3d_render STATIC
    src/camera/BoxSelect.cpp
    src/camera/Camera.cpp
    src/camera/CameraController.cpp
    src/camera/CameraHub.cpp
    src/camera/MouseRay.cpp

    src/render/VulkanContext.cpp
    src/render/VulkanSwapchain.cpp
    src/render/VulkanRenderer.cpp
    src/render/VulkanBuffer.cpp
    src/render/VulkanGpuInfo.cpp
    src/render/GpuFrameTimer.cpp
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
)

target_include_directories(gs3d_render PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(gs3d_render PUBLIC
    gs3d_data
    gs3d_imgui
    Vulkan::Vulkan
    Threads::Threads
)

add_library(gs3d_app_support STATIC
    src/app/AppConfig.cpp
    src/app/BenchmarkSession.cpp
    src/app/PreprocessedBundle.cpp
    src/app/RecentProjects.cpp
    src/app/ResourcePath.cpp
    src/app/UserPreferences.cpp
    src/app/TileLoadCommit.cpp
    src/app/TilePointCache.cpp
    src/app/ViewportResizeScheduler.cpp
    src/app/ViewportInteractionState.cpp
    src/app/ViewportPresentationState.cpp
    src/app/MeasurementManager.cpp
    src/ui/WorkspaceManager.cpp
)

target_include_directories(gs3d_app_support PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tomlplusplus/include
)

target_link_libraries(gs3d_app_support PUBLIC
    gs3d_preprocess
)

add_library(gs3d_viewport_camera STATIC
    src/app/ViewportCameraSystem.cpp
    include/app/ViewportCameraSystem.hpp
)

target_include_directories(gs3d_viewport_camera PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(gs3d_viewport_camera PUBLIC
    gs3d_app_support
    gs3d_render
)

# Viewer-specific orchestration and UI. The executable and benchmark both
# link this target, so these sources compile once per build.
add_library(gs3d_app STATIC
    src/gui/ImGuiLayer.cpp
    src/ui/Theme.cpp
    src/ui/Widgets.cpp
    src/ui/UiRoot.cpp
    src/ui/RegionStatsPanel.cpp
    src/ui/DatasetPanel.cpp
    src/ui/NavigationMapPanel.cpp
    src/ui/MeasurementPanel.cpp
    src/ui/AuxiliaryPanels.cpp
    src/ui/RenderSettingsPanel.cpp
    src/ui/SvgLogoTexture.cpp

    src/app/ViewerApp.cpp
    src/app/ViewerAppViewport.cpp
    src/app/ViewerAppPicking.cpp
    src/app/ViewerAppRenderView.cpp
    src/app/ViewerAppGuiCommands.cpp
    src/app/ViewerAppCameraCommands.cpp
    src/app/ViewerAppScreenshot.cpp
    src/app/ViewerAppRegionStats.cpp
    src/app/ViewerAppBenchmark.cpp
    src/app/ViewerAppNavigationMap.cpp
    src/app/ViewerAppPickDebugDump.cpp
    src/app/ViewerAppPointIds.cpp
    src/app/ViewerAppTileStreaming.cpp
    src/app/ViewerAppRenderPass.cpp

    src/platform/Window.cpp
    src/platform/NativeFileDialog.cpp

    include/gui/ImGuiLayer.hpp
    include/ui/Theme.hpp
    include/ui/Widgets.hpp
    include/ui/UiRoot.hpp
    include/ui/RegionStatsPanel.hpp
    include/ui/DatasetPanel.hpp
    include/ui/NavigationMapPanel.hpp
    include/ui/MeasurementPanel.hpp
    include/ui/AuxiliaryPanels.hpp
    include/ui/RenderSettingsPanel.hpp
    include/ui/SvgLogoTexture.hpp

    include/app/ViewerApp.hpp
    include/app/ViewerAppInternal.hpp
    include/app/AppConfig.hpp
    include/app/BenchmarkSession.hpp
    include/app/PreprocessedBundle.hpp
    include/app/RecentProjects.hpp
    include/app/ResourcePath.hpp
    include/app/UserPreferences.hpp
    include/app/AppState.hpp
    include/app/UiActions.hpp
    include/app/TilePointCache.hpp
    include/app/ViewportResizeScheduler.hpp
    include/app/ViewportInteractionState.hpp
    include/app/ViewportCameraSystem.hpp
    include/app/MeasurementManager.hpp
    include/ui/WorkspaceManager.hpp

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
    include/render/GpuFrameTimer.hpp
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

    include/core/DatasetDescriptor.hpp
    include/core/PointData.hpp
    include/core/TileData.hpp
    include/scene/SceneState.hpp

    include/util/ThreadPool.hpp
    include/util/Log.hpp
    include/util/Stopwatch.hpp
    include/util/PercentileStats.hpp
)

target_include_directories(gs3d_app PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nanosvg
)

target_link_libraries(gs3d_app PUBLIC
    gs3d_app_support
    gs3d_viewport_camera
    gs3d_render
    gs3d_imgui
    glfw
)

add_executable(GeoScatter3D
    src/main.cpp
    src/app/WelcomeWindow.cpp
    src/ui/WelcomePage.cpp
)

function(copy_runtime_assets target_name)
    add_dependencies(${target_name} gs3d_shaders)
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/assets
                $<TARGET_FILE_DIR:${target_name}>/assets
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${GS3D_GENERATED_SHADER_DIR}
                $<TARGET_FILE_DIR:${target_name}>/assets/shaders
    )
endfunction()

target_include_directories(GeoScatter3D PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tomlplusplus/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nanosvg
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
)

target_link_libraries(GeoScatter3D PRIVATE
    gs3d_app
)

if(WIN32)
    target_link_libraries(gs3d_app PRIVATE comdlg32 ole32 shell32)
endif()

copy_runtime_assets(GeoScatter3D)

# Standalone preprocessor CLI — CSV → GS3D conversion + tile building.
# Links only the data/preprocess sources; no Vulkan / GLFW / ImGui.
set(GEOSCATTER3D_PREPROCESS_SOURCES
    src/preprocess_main.cpp
    src/app/AppConfig.cpp
    src/app/PreprocessedBundle.cpp
    src/app/ResourcePath.cpp
)

add_executable(GeoScatter3DPreprocess
    ${GEOSCATTER3D_PREPROCESS_SOURCES}
)

target_include_directories(GeoScatter3DPreprocess PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tomlplusplus/include
)

target_link_libraries(GeoScatter3DPreprocess PRIVATE
    gs3d_app_support
)

# Standalone ground-truth CLI — reads raw .gs3d, no LOD/tiles/Vulkan/GLFW.
# Used to verify the viewer's pick accuracy, region coverage, and dataset stats.
add_executable(gs3d_groundtruth
    tools/gs3d_groundtruth.cpp
)

target_include_directories(gs3d_groundtruth PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(gs3d_groundtruth PRIVATE
    gs3d_data
)

include(CTest)

if(BUILD_TESTING)
    set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(CATCH_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
    set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/catch2 EXCLUDE_FROM_ALL)

    add_executable(GeoScatter3DThreadPoolTests
        tests/ThreadPoolTests.cpp
    )

    target_include_directories(GeoScatter3DThreadPoolTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(GeoScatter3DThreadPoolTests PRIVATE
        Catch2::Catch2WithMain
    )

    add_test(
        NAME GeoScatter3D.ThreadPool
        COMMAND GeoScatter3DThreadPoolTests
    )

    add_executable(GeoScatter3DGs3dV2Tests
        tests/Gs3dV2Tests.cpp
    )

    target_include_directories(GeoScatter3DGs3dV2Tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(GeoScatter3DGs3dV2Tests PRIVATE
        gs3d_data
        Catch2::Catch2WithMain
    )

    add_test(
        NAME GeoScatter3D.Gs3dV2
        COMMAND GeoScatter3DGs3dV2Tests
    )

    add_executable(GeoScatter3DAppConfigTests
        tests/AppConfigTests.cpp
    )

    target_include_directories(GeoScatter3DAppConfigTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tomlplusplus/include
    )

    target_link_libraries(GeoScatter3DAppConfigTests PRIVATE
        gs3d_app_support
        Catch2::Catch2WithMain
    )

    add_test(
        NAME GeoScatter3D.AppConfig
        COMMAND GeoScatter3DAppConfigTests
    )

    add_executable(GeoScatter3DViewportInteractionTests
        tests/ViewportInteractionTests.cpp
    )

    target_include_directories(GeoScatter3DViewportInteractionTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(GeoScatter3DViewportInteractionTests PRIVATE
        gs3d_app_support
        Catch2::Catch2WithMain
    )

    add_test(
        NAME GeoScatter3D.ViewportInteraction
        COMMAND GeoScatter3DViewportInteractionTests
    )

    add_executable(GeoScatter3DLogTests
        tests/LogTests.cpp
    )

    target_include_directories(GeoScatter3DLogTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(GeoScatter3DLogTests PRIVATE
        Catch2::Catch2WithMain
    )

    add_test(
        NAME GeoScatter3D.Log
        COMMAND GeoScatter3DLogTests
    )

    add_executable(GeoScatter3DViewportCameraSystemTests
        tests/ViewportCameraSystemTests.cpp
    )

    target_include_directories(GeoScatter3DViewportCameraSystemTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(GeoScatter3DViewportCameraSystemTests PRIVATE
        gs3d_viewport_camera
        Catch2::Catch2WithMain
    )

    add_test(
        NAME GeoScatter3D.ViewportCameraSystem
        COMMAND GeoScatter3DViewportCameraSystemTests
    )

    add_executable(GeoScatter3DWorkspaceManagerTests
        tests/WorkspaceManagerTests.cpp
    )

    target_include_directories(GeoScatter3DWorkspaceManagerTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(GeoScatter3DWorkspaceManagerTests PRIVATE
        gs3d_app_support
        Catch2::Catch2WithMain
    )

    add_test(
        NAME GeoScatter3D.WorkspaceManager
        COMMAND GeoScatter3DWorkspaceManagerTests
    )

    add_executable(GeoScatter3DBenchmarkSessionTests
        tests/BenchmarkSessionTests.cpp
    )

    target_include_directories(GeoScatter3DBenchmarkSessionTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(GeoScatter3DBenchmarkSessionTests PRIVATE
        gs3d_app_support
        Catch2::Catch2WithMain
    )

    add_test(
        NAME GeoScatter3D.BenchmarkSession
        COMMAND GeoScatter3DBenchmarkSessionTests
    )

    add_executable(GeoScatter3DViewportPresentationStateTests
        tests/ViewportPresentationStateTests.cpp
    )

    target_include_directories(GeoScatter3DViewportPresentationStateTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(GeoScatter3DViewportPresentationStateTests PRIVATE
        gs3d_app_support
        Catch2::Catch2WithMain
    )

    add_test(
        NAME GeoScatter3D.ViewportPresentationState
        COMMAND GeoScatter3DViewportPresentationStateTests
    )

    add_executable(GeoScatter3DDataTests
        tests/Gs3dFormatTests.cpp
    )

    target_include_directories(GeoScatter3DDataTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(GeoScatter3DDataTests PRIVATE
        gs3d_preprocess
        Catch2::Catch2WithMain
    )

    add_test(
        NAME GeoScatter3D.DataFormat
        COMMAND GeoScatter3DDataTests
    )

    add_executable(GeoScatter3DRuntimeLogicTests
        tests/RuntimeLogicTests.cpp
    )

    target_include_directories(GeoScatter3DRuntimeLogicTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tomlplusplus/include
    )

    target_link_libraries(GeoScatter3DRuntimeLogicTests PRIVATE
        gs3d_app_support
        gs3d_render
        Catch2::Catch2WithMain
    )

    add_test(
        NAME GeoScatter3D.RuntimeLogic
        COMMAND GeoScatter3DRuntimeLogicTests
    )

    add_executable(GeoScatter3DTileStreamingTests
        tests/TileStreamingTests.cpp
    )

    target_include_directories(GeoScatter3DTileStreamingTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(GeoScatter3DTileStreamingTests PRIVATE
        gs3d_app_support
        gs3d_render
        Catch2::Catch2WithMain
    )

    add_test(
        NAME GeoScatter3D.TileStreaming
        COMMAND GeoScatter3DTileStreamingTests
    )

    add_executable(GeoScatter3DGpuInfoTests
        tests/VulkanGpuInfoTests.cpp
    )

    target_include_directories(GeoScatter3DGpuInfoTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(GeoScatter3DGpuInfoTests PRIVATE
        gs3d_render
        Catch2::Catch2WithMain
    )

    add_test(
        NAME GeoScatter3D.GpuInfo
        COMMAND GeoScatter3DGpuInfoTests
    )

    add_test(
        NAME GeoScatter3D.IncludeDeps
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_include_deps.py
    )

    # Fresh-clone smoke test: a tracked CSV is converted into an ignored
    # bundle, exercising config resolution and the standalone preprocessor
    # without requiring a window server or physical GPU.
    add_test(
        NAME GeoScatter3D.FreshClonePreprocess
        COMMAND GeoScatter3DPreprocess
                --config
                ${CMAKE_CURRENT_SOURCE_DIR}/config/sample-viewer.toml
    )
    set_tests_properties(
        GeoScatter3D.FreshClonePreprocess
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            ENVIRONMENT
                "GS3D_USER_CONFIG_DIR=${CMAKE_CURRENT_BINARY_DIR}/test-user-config"
    )

    # Manual GPU performance harness — not registered with add_test()/ctest
    # (needs a real Vulkan device and takes seconds, not milliseconds; see
    # docs/plan/geoscatter3d-tasks.md Task 1). Run directly:
    #   ./build/GeoScatter3DBenchmark --config config/viewer.toml [frame_count]
    add_executable(GeoScatter3DBenchmark
        tests/BenchmarkSuite.cpp
    )

    target_include_directories(GeoScatter3DBenchmark PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tomlplusplus/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nanosvg
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
    )

    target_link_libraries(GeoScatter3DBenchmark PRIVATE
        gs3d_app
    )

    copy_runtime_assets(GeoScatter3DBenchmark)
endif()
