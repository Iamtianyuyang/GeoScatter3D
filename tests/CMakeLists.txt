# ---------------------------------------------------------------------------
# Test Suite & Benchmarks
# ---------------------------------------------------------------------------

set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CATCH_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
add_subdirectory(${PROJECT_SOURCE_DIR}/third_party/catch2 ${CMAKE_CURRENT_BINARY_DIR}/catch2 EXCLUDE_FROM_ALL)

# Unit Tests

add_executable(GeoScatter3DThreadPoolTests
    ThreadPoolTests.cpp
)
target_include_directories(GeoScatter3DThreadPoolTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(GeoScatter3DThreadPoolTests PRIVATE
    Catch2::Catch2WithMain
)
add_test(
    NAME GeoScatter3D.ThreadPool
    COMMAND GeoScatter3DThreadPoolTests
)

add_executable(GeoScatter3DCpuInfoTests
    CpuInfoTests.cpp
)
target_include_directories(GeoScatter3DCpuInfoTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(GeoScatter3DCpuInfoTests PRIVATE
    gs3d_platform
    Catch2::Catch2WithMain
)
add_test(
    NAME GeoScatter3D.CpuInfo
    COMMAND GeoScatter3DCpuInfoTests
)

add_executable(GeoScatter3DGs3dV2Tests
    Gs3dV2Tests.cpp
)
target_include_directories(GeoScatter3DGs3dV2Tests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
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
    AppConfigTests.cpp
)
target_include_directories(GeoScatter3DAppConfigTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/third_party/tomlplusplus/include
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
    ViewportInteractionTests.cpp
)
target_include_directories(GeoScatter3DViewportInteractionTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
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
    LogTests.cpp
)
target_include_directories(GeoScatter3DLogTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(GeoScatter3DLogTests PRIVATE
    Catch2::Catch2WithMain
)
add_test(
    NAME GeoScatter3D.Log
    COMMAND GeoScatter3DLogTests
)

add_executable(GeoScatter3DViewportCameraSystemTests
    ViewportCameraSystemTests.cpp
)
target_include_directories(GeoScatter3DViewportCameraSystemTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
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
    WorkspaceManagerTests.cpp
)
target_include_directories(GeoScatter3DWorkspaceManagerTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
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
    BenchmarkSessionTests.cpp
)
target_include_directories(GeoScatter3DBenchmarkSessionTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
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
    ViewportPresentationStateTests.cpp
)
target_include_directories(GeoScatter3DViewportPresentationStateTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(GeoScatter3DViewportPresentationStateTests PRIVATE
    gs3d_app_support
    Catch2::Catch2WithMain
)
add_test(
    NAME GeoScatter3D.ViewportPresentationState
    COMMAND GeoScatter3DViewportPresentationStateTests
)

add_executable(GeoScatter3DViewportLodControllerTests
    ViewportLodControllerTests.cpp
)
target_include_directories(GeoScatter3DViewportLodControllerTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(GeoScatter3DViewportLodControllerTests PRIVATE
    gs3d_viewport_lod
    Catch2::Catch2WithMain
)
add_test(
    NAME GeoScatter3D.ViewportLodController
    COMMAND GeoScatter3DViewportLodControllerTests
)

add_executable(GeoScatter3DDataTests
    Gs3dFormatTests.cpp
)
target_include_directories(GeoScatter3DDataTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
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
    RuntimeLogicTests.cpp
)
target_include_directories(GeoScatter3DRuntimeLogicTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/third_party/tomlplusplus/include
)
target_link_libraries(GeoScatter3DRuntimeLogicTests PRIVATE
    gs3d_app_support
    gs3d_render
    gs3d_viewport_camera
    Catch2::Catch2WithMain
)
add_test(
    NAME GeoScatter3D.RuntimeLogic
    COMMAND GeoScatter3DRuntimeLogicTests
)

add_executable(GeoScatter3DUiPanelRegistryTests
    UiPanelRegistryTests.cpp
)
target_include_directories(GeoScatter3DUiPanelRegistryTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/third_party/tomlplusplus/include
)
target_link_libraries(GeoScatter3DUiPanelRegistryTests PRIVATE
    gs3d_app_support
    Catch2::Catch2WithMain
)
add_test(NAME GeoScatter3D.UiPanelRegistry COMMAND GeoScatter3DUiPanelRegistryTests)

add_executable(GeoScatter3DLayoutRegistryTests
    LayoutRegistryTests.cpp
)
target_include_directories(GeoScatter3DLayoutRegistryTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(GeoScatter3DLayoutRegistryTests PRIVATE
    gs3d_app_support
    Catch2::Catch2WithMain
)
add_test(NAME GeoScatter3D.LayoutRegistry COMMAND GeoScatter3DLayoutRegistryTests)

add_executable(GeoScatter3DThemeRegistryTests
    ThemeRegistryTests.cpp
)
target_include_directories(GeoScatter3DThemeRegistryTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/third_party/tomlplusplus/include
)
target_link_libraries(GeoScatter3DThemeRegistryTests PRIVATE
    gs3d_ui
    Catch2::Catch2WithMain
)
add_test(NAME GeoScatter3D.ThemeRegistry COMMAND GeoScatter3DThemeRegistryTests)

add_executable(GeoScatter3DUserPreferencesTests
    UserPreferencesTests.cpp
)
target_include_directories(GeoScatter3DUserPreferencesTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/third_party/tomlplusplus/include
)
target_link_libraries(GeoScatter3DUserPreferencesTests PRIVATE
    gs3d_app_support
    Catch2::Catch2WithMain
)
add_test(NAME GeoScatter3D.UserPreferences COMMAND GeoScatter3DUserPreferencesTests)

add_executable(GeoScatter3DViewerDatasetSessionTests
    ViewerDatasetSessionTests.cpp
)
target_include_directories(GeoScatter3DViewerDatasetSessionTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(GeoScatter3DViewerDatasetSessionTests PRIVATE
    gs3d_app
    Catch2::Catch2WithMain
)
add_test(
    NAME GeoScatter3D.ViewerDatasetSession
    COMMAND GeoScatter3DViewerDatasetSessionTests
)

add_executable(GeoScatter3DViewerAppStateInitializationTests
    ViewerAppStateInitializationTests.cpp
)
target_include_directories(GeoScatter3DViewerAppStateInitializationTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(GeoScatter3DViewerAppStateInitializationTests PRIVATE
    gs3d_app
    Catch2::Catch2WithMain
)
add_test(
    NAME GeoScatter3D.ViewerAppStateInitialization
    COMMAND GeoScatter3DViewerAppStateInitializationTests
)

add_executable(GeoScatter3DViewerAttributeMappingTests
    ViewerAttributeMappingTests.cpp
)
target_include_directories(GeoScatter3DViewerAttributeMappingTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(GeoScatter3DViewerAttributeMappingTests PRIVATE
    gs3d_app
    Catch2::Catch2WithMain
)
add_test(
    NAME GeoScatter3D.ViewerAttributeMapping
    COMMAND GeoScatter3DViewerAttributeMappingTests
)

add_executable(GeoScatter3DViewerRenderSettingsSystemTests
    ViewerRenderSettingsSystemTests.cpp
)
target_include_directories(GeoScatter3DViewerRenderSettingsSystemTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(GeoScatter3DViewerRenderSettingsSystemTests PRIVATE
    gs3d_app
    Catch2::Catch2WithMain
)
add_test(
    NAME GeoScatter3D.ViewerRenderSettingsSystem
    COMMAND GeoScatter3DViewerRenderSettingsSystemTests
)

add_executable(GeoScatter3DViewerCameraFrameSystemTests
    ViewerCameraFrameSystemTests.cpp
)
target_include_directories(GeoScatter3DViewerCameraFrameSystemTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(GeoScatter3DViewerCameraFrameSystemTests PRIVATE
    gs3d_app
    Catch2::Catch2WithMain
)
add_test(
    NAME GeoScatter3D.ViewerCameraFrameSystem
    COMMAND GeoScatter3DViewerCameraFrameSystemTests
)

add_executable(GeoScatter3DViewerCameraConfigurationTests
    ViewerCameraConfigurationTests.cpp
)
target_include_directories(GeoScatter3DViewerCameraConfigurationTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(GeoScatter3DViewerCameraConfigurationTests PRIVATE
    gs3d_app
    Catch2::Catch2WithMain
)
add_test(
    NAME GeoScatter3D.ViewerCameraConfiguration
    COMMAND GeoScatter3DViewerCameraConfigurationTests
)

add_executable(GeoScatter3DViewerFrameMetricsCollectorTests
    ViewerFrameMetricsCollectorTests.cpp
)
target_include_directories(GeoScatter3DViewerFrameMetricsCollectorTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(GeoScatter3DViewerFrameMetricsCollectorTests PRIVATE
    gs3d_app
    Catch2::Catch2WithMain
)
add_test(
    NAME GeoScatter3D.ViewerFrameMetricsCollector
    COMMAND GeoScatter3DViewerFrameMetricsCollectorTests
)

add_executable(GeoScatter3DViewerFrameClockTests
    ViewerFrameClockTests.cpp
)
target_include_directories(GeoScatter3DViewerFrameClockTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(GeoScatter3DViewerFrameClockTests PRIVATE
    gs3d_app_support
    Catch2::Catch2WithMain
)
add_test(
    NAME GeoScatter3D.ViewerFrameClock
    COMMAND GeoScatter3DViewerFrameClockTests
)

add_executable(GeoScatter3DViewerRuntimeConfigurationTests
    ViewerRuntimeConfigurationTests.cpp
)
target_include_directories(GeoScatter3DViewerRuntimeConfigurationTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(GeoScatter3DViewerRuntimeConfigurationTests PRIVATE
    gs3d_app
    Catch2::Catch2WithMain
)
add_test(
    NAME GeoScatter3D.ViewerRuntimeConfiguration
    COMMAND GeoScatter3DViewerRuntimeConfigurationTests
)

add_executable(GeoScatter3DViewerWorkbenchLayoutTests
    ViewerWorkbenchLayoutTests.cpp
)
target_include_directories(GeoScatter3DViewerWorkbenchLayoutTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(GeoScatter3DViewerWorkbenchLayoutTests PRIVATE
    gs3d_app_support
    Catch2::Catch2WithMain
)
add_test(
    NAME GeoScatter3D.ViewerWorkbenchLayout
    COMMAND GeoScatter3DViewerWorkbenchLayoutTests
)

add_executable(GeoScatter3DViewerLodFrameSystemTests
    ViewerLodFrameSystemTests.cpp
)
target_include_directories(GeoScatter3DViewerLodFrameSystemTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(GeoScatter3DViewerLodFrameSystemTests PRIVATE
    gs3d_app
    Catch2::Catch2WithMain
)
add_test(
    NAME GeoScatter3D.ViewerLodFrameSystem
    COMMAND GeoScatter3DViewerLodFrameSystemTests
)

add_executable(GeoScatter3DViewportAxisTicksTests
    ViewportAxisTicksTests.cpp
)
target_include_directories(GeoScatter3DViewportAxisTicksTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(GeoScatter3DViewportAxisTicksTests PRIVATE
    gs3d_ui
    Catch2::Catch2WithMain
)
add_test(
    NAME GeoScatter3D.ViewportAxisTicks
    COMMAND GeoScatter3DViewportAxisTicksTests
)

add_executable(GeoScatter3DViewerFrameStateSynchronizerTests
    ViewerFrameStateSynchronizerTests.cpp
)
target_include_directories(GeoScatter3DViewerFrameStateSynchronizerTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(GeoScatter3DViewerFrameStateSynchronizerTests PRIVATE
    gs3d_app
    Catch2::Catch2WithMain
)
add_test(
    NAME GeoScatter3D.ViewerFrameStateSynchronizer
    COMMAND GeoScatter3DViewerFrameStateSynchronizerTests
)

add_executable(GeoScatter3DScreenshotServiceTests
    ScreenshotServiceTests.cpp
)
target_include_directories(GeoScatter3DScreenshotServiceTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(GeoScatter3DScreenshotServiceTests PRIVATE
    gs3d_app
    Catch2::Catch2WithMain
)
add_test(
    NAME GeoScatter3D.ScreenshotService
    COMMAND GeoScatter3DScreenshotServiceTests
)

add_executable(GeoScatter3DComponentRegistryTests
    ComponentRegistryTests.cpp
)
target_include_directories(GeoScatter3DComponentRegistryTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(GeoScatter3DComponentRegistryTests PRIVATE
    gs3d_control
    Catch2::Catch2WithMain
)
add_test(
    NAME GeoScatter3D.ComponentRegistry
    COMMAND GeoScatter3DComponentRegistryTests
)

add_executable(GeoScatter3DJsonRpcTests
    JsonRpcTests.cpp
)
target_include_directories(GeoScatter3DJsonRpcTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(GeoScatter3DJsonRpcTests PRIVATE
    gs3d_control
    Catch2::Catch2WithMain
)
add_test(
    NAME GeoScatter3D.JsonRpc
    COMMAND GeoScatter3DJsonRpcTests
)

add_executable(GeoScatter3DControlPlaneTests
    ControlPlaneTests.cpp
)
target_include_directories(GeoScatter3DControlPlaneTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(GeoScatter3DControlPlaneTests PRIVATE
    gs3d_control
    Catch2::Catch2WithMain
)
add_test(
    NAME GeoScatter3D.ControlPlane
    COMMAND GeoScatter3DControlPlaneTests
)

add_executable(GeoScatter3DNavigationMapSystemTests
    NavigationMapSystemTests.cpp
)
target_include_directories(GeoScatter3DNavigationMapSystemTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(GeoScatter3DNavigationMapSystemTests PRIVATE
    gs3d_app
    Catch2::Catch2WithMain
)
add_test(
    NAME GeoScatter3D.NavigationMapSystem
    COMMAND GeoScatter3DNavigationMapSystemTests
)

add_executable(GeoScatter3DViewerPickSystemTests
    ViewerPickSystemTests.cpp
)
target_include_directories(GeoScatter3DViewerPickSystemTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(GeoScatter3DViewerPickSystemTests PRIVATE
    gs3d_app
    Catch2::Catch2WithMain
)
add_test(
    NAME GeoScatter3D.ViewerPickSystem
    COMMAND GeoScatter3DViewerPickSystemTests
)

add_executable(GeoScatter3DViewerBenchmarkControllerTests
    ViewerBenchmarkControllerTests.cpp
)
target_include_directories(GeoScatter3DViewerBenchmarkControllerTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(GeoScatter3DViewerBenchmarkControllerTests PRIVATE
    gs3d_app
    Catch2::Catch2WithMain
)
add_test(
    NAME GeoScatter3D.ViewerBenchmarkController
    COMMAND GeoScatter3DViewerBenchmarkControllerTests
)

add_executable(GeoScatter3DTileStreamingSystemTests
    TileStreamingSystemTests.cpp
)
target_include_directories(GeoScatter3DTileStreamingSystemTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(GeoScatter3DTileStreamingSystemTests PRIVATE
    gs3d_app
    Catch2::Catch2WithMain
)
add_test(
    NAME GeoScatter3D.TileStreamingSystem
    COMMAND GeoScatter3DTileStreamingSystemTests
)

add_executable(GeoScatter3DTileStreamingTests
    TileStreamingTests.cpp
)
target_include_directories(GeoScatter3DTileStreamingTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
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
    VulkanGpuInfoTests.cpp
)
target_include_directories(GeoScatter3DGpuInfoTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(GeoScatter3DGpuInfoTests PRIVATE
    gs3d_render
    Catch2::Catch2WithMain
)
add_test(
    NAME GeoScatter3D.GpuInfo
    COMMAND GeoScatter3DGpuInfoTests
)

# Python Script Quality Gate Tests

add_test(
    NAME GeoScatter3D.IncludeDeps
    COMMAND ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/scripts/check_include_deps.py
)

add_test(
    NAME GeoScatter3D.EngineeringGuardrails
    COMMAND ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/scripts/check_engineering_guardrails.py
)

# Fresh-clone smoke test: a tracked CSV is converted into an ignored
# bundle, exercising config resolution and the standalone preprocessor
# without requiring a window server or physical GPU.
add_test(
    NAME GeoScatter3D.FreshClonePreprocess
    COMMAND GeoScatter3DPreprocess
            --config
            ${PROJECT_SOURCE_DIR}/config/sample-viewer.toml
)
set_tests_properties(
    GeoScatter3D.FreshClonePreprocess
    PROPERTIES
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        ENVIRONMENT
            "GS3D_USER_CONFIG_DIR=${CMAKE_BINARY_DIR}/test-user-config"
)

# The preprocessor smoke test must prove that its output is consumable,
# not merely that it exits successfully. This test reads the generated
# GS3D v2 source and tile data back through production readers.
add_executable(GeoScatter3DFreshCloneBundleTests
    FreshCloneBundleTests.cpp
)
target_include_directories(GeoScatter3DFreshCloneBundleTests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
target_compile_definitions(GeoScatter3DFreshCloneBundleTests PRIVATE
    GS3D_SAMPLE_BUNDLE_DIR=\"${PROJECT_SOURCE_DIR}/data/sample-points.gs3d.bundle\"
)
target_link_libraries(GeoScatter3DFreshCloneBundleTests PRIVATE
    gs3d_data
    Catch2::Catch2WithMain
)
add_test(
    NAME GeoScatter3D.FreshCloneBundle
    COMMAND GeoScatter3DFreshCloneBundleTests
)
set_tests_properties(
    GeoScatter3D.FreshCloneBundle
    PROPERTIES
        DEPENDS GeoScatter3D.FreshClonePreprocess
)

# Manual GPU performance harness — not registered with add_test()/ctest
# (needs a real Vulkan device and takes seconds, not milliseconds; see
# docs/plan/geoscatter3d-tasks.md Task 1). Run directly:
#   ./build/GeoScatter3DBenchmark --config config/viewer.toml [frame_count]
add_executable(GeoScatter3DBenchmark
    BenchmarkSuite.cpp
)
target_include_directories(GeoScatter3DBenchmark PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/third_party/tomlplusplus/include
    ${PROJECT_SOURCE_DIR}/third_party/stb
    ${PROJECT_SOURCE_DIR}/third_party/nanosvg
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_link_libraries(GeoScatter3DBenchmark PRIVATE
    gs3d_app
)
copy_runtime_assets(GeoScatter3DBenchmark)
