# ---------------------------------------------------------------------------
# App Subsystem: State Models, Runtime Systems, and Orchestration
# ---------------------------------------------------------------------------

# App Support & State Model Library
add_library(gs3d_app_support STATIC
    AppConfig.cpp
    config/AppConfigValidation.cpp
    config/AppConfigViewerToml.cpp
    session/BenchmarkSession.cpp
    session/PreprocessedBundle.cpp
    config/RecentProjects.cpp
    config/ResourcePath.cpp
    config/UserPreferences.cpp
    session/TileLoadCommit.cpp
    session/TilePointCache.cpp
    systems/ViewportResizeScheduler.cpp
    state/ViewportInteractionState.cpp
    state/ViewportPresentationState.cpp
    systems/ViewerFrameClock.cpp
    state/ViewerWorkbenchLayout.cpp
    systems/MeasurementManager.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/AnalysisRailLayout.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/FloatingDockLayout.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/styling/LayoutRegistry.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/WorkspaceManager.cpp
)

target_include_directories(gs3d_app_support PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/third_party/tomlplusplus/include
)

target_link_libraries(gs3d_app_support PUBLIC
    gs3d_preprocess
    Vulkan::Vulkan
)

# App Orchestration & Systems Library
add_library(gs3d_app STATIC
    ViewerApp.cpp
    session/ViewerDatasetSession.cpp
    session/ViewerDatasetDescriptor.cpp
    session/ViewerBenchmarkController.cpp
    systems/ViewerKeyboardShortcutSystem.cpp
    systems/ViewerCameraFrameSystem.cpp
    systems/ViewerRenderSettingsSystem.cpp
    state/ViewerAttributeMapping.cpp
    systems/ViewerFrameMetricsCollector.cpp
    systems/ViewerLodFrameSystem.cpp
    config/ViewerRuntimeConfiguration.cpp
    systems/ViewerRenderRuntime.cpp
    systems/ViewerFrameRenderer.cpp
    systems/ViewerFrameStateSynchronizer.cpp
    ViewerAppStateInitialization.cpp
    ViewerAppViewport.cpp
    systems/ViewerPickSystem.cpp
    ViewerAppRenderView.cpp
    ViewerAppGuiCommands.cpp
    ViewerAppCameraCommands.cpp
    ViewerAppScreenshot.cpp
    ViewerAppRegionStats.cpp
    ViewerAppBenchmark.cpp
    systems/NavigationMapSystem.cpp
    ViewerAppPickDebugDump.cpp
    ViewerAppPointIds.cpp
    ViewerAppTileStreaming.cpp
    systems/ViewerViewportRenderSystem.cpp
    ViewerAppControlPlane.cpp
)

target_include_directories(gs3d_app PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(gs3d_app PUBLIC
    gs3d_app_support
    gs3d_ui
    gs3d_viewport_camera
    gs3d_viewport_lod
    gs3d_render
    gs3d_control
    gs3d_platform
    glfw
)

if(WIN32)
    target_link_libraries(gs3d_app PRIVATE comdlg32 ole32 shell32)
endif()
